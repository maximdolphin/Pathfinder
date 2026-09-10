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
#include "Misc/CommandLine.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionCloudLayer.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
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

		// **Declared usable on a volumetric cloud, and this is not optional.**
		//
		// Unreal validates material usage per consumer and silently swaps in the
		// default material for anything that has not declared itself. This is
		// the same trap that rendered three hundred and forty-two instanced
		// trees as black cut-outs in M02 with the material assigned and the
		// slot present -- the thing being drawn was not this material at all.
		// Here it presents as a sky with no cloud in it, which reads as a
		// density bug and is not one.
		Material->bUsedWithVolumetricCloud = true;

		// **Additive, because the compiler says so.** A volume material in any
		// other blend mode fails to compile outright and the default material
		// is substituted, which draws nothing -- and a sky with no cloud in it
		// looks exactly like a density that came out zero. Two rounds of this
		// task went on the second explanation. The engine had been saying
		// "Volume materials must use an Additive blend mode" in LogMaterial the
		// whole time, under a warning about the map rather than the material.
		Material->BlendMode = BLEND_Additive;

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
		// **Camera-relative, plus an anchor, because absolute world position has
		// no precision left to give.**
		//
		// This planet's surface is at 6.3e8 centimetres from the origin. A
		// shader float there has an ULP of about sixty-four centimetres, so
		// multiplying by a noise scale and asking for variation across a cloud
		// asks for detail that is not in the number any more -- and what comes
		// back is a flat sheet with a dither on it. Feeding the noise the raw
		// density confirmed it: the whole sky was one value.
		//
		// This is the same large-world precision problem the terrain solved with
		// doubles, arriving in a place where doubles are not available. The way
		// out is to keep the coordinate small and anchor it: camera-relative
		// position is a small float, and an origin parameter carrying the
		// camera's own position modulo a hundred kilometres puts it back in the
		// world. The seam where that modulo wraps is a hundred kilometres away
		// and moves with the viewer, which is a better place for it than
		// everywhere.
		UMaterialExpressionWorldPosition* Position =
			Graph.Make<UMaterialExpressionWorldPosition>();
		Position->WorldPositionShaderOffset = WPT_CameraRelativeNoOffsets;

		UMaterialExpressionVectorParameter* Anchor =
			Graph.Make<UMaterialExpressionVectorParameter>();
		Anchor->ParameterName = TEXT("NoiseOrigin");
		Anchor->DefaultValue = FLinearColor::Black;
		UMaterialExpressionTime* Time = Graph.Make<UMaterialExpressionTime>();

		UMaterialExpression* Drift = Graph.Add(Position, Anchor);
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
			Drift = Graph.Subtract(Drift, Offset);
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

		// **Per centimetre, and that is a factor of a hundred.**
		//
		// A cumulus you can see fifty metres into has an extinction of about
		// 3/50 per metre, which is 0.0006 per centimetre -- and centimetres are
		// what a volume material's opacity output is in. Handing it 0.05
		// per-metre-thinking made every band a hundred times too dense and
		// turned the zenith into a flat purple wall, which reads as a broken
		// shader rather than as an arithmetic slip.
		// **Per centimetre, and a cloud is opaque.**
		//
		// A cumulus you can see fifty metres into is 0.06 per metre, which is
		// 6e-4 per centimetre -- and across a five-hundred-metre deck that is
		// thirty optical depths. That is correct: real clouds are opaque. It
		// also means the difference between 0.05 and 0.0006 here is the
		// difference between utterly opaque and utterly opaque, which is why
		// changing it by a factor of a hundred changed no pixel and ruled
		// nothing out.
		//
		// What makes a sky rather than a lid is the gaps, and the gaps are the
		// coverage threshold on the noise -- not the density.
		UMaterialExpressionScalarParameter* Extinction =
			Parameter(Graph, TEXT("Extinction"), 0.0004f);

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
		// **`-cloudprobe` makes the material show its own coordinate.**
		//
		// A uniform sky can mean the bands are everywhere or that the altitude
		// they are drawn against is constant, and those two look identical from
		// outside. With the probe on, the cloud is coloured by its normalised
		// altitude in the layer and given a flat thin density: a vertical
		// gradient means the coordinate is good and the bands are at fault, and
		// a flat colour means the coordinate is.
		const bool bProbe =
			FParse::Param(FCommandLine::Get(), TEXT("cloudprobe"));
		if (bProbe)
		{
			// **The probe drives density from the noise alone**, with the bands
			// and the altitude taken out of the circuit entirely. If the sky
			// comes back with cloud-shaped holes in it the noise works and the
			// bands are at fault; if it comes back a flat sheet the noise is
			// constant and nothing downstream of it could have helped.
			//
			// An earlier probe put the altitude into the emissive instead and
			// changed nothing visible, which ruled out less than it looked
			// like: an additive volume's emissive is not a reliable readout.
			// Density is, because density is the only thing this material is
			// really for.
			EditorData->EmissiveColor.Expression =
				Graph.Constant3(FLinearColor::Black);
			EditorData->Opacity.Expression = Graph.Multiply(Noise, Extinction);
			UE_LOG(LogLedger, Log,
				TEXT("cloud material: probe on, density is the raw noise"));
		}
		else
		{
			EditorData->EmissiveColor.Expression =
				Graph.Constant3(FLinearColor::Black);
		}

		Material->PostEditChange();

		// **Whether it compiled is already in the log**, under LogMaterial, and
		// a second copy here would only be a second thing to keep true. What is
		// worth saying is how big the graph is, so a silent sky can be told
		// from an empty one.
		UE_LOG(LogLedger, Log,
			TEXT("cloud material: %d expressions, volumetric cloud usage %s"),
			Material->GetExpressionCollection().Expressions.Num(),
			Material->bUsedWithVolumetricCloud ? TEXT("declared") : TEXT("MISSING"));

		return Material;
	}
}

#endif
