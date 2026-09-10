// Unlit emissive, for things that make their own light. T077's stars.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"

namespace LedgerSurface
{
	UMaterialInterface* BuildStarMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Star"));
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_Unlit);

		// **Declared usable on instanced meshes, and this is not optional.**
		// The same flag whose absence rendered three hundred and forty-two
		// trees as black cut-outs in M02. A star field is one instanced mesh
		// with several hundred instances in it.
		Material->bUsedWithInstancedStaticMeshes = true;

		FGraph Graph;
		Graph.Material = Material;

		// **Per instance, not per vertex.**
		//
		// A star's colour is its temperature -- a hot one is blue and a cool one
		// orange, and T077 computed both -- and its brightness is its apparent
		// magnitude. An instanced static mesh does not carry a vertex colour per
		// instance, whatever the name suggests; the four floats of custom data
		// are how a value reaches one instance and not the rest. Eight hundred
		// stars cannot be eight hundred materials.
		auto Custom = [&Graph](int32 Index)
		{
			UMaterialExpressionPerInstanceCustomData* Node =
				Graph.Make<UMaterialExpressionPerInstanceCustomData>();
			Node->DataIndex = Index;
			Node->ConstDefaultValue = 1.0f;
			return Node;
		};
		UMaterialExpression* Colour = Graph.Append(
			Graph.Append(Custom(1), Custom(2)), Custom(3));
		UMaterialExpression* PerStar = Graph.Multiply(Colour, Custom(0));

		UMaterialExpressionVectorParameter* Tint =
			Graph.Make<UMaterialExpressionVectorParameter>();
		Tint->ParameterName = TEXT("Tint");
		Tint->DefaultValue = FLinearColor::White;

		// **Emissive in nits, and the number is large.**
		//
		// The scene is lit in real photometric units -- the sun is a hundred
		// thousand lux -- so an emissive that reads as a star at night has to be
		// in the same units as everything else. A first-magnitude star is about
		// a thousandth of a lux at the eye, which spread over the few pixels it
		// covers wants a luminance in the thousands rather than the ones.
		UMaterialExpressionScalarParameter* Brightness =
			Graph.Make<UMaterialExpressionScalarParameter>();
		Brightness->ParameterName = TEXT("Brightness");
		Brightness->DefaultValue = 6000.0f;

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->EmissiveColor.Expression =
			Graph.Multiply(Graph.Multiply(PerStar, Tint), Brightness);

		Material->PostEditChange();
		return Material;
	}
}

#endif
