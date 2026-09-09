// A tiny builder over UMaterialExpression nodes.
//
// Building a material in C++ means allocating an expression, registering it
// with the material, and wiring its input pins by hand. Written out at every
// site that is four lines of ceremony per node and a material is fifty nodes,
// which is how the first version of the water shader ended up with a Fresnel
// term connected to nothing.
//
// A header rather than a file, because it is used by all four materials and
// owned by none of them. Private to the module: nothing outside builds graphs.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Materials/Material.h"
#include "UObject/Package.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"

namespace LedgerSurface
{
		/// A material whose flags follow its outer.
		///
		/// Given a package, it is a real asset -- public, standalone, and
		/// therefore saveable. Given anything else it is transient, which is
		/// what an in-memory fallback wants. One line at each of the four
		/// creation sites instead of a second set of builders that would drift
		/// from the first.
		static UMaterial* NewMaterial(UObject* Outer, const TCHAR* Name)
		{
			const bool bAsset = Outer != nullptr && Outer->IsA<UPackage>();
			return NewObject<UMaterial>(
				Outer,
				bAsset ? FName(Name) : NAME_None,
				bAsset ? (RF_Public | RF_Standalone) : RF_Transient);
		}

	/// The graph builders. These are the source of truth for what a material
	/// *is*; `Create...` in the public header is how the game gets one, and it
	/// prefers the baked asset. Private, because nothing outside this module
	/// should be building a material graph.
	UMaterialInterface* BuildTerrainMaterial(UObject* Outer, uint32 Seed);
	UMaterialInterface* BuildWaterMaterial(UObject* Outer);
	UMaterialInterface* BuildUnderwaterMaterial(UObject* Outer);
	UMaterialInterface* BuildFlatMaterial(UObject* Outer);

	struct FGraph
	{
		UMaterial* Material = nullptr;

		template <typename T>
		T* Make()
		{
			T* Expression = NewObject<T>(Material);
			Material->GetExpressionCollection().AddExpression(Expression);
			return Expression;
		}

		UMaterialExpression* Constant(float Value)
		{
			UMaterialExpressionConstant* Node = Make<UMaterialExpressionConstant>();
			Node->R = Value;
			return Node;
		}

		UMaterialExpression* Mask(UMaterialExpression* Input, bool R, bool G, bool B)
		{
			UMaterialExpressionComponentMask* Node = Make<UMaterialExpressionComponentMask>();
			Node->Input.Expression = Input;
			Node->R = R;
			Node->G = G;
			Node->B = B;
			Node->A = false;
			return Node;
		}

		UMaterialExpression* Multiply(UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpressionMultiply* Node = Make<UMaterialExpressionMultiply>();
			Node->A.Expression = A;
			Node->B.Expression = B;
			return Node;
		}

		UMaterialExpression* Add(UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpressionAdd* Node = Make<UMaterialExpressionAdd>();
			Node->A.Expression = A;
			Node->B.Expression = B;
			return Node;
		}

		UMaterialExpression* Max(UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpressionMax* Node = Make<UMaterialExpressionMax>();
			Node->A.Expression = A;
			Node->B.Expression = B;
			return Node;
		}

		UMaterialExpression* Constant3(const FLinearColor& Value)
		{
			UMaterialExpressionConstant3Vector* Node = Make<UMaterialExpressionConstant3Vector>();
			Node->Constant = Value;
			return Node;
		}

		UMaterialExpression* Divide(UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpressionDivide* Node = Make<UMaterialExpressionDivide>();
			Node->A.Expression = A;
			Node->B.Expression = B;
			return Node;
		}

		UMaterialExpression* Subtract(UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpressionSubtract* Node = Make<UMaterialExpressionSubtract>();
			Node->A.Expression = A;
			Node->B.Expression = B;
			return Node;
		}

		UMaterialExpression* Scale(UMaterialExpression* A, float Factor)
		{
			return Multiply(A, Constant(Factor));
		}

		UMaterialExpression* Frac(UMaterialExpression* Input)
		{
			UMaterialExpressionFrac* Node = Make<UMaterialExpressionFrac>();
			Node->Input.Expression = Input;
			return Node;
		}

		UMaterialExpression* Saturate(UMaterialExpression* Input)
		{
			UMaterialExpressionSaturate* Node = Make<UMaterialExpressionSaturate>();
			Node->Input.Expression = Input;
			return Node;
		}

		UMaterialExpression* OneMinus(UMaterialExpression* Input)
		{
			UMaterialExpressionOneMinus* Node = Make<UMaterialExpressionOneMinus>();
			Node->Input.Expression = Input;
			return Node;
		}

		UMaterialExpression* Lerp(UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* Alpha)
		{
			UMaterialExpressionLinearInterpolate* Node = Make<UMaterialExpressionLinearInterpolate>();
			Node->A.Expression = A;
			Node->B.Expression = B;
			Node->Alpha.Expression = Alpha;
			return Node;
		}

