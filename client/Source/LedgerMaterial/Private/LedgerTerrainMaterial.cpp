// The ground: triplanar detail at two scales over vertex colour.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"

namespace LedgerSurface
{
	UMaterialInterface* CreateTerrainMaterial(UObject* Outer, uint32 Seed)
	{
		UMaterial* Material = NewObject<UMaterial>(Outer, NAME_None, RF_Transient);
		if (Material == nullptr)
		{
			return nullptr;
		}

		UTexture2D* Albedo = CreateDetailAlbedo(Material, 256, Seed ^ 0xD1D1u);
		UTexture2D* NormalMap = CreateDetailNormal(Material, 256, Seed ^ 0xD1D1u, 3.5);
		if (Albedo == nullptr || NormalMap == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);
		Material->TwoSided = false;

		FGraph Graph;
		Graph.Material = Material;

		// Camera-relative, not absolute: an absolute position on a 6.37e8 cm
		// planet spends all of its float precision on the exponent, and the
		// projection degenerates into banding. Camera-relative keeps full
		// precision exactly where the detail is visible.
		UMaterialExpressionWorldPosition* WorldPosition = Graph.Make<UMaterialExpressionWorldPosition>();
		WorldPosition->WorldPositionShaderOffset = WPT_CameraRelativeNoOffsets;

		// Blend weights: |N| normalised so the three projections sum to one.
		UMaterialExpressionVertexNormalWS* Normal = Graph.Make<UMaterialExpressionVertexNormalWS>();
		UMaterialExpressionAbs* AbsoluteNormal = Graph.Make<UMaterialExpressionAbs>();
		AbsoluteNormal->Input.Expression = Normal;

		UMaterialExpressionConstant3Vector* Ones = Graph.Make<UMaterialExpressionConstant3Vector>();
		Ones->Constant = FLinearColor(1.0f, 1.0f, 1.0f);
		UMaterialExpressionDotProduct* WeightSum = Graph.Make<UMaterialExpressionDotProduct>();
		WeightSum->A.Expression = AbsoluteNormal;
		WeightSum->B.Expression = Ones;

		UMaterialExpression* Weights = Graph.Divide(AbsoluteNormal, WeightSum);
		UMaterialExpression* WeightX = Graph.Mask(Weights, true, false, false);
		UMaterialExpression* WeightY = Graph.Mask(Weights, false, true, false);
		UMaterialExpression* WeightZ = Graph.Mask(Weights, false, false, true);

		// Two scales. The near one carries the grain you see standing on it; the
		// macro one breaks up the tiling that a single scale makes obvious from
		// the air. 400 cm and 90 m.
		UMaterialExpression* NearPosition = Graph.Multiply(WorldPosition, Graph.Constant(1.0f / 400.0f));
		UMaterialExpression* MacroPosition = Graph.Multiply(WorldPosition, Graph.Constant(1.0f / 9000.0f));

		UMaterialExpression* NearDetail = Graph.Triplanar(
			Albedo, NearPosition, WeightX, WeightY, WeightZ, SAMPLERTYPE_LinearColor);
		UMaterialExpression* MacroDetail = Graph.Triplanar(
			Albedo, MacroPosition, WeightX, WeightY, WeightZ, SAMPLERTYPE_LinearColor);

		// Distance fade. Mips stop the near detail from aliasing, but by the time
		// a 4 m pattern is a few pixels across it is only noise on the histogram
		// — fading it to neutral before then is both cheaper and cleaner.
		UMaterialExpressionPixelDepth* Depth = Graph.Make<UMaterialExpressionPixelDepth>();
		UMaterialExpressionSaturate* Fade = Graph.Make<UMaterialExpressionSaturate>();
		Fade->Input.Expression = Graph.Divide(Depth, Graph.Constant(60000.0f)); // 600 m

		UMaterialExpression* NearFaded = Graph.Lerp(NearDetail, Graph.Constant(1.0f), Fade);

		UMaterialExpressionVertexColor* VertexColour = Graph.Make<UMaterialExpressionVertexColor>();
		UMaterialExpression* BaseColour = Graph.Multiply(Graph.Multiply(VertexColour, NearFaded), MacroDetail);

		// Normal, faded to flat over the same distance. A normal map that
		// survives past its mip range is the other half of the shimmer.
		UMaterialExpression* TriplanarNormal = Graph.Triplanar(
			NormalMap, NearPosition, WeightX, WeightY, WeightZ, SAMPLERTYPE_Normal);
		UMaterialExpressionConstant3Vector* FlatNormal = Graph.Make<UMaterialExpressionConstant3Vector>();
		FlatNormal->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
		UMaterialExpression* FadedNormal = Graph.Lerp(TriplanarNormal, FlatNormal, Fade);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->BaseColor.Expression = BaseColour;
		EditorData->Normal.Expression = FadedNormal;
		EditorData->Roughness.Expression = Graph.Constant(0.93f);
		EditorData->Specular.Expression = Graph.Constant(0.05f);

		Material->PostEditChange();
		return Material;
	}
}

#endif
