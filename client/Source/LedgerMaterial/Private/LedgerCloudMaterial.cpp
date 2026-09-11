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
#include "Materials/MaterialExpressionDotProduct.h"
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


		/// The colour channels of a vector parameter. Vector parameters are four
		/// wide and positions three, and a float3 minus a float4 does not compile
		/// -- which in a volume material silently swaps in the default material,
		/// and the default draws nothing. That was the empty sky after the
		/// density moved to the Extinction pin: the log said "(Node Subtract)
		/// Arithmetic between types float3 and float4 are undefined".
		UMaterialExpression* CloudRGB(FGraph& Graph, UMaterialExpression* Input)
		{
			UMaterialExpressionComponentMask* Mask = Graph.Make<UMaterialExpressionComponentMask>();
			Mask->Input.Expression = Input;
			Mask->R = true;
			Mask->G = true;
			Mask->B = true;
			Mask->A = false;
			return Mask;
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
			UMaterialExpression* Softness, const TCHAR* Prefix, float Centre,
			float Width, float Coverage, float Density)
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

			// **Coverage is a threshold on the noise, and the cut has to be
			// sharp.**
			//
			// Multiplying thins the whole deck uniformly, which reads as haze.
			// Thresholding removes some of it and leaves the rest solid, which
			// reads as broken cloud with sky between -- but only if the ramp
			// between the two is narrow. Dividing by the coverage made a ramp
			// nearly half the noise range wide, so almost the whole volume sat
			// somewhere in the middle; and since a band at full density is
			// twenty optical depths thick, anything above about a twentieth of
			// the way up that ramp is already opaque. The result was a sky that
			// was solid everywhere the threshold had not zeroed outright, which
            // is a fog and not a cloud field.
			//
			// A sixteenth of the range is a cloud edge. Above it, solid; below,
			// sky.
			UMaterialExpression* Threshold =
				Graph.Subtract(Graph.Constant(1.0f), Band.Coverage);
			UMaterialExpression* Shaped = Saturate(Graph,
				Graph.Divide(
					Graph.Subtract(Noise, Threshold), Softness));

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
		Material->SetUsageByFlag(MATUSAGE_VolumetricCloud, true);

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
		// **Camera-relative, and not the "no offsets" variant.** The cloud pass
		// fills WorldPosition_CamRelative but leaves WorldPosition_NoOffsets_CamRelative
		// as a literal TODO (VolumetricCloudMaterialPixelCommon.ush,
		// UpdateMaterialCloudParam), so a noise fed from the no-offsets variant
		// never moved with the sample: once the density reached the right pin,
		// probe and decks alike came back an empty sky.
		Position->WorldPositionShaderOffset = WPT_CameraRelative;

		UMaterialExpressionVectorParameter* Anchor =
			Graph.Make<UMaterialExpressionVectorParameter>();
		Anchor->ParameterName = TEXT("NoiseOrigin");
		Anchor->DefaultValue = FLinearColor::Black;
		UMaterialExpressionTime* Time = Graph.Make<UMaterialExpressionTime>();

		UMaterialExpression* Drift = Graph.Add(Position, CloudRGB(Graph, Anchor));
		if (UMaterialParameterCollection* Collection =
			LoadObject<UMaterialParameterCollection>(
				nullptr, TEXT("/Game/Materials/MPC_LedgerWind")))
		{
			// Same fault as the foliage had, never seen here only because
			// until the collection was baked this branch never ran.
			UMaterialExpression* WindDirection =
				CloudRGB(Graph, Graph.CollectionParameter(Collection, TEXT("WindDirection")));
			UMaterialExpression* WindSpeed =
				Graph.CollectionParameter(Collection, TEXT("WindSpeed"));

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

		// **Flattened, or a horizontal ray averages the sky away.**
		//
		// Noise that varies in all three axes puts an independent value every
		// few hundred metres *vertically* as well as horizontally, so a line of
		// sight along a five-hundred-metre deck crosses dozens of uncorrelated
		// cells and integrates to the mean -- which is a uniform fog however
		// hard the coverage threshold bites. A cloud is a column: the shape is
		// horizontal and the vertical profile is the deck's own.
		//
		// So most of the component along the local vertical is taken out before
		// the lookup. Eight per cent is left, because a deck with no vertical
		// variation at all has a machined top.
		UMaterialExpressionVectorParameter* Up =
			Graph.Make<UMaterialExpressionVectorParameter>();
		Up->ParameterName = TEXT("NoiseUp");
		Up->DefaultValue = FLinearColor(0.0f, 0.0f, 1.0f, 0.0f);
		UMaterialExpression* UpRGB = CloudRGB(Graph, Up);

		UMaterialExpressionDotProduct* Along =
			Graph.Make<UMaterialExpressionDotProduct>();
		Along->A.Expression = Drift;
		Along->B.Expression = UpRGB;
		UMaterialExpression* Flattened = Graph.Subtract(Drift,
			Graph.Multiply(UpRGB, Graph.Scale(Along, 0.92f)));

		// **Cloud-sized, not gravel-sized.** A quarter-kilometre feature reads
		// as noise; a cumulus field is spaced in kilometres.
		UMaterialExpressionScalarParameter* Scale =
			Parameter(Graph, TEXT("NoiseScale"), 0.000012f);
		UMaterialExpressionNoise* Noise = Graph.Make<UMaterialExpressionNoise>();
		Noise->Position.Expression = Graph.Multiply(Flattened, Scale);
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
		UMaterialExpressionScalarParameter* Softness =
			Parameter(Graph, TEXT("EdgeSoftness"), 0.06f);

		const FBand Cumulus = MakeBand(Graph, Altitude, Noise, Softness,
			TEXT("Cumulus"), 0.12f, 0.16f, 0.40f, 1.0f);
		const FBand Middle = MakeBand(Graph, Altitude, Noise, Softness,
			TEXT("Middle"), 0.45f, 0.14f, 0.20f, 0.55f);
		const FBand Cirrus = MakeBand(Graph, Altitude, Noise, Softness,
			TEXT("Cirrus"), 0.86f, 0.12f, 0.35f, 0.18f);

		UMaterialExpression* Total =
			Graph.Add(Graph.Add(Cumulus.Mask, Middle.Mask), Cirrus.Mask);

		// **Per metre, and wired to Extinction -- not Opacity.**
		//
		// A volume material's density is its Extinction pin, which is the
		// SubsurfaceColor property under another name (MP_SubsurfaceColor reads
		// "Extinction" in MD_Volume; see MaterialAttributeDefinitionMap.cpp).
		// Opacity is not read at all. For as long as this material existed the
		// noise, the bands and every coverage threshold went into a pin the
		// renderer ignores, and the extinction was that pin's default of one per
		// metre: seven thousand optical depths across the layer, everywhere.
		// Every uniform sky this task produced was that, and so was every change
		// that "changed no pixel".
		//
		// A cumulus you can see fifty metres into is about 0.06 per metre; 0.04
		// leaves a five-hundred-metre deck twenty optical depths thick, which is
		// opaque, as a cloud is. What makes a sky rather than a lid is the gaps,
		// and the gaps are the coverage threshold on the noise.
		UMaterialExpressionScalarParameter* Extinction =
			Parameter(Graph, TEXT("Extinction"), 0.04f);

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
		EditorData->SubsurfaceColor.Expression = Graph.Multiply(Total, Extinction);
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
			// **Thin enough to see through.** At 3e-4 per metre a full column of
			// the seven-kilometre layer is about two optical depths, so the frame
			// shows the noise itself -- structure if it varies, flat if it does not.
			// `-cloudconst` takes the noise out too: a constant thin extinction. A
			// uniform haze then means the pin reaches the integrator and the noise
			// is what comes out as zero; nothing means the pin does not.
			EditorData->SubsurfaceColor.Expression = FParse::Param(FCommandLine::Get(), TEXT("cloudconst"))
				? Graph.Constant(3.0e-4f)
				: Graph.Scale(Noise, 3.0e-4f);
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
			Material->GetUsageByFlag(MATUSAGE_VolumetricCloud) ? TEXT("declared") : TEXT("MISSING"));

		return Material;
	}
}

#endif
