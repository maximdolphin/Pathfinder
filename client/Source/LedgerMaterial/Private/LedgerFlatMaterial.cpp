// Plain lit colour for the buildings and the trees.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"

namespace LedgerSurface
{
	UMaterialInterface* BuildFlatMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Flat"));
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);

		// Declared usable on instanced components, and this is not optional.
		//
		// Unreal validates material usage per component type, and a material
		// without this flag is silently swapped for the default one on an
		// instanced static mesh. Three hundred and forty-two instanced trees
		// rendered as black cut-outs with the material assigned, the material
		// slot present and the vertex colours present -- because the thing being
		// drawn was not this material at all.
		Material->bUsedWithInstancedStaticMeshes = true;

		FGraph Graph;
		Graph.Material = Material;

		// Parameters, not constants.
		//
		// The settlement wants a different colour per building and the trees a
		// different one per canopy, and a constant means a material each --
		// which cannot be baked, which is why a packaged build had no material
		// on any building, tree or hull at all. As parameters there is one
		// baked material and a dynamic instance per colour, which works in a
		// cooked build because the parent is a real asset.
		UMaterialExpressionVectorParameter* Tint = Graph.Make<UMaterialExpressionVectorParameter>();
		Tint->ParameterName = TEXT("Tint");
		Tint->DefaultValue = FLinearColor::White;

		// Multiplied by vertex colour, not replacing it. The buildings and trees
		// carry all their variation per-vertex, so passing white here yields
		// exactly those colours and passing a tint shades the whole set.
		UMaterialExpressionVertexColor* VertexColour = Graph.Make<UMaterialExpressionVertexColor>();
		UMaterialExpression* Base = Graph.Multiply(Tint, VertexColour);

		UMaterialExpressionScalarParameter* Roughness =
			Graph.Make<UMaterialExpressionScalarParameter>();
		Roughness->ParameterName = TEXT("Roughness");
		Roughness->DefaultValue = 0.82f;

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->BaseColor.Expression = Base;
		EditorData->Roughness.Expression = Roughness;
		EditorData->Specular.Expression = Graph.Constant(0.4f);

		Material->PostEditChange();
		return Material;
	}
}

#endif
