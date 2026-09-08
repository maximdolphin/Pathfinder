// The sea, from above: depth tint, Fresnel, waves and shoreline foam.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"

namespace LedgerSurface
{
	UMaterialInterface* CreateWaterMaterial(UObject* Outer)
	{
		UMaterial* Material = NewObject<UMaterial>(Outer, NAME_None, RF_Transient);
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);
		// The wave normal is derived from world-space gradients. There is no
		// tangent basis anywhere in that derivation and none should be applied
		// to the result.
		Material->bTangentSpaceNormal = false;

		FGraph Graph;
		Graph.Material = Material;

		// Depth comes in on vertex alpha: shallow water over a sandbar is a
		// different colour from open ocean, and that gradient at the shoreline is
		// most of what tells you it is a surface with something under it.
		UMaterialExpressionVertexColor* VertexColour = Graph.Make<UMaterialExpressionVertexColor>();

		UMaterialExpressionConstant3Vector* Shallow = Graph.Make<UMaterialExpressionConstant3Vector>();
		Shallow->Constant = FLinearColor(0.075f, 0.220f, 0.245f);
		UMaterialExpressionConstant3Vector* Deep = Graph.Make<UMaterialExpressionConstant3Vector>();
		Deep->Constant = FLinearColor(0.012f, 0.048f, 0.098f);

		// Depth arrives on vertex alpha, and alpha is a **separate output pin**
		// on the vertex-colour node — pin 0 is float3 RGB with no alpha in it.
		//
		// Masking pin 0 for alpha is what broke this material: it compiled to
		// "not enough components in float3 for component mask 0001", and UE's
		// response to a material that will not compile is one warning line and a
		// silent swap to the default. The sea therefore rendered as grey
		// WorldGridMaterial, and every subsequent change to the water shader
		// appeared to do nothing whatsoever. Worth knowing: a material that does
		// not react to any edit is usually not being used.
		constexpr int32 VertexAlphaPin = 4;

		UMaterialExpressionLinearInterpolate* Body = Graph.Make<UMaterialExpressionLinearInterpolate>();
		Body->A.Expression = Shallow;
		Body->B.Expression = Deep;
		Body->Alpha.Expression = VertexColour;
		Body->Alpha.OutputIndex = VertexAlphaPin;


		// Fresnel: water is nearly a mirror at grazing angles and nearly clear
		// looking straight down. Without it the sea is the same colour to the
		// horizon and reads as painted.
		UMaterialExpressionFresnel* Fresnel = Graph.Make<UMaterialExpressionFresnel>();
		Fresnel->Exponent = 4.0f;
		Fresnel->BaseReflectFraction = 0.02f;

		// At a grazing angle water shows the sky, which near the horizon is bright.
		// The first pass used a dark grey here and the sea came back nearly black
		// from any low viewpoint, which is every viewpoint that matters.
		UMaterialExpressionConstant3Vector* Grazing = Graph.Make<UMaterialExpressionConstant3Vector>();
		Grazing->Constant = FLinearColor(0.42f, 0.52f, 0.62f);
		UMaterialExpression* BaseColour = Graph.Lerp(Body, Grazing, Fresnel);

		// ------------------------------------------------------------- waves
		//
		// Two crossed plane-wave trains, each running along a world axis. Two is
		// enough to stop the sea reading as a single corrugation and is well
		// short of a spectrum; a real ocean wants a handful more, in a texture.
		//
		// Phase is `Frac(x / wavelength)`, not `x / wavelength`. The planet is
		// 6371 km across, so a world coordinate out here is around 6.4e8 and a
		// float has no fractional part left at that magnitude — the sine would
		// return staircase garbage. Frac on an absolute world position is the
		// one operation large-world coordinates keep exact, which is why it
		// exists. It is also why this cannot use the camera-relative position
		// the terrain material uses: relative coordinates move with the camera,
		// and the wave field would swim along with it.
		UMaterialExpressionWorldPosition* WorldPos = Graph.Make<UMaterialExpressionWorldPosition>();
		WorldPos->WorldPositionShaderOffset = WPT_Default;

