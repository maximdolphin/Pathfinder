// The canopy from inside: a post-process material over the cockpit view. T101.
//
// Five numbers from the ship's canopy state -- water, which way it runs, ice,
// fog and dust -- and nothing else. At zero on all of them this passes the
// scene straight through, so it sits on the camera at full weight for good.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionTime.h"

namespace LedgerSurface
{
	UMaterialInterface* BuildVisorMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Visor"));
		if (Material == nullptr)
		{
			return nullptr;
		}
		Material->MaterialDomain = MD_PostProcess;

		FGraph Graph;
		Graph.Material = Material;

		UMaterialExpression* Water = Graph.ScalarParameter(TEXT("VisorWater"), 0.0f);
		UMaterialExpression* Flow = Graph.ScalarParameter(TEXT("VisorFlow"), -1.0f);
		UMaterialExpression* Ice = Graph.ScalarParameter(TEXT("VisorIce"), 0.0f);
		UMaterialExpression* Fog = Graph.ScalarParameter(TEXT("VisorFog"), 0.0f);
		UMaterialExpression* Dust = Graph.ScalarParameter(TEXT("VisorDust"), 0.0f);

		auto Append = [&Graph](UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpressionAppendVector* Node = Graph.Make<UMaterialExpressionAppendVector>();
			Node->A.Expression = A;
			Node->B.Expression = B;
			return Node;
		};

		UMaterialExpressionScreenPosition* Screen = Graph.Make<UMaterialExpressionScreenPosition>();
		UMaterialExpression* UV = Screen;
		UMaterialExpression* ScreenU = Graph.Mask(UV, true, false, false);
		UMaterialExpression* ScreenV = Graph.Mask(UV, false, true, false);
		UMaterialExpressionTime* Time = Graph.Make<UMaterialExpressionTime>();

		// **Streaks: noise stretched down the screen, sliding the way the water
		// runs.** Down the glass is down the screen; back along it, looking
		// forward, is up the screen. The flow sets both which way and how fast,
		// so accelerating turns the streaks round without a switch anywhere.
		UMaterialExpression* Slide = Graph.Multiply(Graph.Multiply(Time, Flow), Graph.Constant(-0.35f));
		UMaterialExpressionNoise* Drops = Graph.Make<UMaterialExpressionNoise>();
		Drops->Position.Expression = Append(Append(
			Graph.Multiply(ScreenU, Graph.Constant(60.0f)),
			Graph.Multiply(Graph.Add(ScreenV, Slide), Graph.Constant(7.0f))), Graph.Constant(0.0f));
		Drops->NoiseFunction = NOISEFUNCTION_GradientTex;
		Drops->Scale = 1.0f;
		Drops->Levels = 2;
		Drops->OutputMin = 0.0f;
		Drops->OutputMax = 1.0f;
		Drops->bTurbulence = false;

		// How much of the glass is running with water: the threshold walks down
		// the noise as the water rises, so zero water is zero streaks.
		UMaterialExpression* Wet = Graph.Saturate(Graph.Multiply(
			Graph.Subtract(Drops, Graph.OneMinus(Graph.Multiply(Water, Graph.Constant(0.6f)))),
			Graph.Constant(10.0f)));

		// Water on glass bends what is behind it.
		UMaterialExpression* Bend = Graph.Multiply(Wet, Graph.Constant(0.012f));
		auto Scene = [&Graph, &Append](UMaterialExpression* At)
		{
			UMaterialExpressionSceneTexture* Sample = Graph.Make<UMaterialExpressionSceneTexture>();
			Sample->SceneTextureId = PPI_PostProcessInput0;
			Sample->Coordinates.Expression = At;
			return Graph.Mask(Sample, true, true, true);
		};
		UMaterialExpression* Bent = Graph.Add(UV, Append(Bend, Graph.Multiply(Bend, Graph.Constant(0.5f))));
		UMaterialExpression* Colour = Scene(Bent);

		// Fog and ice scatter: four taps around the pixel, as wide as they are thick.
		UMaterialExpression* Spread = Graph.Multiply(Graph.Saturate(Graph.Add(Fog, Ice)), Graph.Constant(0.008f));
		UMaterialExpression* Zero = Graph.Constant(0.0f);
		UMaterialExpression* Blurred = Colour;
		for (int32 Tap = 0; Tap < 4; ++Tap)
		{
			UMaterialExpression* Signed = Graph.Multiply(Spread, Graph.Constant(Tap % 2 == 0 ? 1.0f : -1.0f));
			UMaterialExpression* Offset = Tap < 2 ? Append(Signed, Zero) : Append(Zero, Signed);
			Blurred = Graph.Add(Blurred, Scene(Graph.Add(Bent, Offset)));
		}
		Blurred = Graph.Multiply(Blurred, Graph.Constant(0.2f));
		Colour = Graph.Lerp(Colour, Blurred, Graph.Saturate(Graph.Add(Fog, Ice)));

		// Fog: the cabin's condensation, milky and flat.
		Colour = Graph.Lerp(Colour,
			Graph.Add(Graph.Multiply(Colour, Graph.Constant(0.35f)), Graph.Constant3(FLinearColor(0.55f, 0.57f, 0.60f))),
			Graph.Multiply(Fog, Graph.Constant(0.85f)));

		// Ice: frost from the edges of the glass inwards.
		UMaterialExpression* FromCentre = Graph.Subtract(UV, Graph.Constant(0.5f));
		UMaterialExpressionDotProduct* Reach = Graph.Make<UMaterialExpressionDotProduct>();
		Reach->A.Expression = FromCentre;
		Reach->B.Expression = FromCentre;
		UMaterialExpression* Edge = Graph.Saturate(Graph.Multiply(Graph.Subtract(Reach, Graph.Constant(0.06f)), Graph.Constant(6.0f)));
		UMaterialExpression* Frost = Graph.Saturate(Graph.Add(
			Graph.Subtract(Graph.Multiply(Ice, Graph.Constant(1.5f)), Graph.OneMinus(Edge)),
			Graph.Multiply(Drops, Graph.Multiply(Ice, Graph.Constant(0.3f)))));
		Colour = Graph.Lerp(Colour, Graph.Constant3(FLinearColor(0.85f, 0.90f, 0.95f)), Graph.Multiply(Frost, Graph.Constant(0.9f)));

		// Dust: a brown film that takes the contrast.
		Colour = Graph.Lerp(Colour, Graph.Constant3(FLinearColor(0.45f, 0.38f, 0.30f)), Graph.Multiply(Dust, Graph.Constant(0.6f)));

		// And running water darkens a little where it lies.
		Colour = Graph.Lerp(Colour, Graph.Multiply(Colour, Graph.Constant(0.8f)), Graph.Multiply(Wet, Graph.Constant(0.3f)));

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->EmissiveColor.Expression = Colour;
		Material->PostEditChange();
		return Material;
	}
}

#endif
