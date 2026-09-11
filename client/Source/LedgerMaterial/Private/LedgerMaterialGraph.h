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
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionNormalize.h"
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
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionSign.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialParameterCollection.h"

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
	UMaterialInterface* BuildVisorMaterial(UObject* Outer);
	UMaterialInterface* BuildAuroraMaterial(UObject* Outer);
	UMaterialInterface* BuildFlatMaterial(UObject* Outer);
	UMaterialInterface* BuildCloudMaterial(UObject* Outer);
	UMaterialInterface* BuildGroundFogMaterial(UObject* Outer);
	UMaterialInterface* BuildStarMaterial(UObject* Outer);
	UMaterialInterface* BuildFoliageMaterial(UObject* Outer);

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

		UMaterialExpression* Mask(UMaterialExpression* Input, bool R, bool G, bool B, bool A = false)
		{
			UMaterialExpressionComponentMask* Node = Make<UMaterialExpressionComponentMask>();
			Node->Input.Expression = Input;
			Node->R = R;
			Node->G = G;
			Node->B = B;
			Node->A = A;
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

		/// Two expressions into one wider one, for building a swizzle.
		UMaterialExpression* Append(UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpressionAppendVector* Node = Make<UMaterialExpressionAppendVector>();
			Node->A.Expression = A;
			Node->B.Expression = B;
			return Node;
		}

		UMaterialExpression* Frac(UMaterialExpression* Input)
		{
			UMaterialExpressionFrac* Node = Make<UMaterialExpressionFrac>();
			Node->Input.Expression = Input;
			return Node;
		}

		/// Distance between two positions. Used for altitude on a sphere, which
		/// is distance from the centre and not world Z.
		UMaterialExpression* Distance(UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpressionDistance* Node = Make<UMaterialExpressionDistance>();
			Node->A.Expression = A;
			Node->B.Expression = B;
			return Node;
		}

		UMaterialExpression* Sine(UMaterialExpression* Input)
		{
			UMaterialExpressionSine* Node = Make<UMaterialExpressionSine>();
			Node->Input.Expression = Input;
			// One cycle per unit of input, so the caller's scale is a
			// wavelength rather than a wavelength over two pi.
			Node->Period = 1.0f;
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

		/// A triplanar normal, which is not the same thing as a triplanar
		/// anything else and was being treated as one.
		///
		/// Each projection's normal map is in ITS OWN tangent frame: the X
		/// projection reads the YZ plane, so its red channel points along world
		/// Y and its blue along world X. Adding three of those together
		/// weighted -- which is what `Triplanar` does, correctly, for scalars
		/// and colours -- adds three vectors that are not in the same space.
		///
		/// Measured before it was fixed, on the ground the game actually draws:
		/// the mean normal handed to the shading model decoded to
		/// (-0.26, -0.26, 0.11). Length 0.39 instead of 1, pointing mostly
		/// sideways instead of up. Every lit pixel of terrain was shaded with
		/// that.
		///
		/// A planet makes it worse than it would be on a level. Triplanar
		/// weights come from the surface normal, and on a sphere that is the
		/// radial direction -- so all three projections carry real weight
		/// almost everywhere, and the incoherent sum is the common case rather
		/// than the edge case.
		///
		/// So: swizzle each projection into world space, then blend, then
		/// normalise. The result is a world-space normal, and the material has
		/// to say so (`bTangentSpaceNormal = false`).
		///
		/// ponytail: the sign of each projection's axis is not applied, so a
		/// surface facing -X gets the normal of one facing +X. That is what
		/// whiteout blending does too, the weights are already `abs`, and no
		/// measurement has yet shown it.
		UMaterialExpression* TriplanarNormalParameter(
			const TCHAR* ParameterName,
			UTexture2D* Default,
			UMaterialExpression* ScaledPosition,
			UMaterialExpression* WeightX,
			UMaterialExpression* WeightY,
			UMaterialExpression* WeightZ)
		{
			UMaterialExpression* PlaneYZ = Mask(ScaledPosition, false, true, true);
			UMaterialExpression* PlaneXZ = Mask(ScaledPosition, true, false, true);
			UMaterialExpression* PlaneXY = Mask(ScaledPosition, true, true, false);

			UMaterialExpression* TangentX =
				SampleParameter(ParameterName, Default, PlaneYZ, SAMPLERTYPE_Normal);
			UMaterialExpression* TangentY =
				SampleParameter(ParameterName, Default, PlaneXZ, SAMPLERTYPE_Normal);
			UMaterialExpression* TangentZ =
				SampleParameter(ParameterName, Default, PlaneXY, SAMPLERTYPE_Normal);

			// **Which way is out.**
			//
			// A normal map's blue channel is the component out of the surface,
			// and it is always positive -- a tangent-space normal never points
			// into its own surface. Swizzling it straight onto +x, +y or +z
			// therefore assumes every projection faces the positive axis, which
			// on a sphere is true for exactly half of it.
			//
			// The other half got normals pointing INTO the ground, and where
			// the blend mixed one of those with a correct one they cancelled.
			// The result was terrain with a perfectly good albedo that took no
			// light at all: with the atmosphere switched off, the ground at
			// noon under a sun 75 degrees up measured 0.00 out of 255, while
			// the same frame unlit measured 128.9. Every normal render was
			// in-scattered air in front of a black surface, which is what made
			// it read as haze and silhouette rather than as ground.
			//
			// The sign of the vertex normal is what says which way each
			// projection is actually facing, so the out-of-surface channel
			// carries it.
			UMaterialExpressionVertexNormalWS* Facing = Make<UMaterialExpressionVertexNormalWS>();
			UMaterialExpressionSign* Which = Make<UMaterialExpressionSign>();
			Which->Input.Expression = Facing;

			UMaterialExpression* OutOfX =
				Multiply(Mask(TangentX, false, false, true), Mask(Which, true, false, false));
			UMaterialExpression* OutOfY =
				Multiply(Mask(TangentY, false, false, true), Mask(Which, false, true, false));
			UMaterialExpression* OutOfZ =
				Multiply(Mask(TangentZ, false, false, true), Mask(Which, false, false, true));

			// X reads (y, z), so its (r, g, b) is world (y, z, x) -> (b, r, g).
			UMaterialExpression* WorldX = Append(
				Append(OutOfX, Mask(TangentX, true, false, false)),
				Mask(TangentX, false, true, false));
			// Y reads (x, z), so its (r, g, b) is world (x, z, y) -> (r, b, g).
			UMaterialExpression* WorldY = Append(
				Append(Mask(TangentY, true, false, false), OutOfY),
				Mask(TangentY, false, true, false));
			// Z reads (x, y), which is already world order.
			UMaterialExpression* WorldZ = Append(
				Append(Mask(TangentZ, true, false, false), Mask(TangentZ, false, true, false)),
				OutOfZ);

			UMaterialExpression* Blended = Add(Add(
				Multiply(WorldX, WeightX),
				Multiply(WorldY, WeightY)),
				Multiply(WorldZ, WeightZ));

			UMaterialExpressionNormalize* Unit = Make<UMaterialExpressionNormalize>();
			Unit->VectorInput.Expression = Blended;
			return Unit;
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

		/// A value read from a material parameter collection.
		///
		/// **By id, not only by name.** The node finds its parameter by the
		/// parameter's GUID; the name is for people. Picking a parameter in the
		/// editor fills both in, and a builder that sets only the name gets a
		/// node the compiler rejects -- "CollectionParameter has invalid
		/// parameter WindDirection" -- and the whole material falls back to
		/// the engine default. That is what M_Foliage did on its first bake:
		/// every canopy in the forest drawn with the default material.
		UMaterialExpression* CollectionParameter(
			UMaterialParameterCollection* Collection, const TCHAR* ParameterName)
		{
			UMaterialExpressionCollectionParameter* Node =
				Make<UMaterialExpressionCollectionParameter>();
			Node->Collection = Collection;
			Node->ParameterName = ParameterName;
			Node->ParameterId = Collection != nullptr
				? Collection->GetParameterId(FName(ParameterName)) : FGuid();
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
