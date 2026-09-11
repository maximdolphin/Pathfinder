// The aurora: an additive shell at a hundred and ten kilometres, lit in an oval
// around the magnetic pole. T103.
//
// The oval is where LedgerAurora says it is: the pole, the oval's latitude and
// width, and how bright, arrive as parameters. Additive, so it is seen against
// the night and lost in the day, as the real one is; two-sided, so it is the
// same oval from under it and from orbit.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"

namespace LedgerSurface
{
	UMaterialInterface* BuildAuroraMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Aurora"));
		if (Material == nullptr)
		{
			return nullptr;
		}
		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_Unlit);
		Material->BlendMode = BLEND_Additive;
		Material->TwoSided = true;

		FGraph Graph;
		Graph.Material = Material;

		auto Vector = [&Graph](const TCHAR* Name, const FLinearColor& Default)
		{
			UMaterialExpressionVectorParameter* Node = Graph.Make<UMaterialExpressionVectorParameter>();
			Node->ParameterName = Name;
			Node->DefaultValue = Default;
			return Graph.Mask(Node, true, true, true);
		};

		// **Camera-relative, with the planet's centre handed in the same way.**
		// Absolute positions are six hundred million centimetres; this keeps the
		// arithmetic in floats that can hold it.
		UMaterialExpressionWorldPosition* Position = Graph.Make<UMaterialExpressionWorldPosition>();
		Position->WorldPositionShaderOffset = WPT_CameraRelative;
		UMaterialExpressionNormalize* Outward = Graph.Make<UMaterialExpressionNormalize>();
		Outward->VectorInput.Expression = Graph.Subtract(Position,
			Vector(TEXT("PlanetFromCamera"), FLinearColor(0.0f, 0.0f, -1.0e8f, 0.0f)));

		// Magnetic latitude, as its sine: both hemispheres at once.
		UMaterialExpressionDotProduct* Along = Graph.Make<UMaterialExpressionDotProduct>();
		Along->A.Expression = Outward;
		Along->B.Expression = Vector(TEXT("AuroraPole"), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f));
		UMaterialExpressionAbs* SinLatitude = Graph.Make<UMaterialExpressionAbs>();
		SinLatitude->Input.Expression = Along;

		// The oval, in sine of latitude: centre and half-width precomputed from
		// the angles on the CPU, so there is no trigonometry per pixel.
		UMaterialExpression* Off = Graph.Divide(
			Graph.Subtract(SinLatitude, Graph.ScalarParameter(TEXT("AuroraSinOval"), 0.9f)),
			Graph.ScalarParameter(TEXT("AuroraSinWidth"), 0.05f));
		UMaterialExpression* Core = Graph.Saturate(Graph.OneMinus(Graph.Multiply(Off, Off)));
		UMaterialExpression* Band = Graph.Multiply(Core, Core);

		// Curtains: a field that varies fast around the pole and drifts.
		UMaterialExpressionTime* Time = Graph.Make<UMaterialExpressionTime>();
		UMaterialExpressionNoise* Rays = Graph.Make<UMaterialExpressionNoise>();
		Rays->Position.Expression = Graph.Add(Graph.Multiply(Outward, Graph.Constant(260.0f)),
			Graph.Multiply(Time, Graph.Constant(0.03f)));
		Rays->NoiseFunction = NOISEFUNCTION_GradientTex;
		Rays->Scale = 1.0f;
		Rays->Levels = 3;
		Rays->OutputMin = 0.0f;
		Rays->OutputMax = 1.0f;
		Rays->bTurbulence = true;
		UMaterialExpression* Curtain = Graph.Add(Graph.Constant(0.35f), Graph.Multiply(Rays, Graph.Constant(0.65f)));

		// Oxygen's green line, 557.7 nm, which is most of what an aurora is.
		UMaterialExpression* Green = Graph.Constant3(FLinearColor(0.15f, 1.0f, 0.35f));

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->EmissiveColor.Expression = Graph.Multiply(Graph.Multiply(Graph.Multiply(Green, Band), Curtain),
			Graph.Multiply(Graph.ScalarParameter(TEXT("AuroraStrength"), 0.0f),
				Graph.ScalarParameter(TEXT("AuroraBrightness"), 40.0f)));
		Material->PostEditChange();
		return Material;
	}
}

#endif