		// Deep-water phase speed is sqrt(g L / 2pi), so the long swell outruns
		// the chop. Getting this wrong is immediately legible: waves of
		// different lengths travelling at the same speed hold their relative
		// positions, and the sea reads as one scrolling texture, which is what
		// it would then be.
		constexpr float Gravity = 981.0f;              // cm/s^2
		constexpr float LongWavelength = 7000.0f;      // 70 m
		constexpr float LongAmplitude = 55.0f;
		constexpr float ShortWavelength = 3100.0f;     // 31 m
		constexpr float ShortAmplitude = 22.0f;

		const float LongPeriod = LongWavelength / FMath::Sqrt(Gravity * LongWavelength / (2.0f * PI));
		const float ShortPeriod = ShortWavelength / FMath::Sqrt(Gravity * ShortWavelength / (2.0f * PI));

		UMaterialExpressionTime* Time = Graph.Make<UMaterialExpressionTime>();

		// Phase in cycles: the Sine and Cosine nodes have a period of 1, so 1.0
		// here is one whole wave.
		auto MakePhase = [&Graph, WorldPos, Time](bool bAlongX, float Wavelength, float Period) -> UMaterialExpression*
		{
			UMaterialExpressionFrac* Wrapped = Graph.Make<UMaterialExpressionFrac>();
			Wrapped->Input.Expression = Graph.Divide(
				Graph.Mask(WorldPos, bAlongX, !bAlongX, false), Graph.Constant(Wavelength));
			return Graph.Subtract(Wrapped, Graph.Scale(Time, 1.0f / Period));
		};

		UMaterialExpression* LongPhase = MakePhase(true, LongWavelength, LongPeriod);
		UMaterialExpression* ShortPhase = MakePhase(false, ShortWavelength, ShortPeriod);

		auto Sine = [&Graph](UMaterialExpression* Phase) -> UMaterialExpression*
		{
			UMaterialExpressionSine* Node = Graph.Make<UMaterialExpressionSine>();
			Node->Input.Expression = Phase;
			return Node;
		};
		auto Cosine = [&Graph](UMaterialExpression* Phase) -> UMaterialExpression*
		{
			UMaterialExpressionCosine* Node = Graph.Make<UMaterialExpressionCosine>();
			Node->Input.Expression = Phase;
			return Node;
		};

		UMaterialExpression* LongSine = Sine(LongPhase);
		UMaterialExpression* ShortSine = Sine(ShortPhase);

		// Vertex red carries proximity to the shoreline: 1 at the waterline,
		// 0 once the sea floor is 3 m down. Amplitude runs the other way, so
		// the swell flattens into the beach instead of cutting through it.
		UMaterialExpression* Shore = Graph.Mask(VertexColour, true, false, false);
		UMaterialExpression* OpenWater = Graph.OneMinus(Shore);

		// Displacement has to stop before the mesh stops resolving it: a couple
		// of kilometres out a 31 m wave is narrower than the LOD triangles, and
		// sampling it there turns the sea into noise. Fading the geometry and
		// the normal on the same curve keeps the two agreeing all the way out.
		UMaterialExpressionDistance* ToCamera = Graph.Make<UMaterialExpressionDistance>();
		ToCamera->A.Expression = Graph.Make<UMaterialExpressionCameraPositionWS>();
		ToCamera->B.Expression = WorldPos;
		UMaterialExpression* DistanceFade = Graph.OneMinus(Graph.Saturate(
			Graph.Divide(
				Graph.Subtract(ToCamera, Graph.Constant(40000.0f)),
				Graph.Constant(260000.0f))));

		UMaterialExpression* WaveFade = Graph.Multiply(OpenWater, DistanceFade);

		UMaterialExpressionVertexNormalWS* Up = Graph.Make<UMaterialExpressionVertexNormalWS>();

		UMaterialExpression* Height = Graph.Multiply(
			Graph.Add(
				Graph.Scale(LongSine, LongAmplitude),
				Graph.Scale(ShortSine, ShortAmplitude)),
			WaveFade);