		UMaterialExpression* Sample(UTexture2D* Texture, UMaterialExpression* Coordinates, EMaterialSamplerType Type)
		{
			UMaterialExpressionTextureSample* Node = Make<UMaterialExpressionTextureSample>();
			Node->Texture = Texture;
			Node->SamplerType = Type;
			Node->Coordinates.Expression = Coordinates;
			// The shared wrap sampler, not the texture asset's own.
			//
			// A shader gets sixteen sampler slots and the terrain now reaches
			// for twelve textures -- three biome surface sets plus rock, three
			// maps each. Every SSM_FromTextureAsset sample takes a slot of its
			// own and the material simply fails to compile past sixteen. Every
			// one of these textures wants exactly the same state anyway.
			Node->SamplerSource = SSM_Wrap_WorldGroupSettings;
			return Node;
		}

		/// A texture sample whose texture is a material parameter, so a dynamic
		/// instance can swap it. Used for the terrain's biome surface slots:
		/// which three grounds a patch is made of is a property of the patch,
		/// and the alternative is one material per combination.
		UMaterialExpression* SampleParameter(
			const TCHAR* ParameterName,
			UTexture2D* Default,
			UMaterialExpression* Coordinates,
			EMaterialSamplerType Type)
		{
			UMaterialExpressionTextureSampleParameter2D* Node =
				Make<UMaterialExpressionTextureSampleParameter2D>();
			Node->ParameterName = ParameterName;
			Node->Texture = Default;
			Node->SamplerType = Type;
			Node->Coordinates.Expression = Coordinates;
			Node->SamplerSource = SSM_Wrap_WorldGroupSettings;
			return Node;
		}

		/// World-aligned projection along all three axes, blended by the
		/// surface normal. No UVs, no seams, no pinching at the poles.
		UMaterialExpression* Triplanar(
			UTexture2D* Texture,
			UMaterialExpression* ScaledPosition,
			UMaterialExpression* WeightX,
			UMaterialExpression* WeightY,
			UMaterialExpression* WeightZ,
			EMaterialSamplerType Type)
		{
			UMaterialExpression* PlaneYZ = Mask(ScaledPosition, false, true, true);
			UMaterialExpression* PlaneXZ = Mask(ScaledPosition, true, false, true);
			UMaterialExpression* PlaneXY = Mask(ScaledPosition, true, true, false);

			UMaterialExpression* SampleX = Multiply(Sample(Texture, PlaneYZ, Type), WeightX);
			UMaterialExpression* SampleY = Multiply(Sample(Texture, PlaneXZ, Type), WeightY);
			UMaterialExpression* SampleZ = Multiply(Sample(Texture, PlaneXY, Type), WeightZ);

			return Add(Add(SampleX, SampleY), SampleZ);
		}

		/// The same projection, with the texture as a parameter. All three
		/// samples share one parameter name, so one instance value swaps the
		/// whole projection.
		UMaterialExpression* TriplanarParameter(
			const TCHAR* ParameterName,
			UTexture2D* Default,
			UMaterialExpression* ScaledPosition,
			UMaterialExpression* WeightX,
			UMaterialExpression* WeightY,
			UMaterialExpression* WeightZ,
			EMaterialSamplerType Type)
		{
			UMaterialExpression* PlaneYZ = Mask(ScaledPosition, false, true, true);
			UMaterialExpression* PlaneXZ = Mask(ScaledPosition, true, false, true);
			UMaterialExpression* PlaneXY = Mask(ScaledPosition, true, true, false);

			UMaterialExpression* SampleX =
				Multiply(SampleParameter(ParameterName, Default, PlaneYZ, Type), WeightX);
			UMaterialExpression* SampleY =
				Multiply(SampleParameter(ParameterName, Default, PlaneXZ, Type), WeightY);
			UMaterialExpression* SampleZ =
				Multiply(SampleParameter(ParameterName, Default, PlaneXY, Type), WeightZ);

			return Add(Add(SampleX, SampleY), SampleZ);
		}

		/// A named scalar parameter with a default.
		UMaterialExpression* ScalarParameter(const TCHAR* ParameterName, float Default)
		{
			UMaterialExpressionScalarParameter* Node = Make<UMaterialExpressionScalarParameter>();
			Node->ParameterName = ParameterName;
			Node->DefaultValue = Default;
			return Node;
		}

		/// A named vector parameter with a default.
		UMaterialExpression* VectorParameter(const TCHAR* ParameterName, const FLinearColor& Default)
		{
			UMaterialExpressionVectorParameter* Node = Make<UMaterialExpressionVectorParameter>();
			Node->ParameterName = ParameterName;
			Node->DefaultValue = Default;
			return Node;
		}
	};
}

#endif
