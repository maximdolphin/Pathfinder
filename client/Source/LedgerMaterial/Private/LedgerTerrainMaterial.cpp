// The ground: triplanar detail at two scales over vertex colour.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
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
		// ---- geomorph ----------------------------------------------------
		//
		// A vertex at an odd grid position does not exist in the parent LOD.
		// When this node collapses, it vanishes and the surface snaps to the
		// parent's coarser sampling — the pop. UV1.x is how far this vertex has
		// to move along its radial to already be where the parent would put it;
		// UV1.y is the node's world size.
		//
		// The blend has to be driven by the same quantity the LOD decision uses,
		// or the two disagree and the pop moves rather than disappearing. A node
		// collapses when its *parent's* projected error falls below the
		// threshold, and the parent is twice the size — so the collapse distance
		// is 2 * WorldSize * MorphScale, where MorphScale packs viewport width,
		// field of view and the pixel threshold into one number the planet sets
		// once per frame.
		UMaterialExpressionTextureCoordinate* MorphCoord =
			Graph.Make<UMaterialExpressionTextureCoordinate>();
		MorphCoord->CoordinateIndex = 1;

		UMaterialExpression* MorphDelta = Graph.Mask(MorphCoord, true, false, false);
		UMaterialExpression* NodeSize = Graph.Mask(MorphCoord, false, true, false);

		UMaterialExpressionScalarParameter* MorphScale =
			Graph.Make<UMaterialExpressionScalarParameter>();
		MorphScale->ParameterName = TEXT("MorphScale");
		// A sane still-frame default, so the material is correct even if nobody
		// ever sets it. The planet overrides it every frame.
		MorphScale->DefaultValue = 12.0f;

		UMaterialExpressionScalarParameter* MorphBegin =
			Graph.Make<UMaterialExpressionScalarParameter>();
		MorphBegin->ParameterName = TEXT("MorphBegin");
		// Blend over the last 30% of the node's life. Sooner and distant terrain
		// is permanently coarser than it needs to be; later and the blend has to
		// happen fast enough to be visible as motion.
		MorphBegin->DefaultValue = 0.7f;

		UMaterialExpressionDistance* ToCamera = Graph.Make<UMaterialExpressionDistance>();
		ToCamera->A.Expression = Graph.Make<UMaterialExpressionCameraPositionWS>();
		UMaterialExpressionWorldPosition* AbsolutePosition =
			Graph.Make<UMaterialExpressionWorldPosition>();
		AbsolutePosition->WorldPositionShaderOffset = WPT_Default;
		ToCamera->B.Expression = AbsolutePosition;

		// How far through its life this node is: 0 when freshly split, 1 at the
		// distance its parent takes over.
		UMaterialExpression* CollapseDistance = Graph.Multiply(
			Graph.Multiply(NodeSize, Graph.Constant(2.0f)), MorphScale);
		UMaterialExpression* Through = Graph.Divide(ToCamera, CollapseDistance);

		UMaterialExpression* MorphFactor = Graph.Saturate(
			Graph.Divide(
				Graph.Subtract(Through, MorphBegin),
				Graph.Subtract(Graph.Constant(1.0f), MorphBegin)));

		// Radial, because that is the direction the elevation difference is in.
		// The planet's centre is a parameter rather than the world origin: the
		// two coincide today and will not once there is a second body.
		UMaterialExpressionVectorParameter* PlanetCentre =
			Graph.Make<UMaterialExpressionVectorParameter>();
		PlanetCentre->ParameterName = TEXT("PlanetCentre");
		PlanetCentre->DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

		UMaterialExpressionNormalize* Radial = Graph.Make<UMaterialExpressionNormalize>();
		Radial->VectorInput.Expression = Graph.Subtract(
			AbsolutePosition, Graph.Mask(PlanetCentre, true, true, true));

		EditorData->WorldPositionOffset.Expression =
			Graph.Multiply(Radial, Graph.Multiply(MorphDelta, MorphFactor));

		EditorData->BaseColor.Expression = BaseColour;
		EditorData->Normal.Expression = FadedNormal;
		EditorData->Roughness.Expression = Graph.Constant(0.93f);
		EditorData->Specular.Expression = Graph.Constant(0.05f);

		Material->PostEditChange();
		return Material;
	}
}

#endif
