// The sea, from below: a post-process material, because the thing that
// matters underwater is contrast loss with distance and distance is what
// FPostProcessSettings has no field for.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionSceneTexture.h"

namespace LedgerSurface
{
	UMaterialInterface* BuildUnderwaterMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Underwater"));
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_PostProcess;

		FGraph Graph;
		Graph.Material = Material;

		UMaterialExpressionSceneTexture* SceneColour = Graph.Make<UMaterialExpressionSceneTexture>();
		SceneColour->SceneTextureId = PPI_PostProcessInput0;

		UMaterialExpressionSceneTexture* SceneDepth = Graph.Make<UMaterialExpressionSceneTexture>();
		SceneDepth->SceneTextureId = PPI_SceneDepth;

		// Water swallows red first and blue last, which is why everything more
		// than a few metres away is one flat blue-green regardless of what it
		// actually is. Ten metres is a coastal shelf with sand moving in it;
		// open ocean would be five or six times that.
		constexpr float Visibility = 1000.0f;

		// This lands after tonemapping, so the constant is a final screen value
		// and not a radiance. The first pass used a physically sensible 0.018
		// blue and produced a black screen: at that point in the pipeline there
		// is no exposure left to lift it.
		UMaterialExpressionConstant3Vector* Murk = Graph.Make<UMaterialExpressionConstant3Vector>();
		Murk->Constant = FLinearColor(0.050f, 0.170f, 0.210f);

		UMaterialExpression* Extinction = Graph.Saturate(
			Graph.Divide(
				Graph.Mask(SceneDepth, true, false, false),
				Graph.Constant(Visibility)));

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}

		// Post-process materials write their result to emissive; there is no
		// surface here to shade.
		// Masked to RGB first. The scene texture output is a float4 and the murk
		// is a float3, and a Lerp between the two does not compile — which UE
		// reports as one warning and a silent swap to the default material, so
		// the post-process pass draws WorldGridMaterial across the whole screen
		// instead of doing nothing.
		EditorData->EmissiveColor.Expression =
			Graph.Lerp(Graph.Mask(SceneColour, true, true, true), Murk, Extinction);

		Material->PostEditChange();
		return Material;
	}
}

#endif