		// The normal of a height field displaced along N is N - grad(h), with
		// the radial part of the gradient removed. Each train varies along one
		// world axis only, so the gradient is just the two cosines laid into x
		// and y.
		//
		// The slopes are exaggerated: A*2pi/L comes to about 0.05, which is a
		// correct and completely invisible amount of tilt. Lighting is what
		// sells a wave; the geometry only gives it a silhouette.
		constexpr float NormalStrength = 1.8f;
		UMaterialExpression* SlopeX = Graph.Scale(Cosine(LongPhase),
			NormalStrength * LongAmplitude * 2.0f * PI / LongWavelength);
		UMaterialExpression* SlopeY = Graph.Scale(Cosine(ShortPhase),
			NormalStrength * ShortAmplitude * 2.0f * PI / ShortWavelength);

		UMaterialExpressionAppendVector* SlopeXY = Graph.Make<UMaterialExpressionAppendVector>();
		SlopeXY->A.Expression = SlopeX;
		SlopeXY->B.Expression = SlopeY;
		UMaterialExpressionAppendVector* Gradient = Graph.Make<UMaterialExpressionAppendVector>();
		Gradient->A.Expression = SlopeXY;
		Gradient->B.Expression = Graph.Constant(0.0f);

		UMaterialExpressionDotProduct* Radial = Graph.Make<UMaterialExpressionDotProduct>();
		Radial->A.Expression = Gradient;
		Radial->B.Expression = Up;
		UMaterialExpression* Tangential = Graph.Subtract(Gradient, Graph.Multiply(Up, Radial));

		UMaterialExpressionNormalize* WaveNormal = Graph.Make<UMaterialExpressionNormalize>();
		WaveNormal->VectorInput.Expression =
			Graph.Subtract(Up, Graph.Multiply(Tangential, WaveFade));

		// -------------------------------------------------------------- foam
		//
		// A band hugging the waterline, surging with the long wave so the
		// shoreline advances and retreats instead of sitting still. Keyed off
		// the same shore channel, on a curve rather than a threshold: a hard cut
		// would have to know whether vertex colour arrives sRGB-encoded, and a
		// curve does not care.
		UMaterialExpressionPower* FoamBand = Graph.Make<UMaterialExpressionPower>();
		FoamBand->Base.Expression = Shore;
		FoamBand->Exponent.Expression = Graph.Constant(6.0f);

		UMaterialExpression* Surge = Graph.Saturate(
			Graph.Add(Graph.Constant(0.5f), Graph.Scale(LongSine, 0.85f)));
		UMaterialExpression* Foam = Graph.Saturate(
			Graph.Scale(Graph.Multiply(FoamBand, Surge), 1.3f));

		UMaterialExpressionConstant3Vector* FoamColour = Graph.Make<UMaterialExpressionConstant3Vector>();
		FoamColour->Constant = FLinearColor(0.82f, 0.87f, 0.90f);
		BaseColour = Graph.Lerp(BaseColour, FoamColour, Foam);

		// Roughness matters more than it looks.
		//
		// At 0.012 the surface is a mirror, and a mirror renders black unless
		// something is in front of it: screen-space reflection can only return
		// what is already on screen, and looking out to sea that is mostly sky
		// it cannot sample. Around 0.09 the sea picks up the sky light's ambient
		// specular instead, which is what gives it colour at all, and still sits
		// under the SSR roughness cutoff so real reflections work where they can.
		UMaterialExpressionLinearInterpolate* Roughness = Graph.Make<UMaterialExpressionLinearInterpolate>();
		Roughness->A.Expression = Graph.Constant(0.11f);
		Roughness->B.Expression = Graph.Constant(0.07f);
		Roughness->Alpha.Expression = VertexColour;
		Roughness->Alpha.OutputIndex = VertexAlphaPin;

		// Foam is aerated water, and it is the one part of the sea that is not
		// shiny. Left smooth it comes out as a white mirror.
		UMaterialExpression* SurfaceRoughness = Graph.Lerp(Roughness, Graph.Constant(0.62f), Foam);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->BaseColor.Expression = BaseColour;
		EditorData->Roughness.Expression = SurfaceRoughness;
		EditorData->Specular.Expression = Graph.Constant(0.85f);
		EditorData->Metallic.Expression = Graph.Constant(0.0f);
		EditorData->Normal.Expression = WaveNormal;
		EditorData->WorldPositionOffset.Expression = Graph.Multiply(Up, Height);

		Material->PostEditChange();
		return Material;
	}
}

#endif
