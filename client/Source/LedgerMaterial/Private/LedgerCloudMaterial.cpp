// Three decks in one volume material. T094.
//
// **Unreal draws one volumetric cloud per scene.** One component, one bottom
// altitude, one height -- so three decks cannot be three components. They have
// to be three bands inside one layer, which means the layer spans from the
// cumulus base to the tropopause and the *material* decides what is at each
// height.
//
// That is the right shape anyway. A deck is not an object; it is a range of
// altitudes where the air is between two temperatures, and a material that
// reads its own altitude is a material that can be told those temperatures.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "LedgerLog.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionCloudLayer.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionWorldPosition.h"

namespace LedgerSurface
{
	namespace
	{
		/// One deck's window in the layer, as a triangle in normalised altitude.
		///
		/// Centre and half-width are parameters rather than constants because
		/// the decks move: a cold day lowers all three, and a body with a
		/// different lapse rate puts them somewhere else entirely.
		struct FBand
		{
			UMaterialExpression* Mask = nullptr;
			UMaterialExpressionScalarParameter* Centre = nullptr;
			UMaterialExpressionScalarParameter* Width = nullptr;
			UMaterialExpressionScalarParameter* Coverage = nullptr;
			UMaterialExpressionScalarParameter* Density = nullptr;
		};

		UMaterialExpression* Abs(FGraph& Graph, UMaterialExpression* Input)
		{
			UMaterialExpressionAbs* Node = Graph.Make<UMaterialExpressionAbs>();
			Node->Input.Expression = Input;
			return Node;
		}

		UMaterialExpression* Saturate(FGraph& Graph, UMaterialExpression* Input)
		{
			UMaterialExpressionSaturate* Node = Graph.Make<UMaterialExpressionSaturate>();
			Node->Input.Expression = Input;
			return Node;
		}

		UMaterialExpressionScalarParameter* Parameter(
			FGraph& Graph, const TCHAR* Name, float Default)
		{
			UMaterialExpressionScalarParameter* Node =
				Graph.Make<UMaterialExpressionScalarParameter>();
			Node->ParameterName = Name;
			Node->DefaultValue = Default;
			return Node;
		}

		/// A deck: how much of this altitude belongs to it, times how much of
		/// the sky it covers, times how solid it is.
		FBand MakeBand(
			FGraph& Graph, UMaterialExpression* Altitude, UMaterialExpression* Noise,
			const TCHAR* Prefix, float Centre, float Width, float Coverage,
			float Density)
		{
			FBand Band;
			Band.Centre = Parameter(Graph,
				*FString::Printf(TEXT("%sCentre"), Prefix), Centre);
			Band.Width = Parameter(Graph,
				*FString::Printf(TEXT("%sWidth"), Prefix), Width);
			Band.Coverage = Parameter(Graph,
				*FString::Printf(TEXT("%sCoverage"), Prefix), Coverage);
			Band.Density = Parameter(Graph,
				*FString::Printf(TEXT("%sDensity"), Prefix), Density);

			// A triangle: one at the centre, zero at a half-width either side.
			// Soft edges matter -- a hard-edged deck has a visible plane at the
			// top and the bottom, which is the thing that makes a cloud layer
			// look like a slab.
			UMaterialExpression* Distance =
				Abs(Graph, Graph.Subtract(Altitude, Band.Centre));
			UMaterialExpression* Inside = Saturate(Graph,
				Graph.Subtract(Graph.Constant(1.0f),
					Graph.Divide(Distance, Band.Width)));

			// **Coverage is a threshold on the noise, not a multiplier on it.**
			// Multiplying thins the whole deck uniformly, which reads as haze;
			// thresholding removes some of it and leaves the rest solid, which
			// reads as broken cloud with sky between. The difference is the
			// whole look of a fair-weather day.
			UMaterialExpression* Threshold =
				Graph.Subtract(Graph.Constant(1.0f), Band.Coverage);
			UMaterialExpression* Shaped = Saturate(Graph,
				Graph.Divide(
					Graph.Subtract(Noise, Threshold),
					Graph.Max(Band.Coverage, Graph.Constant(0.02f))));

			Band.Mask = Graph.Multiply(
				Graph.Multiply(Inside, Shaped), Band.Density);
			return Band;
		}
	}

	UMaterialInterface* BuildCloudMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Clouds"));
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Volume;

		FGraph Graph;
		Graph.Material = Material;

		// **Where in the layer this sample is.** The engine gives it: the node
		// reports the sample's normalised altitude between the layer's bottom
		// and its top, which is exactly the coordinate three stacked decks want
		// and the reason they can share one component.
		UMaterialExpressionCloudSampleAttribute* Sample =
			Graph.Make<UMaterialExpressionCloudSampleAttribute>();

		// The outputs of an external-code node are not something to guess at.
		{
			FString Names;
			for (const FExpressionOutput& Output : Sample->GetOutputs())
			{
				Names += Output.OutputName.ToString() + TEXT(" ");
			}
			UE_LOG(LogLedger, Log,
				TEXT("cloud material: sample attribute outputs are %s"), *Names);
		}

