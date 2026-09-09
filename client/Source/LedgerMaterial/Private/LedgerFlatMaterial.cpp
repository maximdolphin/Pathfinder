// Plain lit colour for the buildings and the trees.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionVertexColor.h"

namespace LedgerSurface
{
	UMaterialInterface* CreateFlatMaterial(UObject* Outer, const FLinearColor& Colour, float Roughness)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Flat"));
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);

		FGraph Graph;
		Graph.Material = Material;

		UMaterialExpressionConstant3Vector* Tint = Graph.Make<UMaterialExpressionConstant3Vector>();
		Tint->Constant = Colour;

		// Multiplied by vertex colour, not replacing it. The buildings and trees
		// carry all their variation per-vertex, so passing white here yields
		// exactly those colours and passing a tint shades the whole set.
		UMaterialExpressionVertexColor* VertexColour = Graph.Make<UMaterialExpressionVertexColor>();
		UMaterialExpression* Base = Graph.Multiply(Tint, VertexColour);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->BaseColor.Expression = Base;
		EditorData->Roughness.Expression = Graph.Constant(Roughness);
		EditorData->Specular.Expression = Graph.Constant(0.4f);

		Material->PostEditChange();
		return Material;
	}
}

#endif