		UMaterialExpressionComponentMask* Altitude =
			Graph.Make<UMaterialExpressionComponentMask>();
		Altitude->Input.Expression = Sample;
		// **Output 2, and the log above is why.** The node offers Altitude,
		// AltitudeInLayer, NormAltitudeInLayer and ShadowSampleDistance in that
		// order; the first two are centimetres and only the third is the 0-to-1
		// coordinate the bands are drawn in. Picking index 1 by its name put
		// every band in the bottom millimetre of the layer.
		Altitude->Input.OutputIndex = 2;
		Altitude->R = true;
		Altitude->G = false;
		Altitude->B = false;

		// **Advected by the wind field.** T093 publishes the wind at the viewer
		// to a collection every tick; the material offsets its noise by that
		// times elapsed time, so the deck moves downwind at the speed the air
		// is actually doing rather than at a scroll rate somebody picked.
		UMaterialExpressionWorldPosition* Position =
			Graph.Make<UMaterialExpressionWorldPosition>();
		UMaterialExpressionTime* Time = Graph.Make<UMaterialExpressionTime>();

		UMaterialExpression* Drift = Position;
		if (UMaterialParameterCollection* Collection =
			LoadObject<UMaterialParameterCollection>(
				nullptr, TEXT("/Game/Materials/MPC_LedgerWind")))
		{
			UMaterialExpressionCollectionParameter* WindDirection =
				Graph.Make<UMaterialExpressionCollectionParameter>();
			WindDirection->Collection = Collection;
			WindDirection->ParameterName = TEXT("WindDirection");

			UMaterialExpressionCollectionParameter* WindSpeed =
				Graph.Make<UMaterialExpressionCollectionParameter>();
			WindSpeed->Collection = Collection;
			WindSpeed->ParameterName = TEXT("WindSpeed");

			// Metres per second into centimetres per second, which is the frame
			// the position is in.
			UMaterialExpression* Offset = Graph.Multiply(
				Graph.Multiply(WindDirection,
					Graph.Multiply(WindSpeed, Graph.Constant(100.0f))),
				Time);
			Drift = Graph.Subtract(Position, Offset);
		}
		else
		{
			UE_LOG(LogLedger, Log,
				TEXT("cloud material: no wind collection, so the decks will not "
					 "drift; every other consumer of the wind is unaffected"));
		}

		UMaterialExpressionScalarParameter* Scale =
			Parameter(Graph, TEXT("NoiseScale"), 0.00004f);
		UMaterialExpressionNoise* Noise = Graph.Make<UMaterialExpressionNoise>();
		Noise->Position.Expression = Graph.Multiply(Drift, Scale);
		Noise->NoiseFunction = NOISEFUNCTION_GradientTex;
		Noise->Scale = 1.0f;
		Noise->Levels = 4;
		Noise->OutputMin = 0.0f;
		Noise->OutputMax = 1.0f;
		// **Not turbulence.** Turbulent noise is the absolute value of a
		// gradient field, so it is biased hard towards zero -- and a coverage
		// threshold at 1 - 0.38 then passes almost nothing, which is a sky with
		// the clouds computed, placed, and invisible. Plain gradient noise is
		// centred on a half, so a threshold at 1 - c passes about c of it,
		// which is what a coverage number is supposed to mean.
		Noise->bTurbulence = false;

		// Three decks, with defaults that are Earth's if nobody sets them.
		// Centres are fractions of the layer: a layer from the cumulus base to
		// the tropopause puts cumulus low, the middle deck in the middle and
		// cirrus near the top.
		const FBand Cumulus = MakeBand(Graph, Altitude, Noise,
			TEXT("Cumulus"), 0.12f, 0.16f, 0.40f, 1.0f);
		const FBand Middle = MakeBand(Graph, Altitude, Noise,
			TEXT("Middle"), 0.45f, 0.14f, 0.20f, 0.55f);
		const FBand Cirrus = MakeBand(Graph, Altitude, Noise,
			TEXT("Cirrus"), 0.86f, 0.12f, 0.35f, 0.18f);

		UMaterialExpression* Total =
			Graph.Add(Graph.Add(Cumulus.Mask, Middle.Mask), Cirrus.Mask);

		UMaterialExpressionScalarParameter* Extinction =
			Parameter(Graph, TEXT("Extinction"), 0.05f);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}

		// **Albedo, and it is not white.** Cloud droplets scatter almost
		// everything they intercept -- a single-scattering albedo of about 0.99
		// -- and what makes a cloud grey underneath is the light having been
		// scattered many times on the way through, not the droplets absorbing
		// it. So this stays high and the renderer does the greying.
		EditorData->BaseColor.Expression = Graph.Constant3(
			FLinearColor(0.98f, 0.98f, 0.99f));
		EditorData->Opacity.Expression = Graph.Multiply(Total, Extinction);
		EditorData->EmissiveColor.Expression =
			Graph.Constant3(FLinearColor::Black);

		Material->PostEditChange();
		return Material;
	}
}

#endif
