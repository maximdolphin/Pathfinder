// The ground: three biome surface scans and a rock, triplanar, interlocked by
// height rather than cross-faded.
//
// The three ground slots are *parameters*. Which three a patch is made of comes
// from its biomes (T053), the weights come down the vertex colour, and a
// dynamic instance per palette binds the textures. One material, one shader,
// any three grounds.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerLog.h"
#include "LedgerMaterialGraph.h"
#include "LedgerSurfaceSets.h"
#include "MaterialDomain.h"
#include "Misc/CommandLine.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
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
	namespace
	{
		// What the ground slots hold when nobody has bound a palette.
		//
		// A default, not a choice: the biome that owns a patch supplies the
		// real texture through the instance. This is what a fresh clone with no
		// biome files, or the editor viewport before the planet has streamed,
		// draws with -- and it is deliberately a plausible ground rather than a
		// checkerboard, because it is also the fallback if binding ever fails.
		const TCHAR* DefaultSurface = TEXT("grassy_soil_xbklfix");

		// Rock is not a biome. It is what any biome becomes on a face too steep
		// to hold anything, so it is fixed in the material rather than bound
		// per patch -- and it is why there are three ground slots and not four.
		const TCHAR* SteepSurface = TEXT("rock_cliff_xbknedb");

		// What falls off a cliff and piles up under it.
		//
		// Not a biome either, and not a third thing decided by hand: scree sits
		// at the angle of repose, which is a slope, so it is the band between
		// ground too steep to hold soil and rock too steep to hold anything.
		// The debris under a face is derived from the same number that makes
		// the face bare (T054).
		const TCHAR* ScreeSurface = TEXT("gravel_ground_vi0maebg");

		// Snow would be neither: it lies on top of whichever ground is there,
		// so it wants to be an overlay on the finished blend rather than a
		// fourth competitor in it, and it has a channel waiting for it in the
		// vertex colour's alpha (T060). The set it would use is
		// fresh_windswept_snow_ugspafgdy. See the note further down for why the
		// blend is not here.

		/// The three parameterised ground slots.
		constexpr int32 GroundSlots = 3;

		/// The angle of repose, as the cosine of the angle from straight up.
		///
		/// Loose rock will not stand steeper than about 34 degrees; above that
		/// it slides, and what is left is the face it slid off. cos(34) = 0.83,
		/// and scree is strongest just under it and gone by the time the ground
		/// is bare rock.
		constexpr float ReposeCosine = 0.83f;

		// Where rock takes over from soil, as the cosine of the angle between
		// the surface normal and straight up. Soil holds to about 26 degrees;
		// by 49 it is bare rock. Anything steeper than that does not keep a
		// soil layer in the first place, which is why this is slope-driven
		// rather than painted.
		constexpr float SoilHoldsTo = 0.90f;
		constexpr float RockTakesOver = 0.65f;

		// How sharply the two interlock. This is the depth over which the two
		// height maps compete: small values make a hard, gravelly boundary,
		// large ones soften back towards a cross-fade. 0.2 keeps pebbles
		// sitting proud of the soil rather than dissolving into it.
		constexpr float BlendDepth = 0.2f;

		/// Everything one surface set contributes, sampled once.
		struct FSampled
		{
			UMaterialExpression* Albedo = nullptr;
			UMaterialExpression* Normal = nullptr;
			UMaterialExpression* Height = nullptr;
			UMaterialExpression* Roughness = nullptr;
			UMaterialExpression* Occlusion = nullptr;
		};

		/// A ground slot's parameter names. One place, so the material and the
		/// instance that binds it cannot disagree about spelling.
		FString SlotParameter(int32 Slot, const TCHAR* Suffix)
		{
			return FString::Printf(TEXT("Ground%d%s"), Slot, Suffix);
		}

		/// One parameterised ground slot, sampled.
		///
		/// Tiling is a parameter too: these scans are not all the same size on
		/// the ground -- half a metre for forest floor, two for most -- and a
		/// set tiled at the wrong distance reads as a pattern rather than as
		/// ground. Baking one constant in would make the slot only correct for
		/// whichever set it was baked for.
		FSampled SampleSlot(
			FGraph& Graph,
			int32 Slot,
			const FSurfaceSet& Default,
			UMaterialExpression* WorldPosition,
			UMaterialExpression* WeightX,
			UMaterialExpression* WeightY,
			UMaterialExpression* WeightZ)
		{
			UMaterialExpression* Position = Graph.Multiply(WorldPosition,
				Graph.ScalarParameter(*SlotParameter(Slot, TEXT("Tiling")),
					1.0f / static_cast<float>(Default.TilingMetres * 100.0)));

			FSampled Out;
			Out.Albedo = Graph.TriplanarParameter(*SlotParameter(Slot, TEXT("Albedo")),
				Default.Albedo, Position, WeightX, WeightY, WeightZ, SAMPLERTYPE_Color);
			Out.Normal = Graph.TriplanarNormalParameter(*SlotParameter(Slot, TEXT("Normal")),
				Default.Normal, Position, WeightX, WeightY, WeightZ);

			UMaterialExpression* Packed = Graph.TriplanarParameter(
				*SlotParameter(Slot, TEXT("Packed")),
				Default.Packed, Position, WeightX, WeightY, WeightZ, SAMPLERTYPE_Masks);
			Out.Occlusion = Graph.Mask(Packed, true, false, false);
			Out.Roughness = Graph.Mask(Packed, false, true, false);
			Out.Height = Graph.Mask(Packed, false, false, true);
			return Out;
		}

		FSampled SampleSet(
			FGraph& Graph,
			const FSurfaceSet& Set,
			UMaterialExpression* WorldPosition,
			UMaterialExpression* WeightX,
			UMaterialExpression* WeightY,
			UMaterialExpression* WeightZ)
		{
			// Tiling distance comes from the scan's own metadata, in metres;
			// Unreal is in centimetres.
			const float Scale = 1.0f / static_cast<float>(Set.TilingMetres * 100.0);
			UMaterialExpression* Position = Graph.Multiply(WorldPosition, Graph.Constant(Scale));

			FSampled Out;
			Out.Albedo = Graph.Triplanar(
				Set.Albedo, Position, WeightX, WeightY, WeightZ, SAMPLERTYPE_Color);
			// A set rather than a slot, so no parameter name -- but the same
			// blend, because the arithmetic does not care who owns the texture.
			Out.Normal = Graph.TriplanarNormalParameter(
				*FString::Printf(TEXT("%sNormal"), *Set.Name),
				Set.Normal, Position, WeightX, WeightY, WeightZ);

			// One sample, three masks: ambient occlusion in red, roughness in
			// green, height in blue. Separately they would be three triplanar
			// samples each, which is nine more per surface per pixel.
			UMaterialExpression* Packed = Graph.Triplanar(
				Set.Packed, Position, WeightX, WeightY, WeightZ, SAMPLERTYPE_Masks);
			Out.Occlusion = Graph.Mask(Packed, true, false, false);
			Out.Roughness = Graph.Mask(Packed, false, true, false);
			Out.Height = Graph.Mask(Packed, false, false, true);
			return Out;
		}
	}

	UMaterialInterface* BuildTerrainMaterial(UObject* Outer, uint32 Seed)
	{
		const FSurfaceSet Flat = LoadSurfaceSet(DefaultSurface);
		const FSurfaceSet Steep = LoadSurfaceSet(SteepSurface);
		const FSurfaceSet Scree = LoadSurfaceSet(ScreeSurface);
		if (!Flat.IsValid() || !Steep.IsValid() || !Scree.IsValid())
		{
			// Loudly, and with nothing returned. A terrain material that
			// silently falls back to something plausible is how this project
			// once drew WorldGridMaterial across an entire ocean for a task.
			UE_LOG(LogLedger, Error,
				TEXT("terrain material: surface sets missing, refusing to build a fallback"));
			return nullptr;
		}

		UMaterial* Material = NewMaterial(Outer, TEXT("M_Terrain"));
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);

		// World space, because the triplanar blend produces one. See
		// FGraph::TriplanarNormalParameter for why it has to.
		Material->bTangentSpaceNormal = false;
		Material->TwoSided = false;

		FGraph Graph;
		Graph.Material = Material;

		// Camera-relative, not absolute: an absolute position on a 6.37e8 cm
		// planet spends all of its float precision on the exponent, and the
		// projection degenerates into banding. Camera-relative keeps full
		// precision exactly where the detail is visible.
		UMaterialExpressionWorldPosition* WorldPosition = Graph.Make<UMaterialExpressionWorldPosition>();
		WorldPosition->WorldPositionShaderOffset = WPT_CameraRelativeNoOffsets;

		UMaterialExpressionWorldPosition* AbsolutePosition =
			Graph.Make<UMaterialExpressionWorldPosition>();
		AbsolutePosition->WorldPositionShaderOffset = WPT_Default;

		// The planet's centre is a parameter rather than the world origin: the
		// two coincide today and will not once there is a second body.
		UMaterialExpressionVectorParameter* PlanetCentre =
			Graph.Make<UMaterialExpressionVectorParameter>();
		PlanetCentre->ParameterName = TEXT("PlanetCentre");
		PlanetCentre->DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

		UMaterialExpressionNormalize* Radial = Graph.Make<UMaterialExpressionNormalize>();
		Radial->VectorInput.Expression = Graph.Subtract(
			AbsolutePosition, Graph.Mask(PlanetCentre, true, true, true));

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

		// ---- parallax: the ground gets to have a thickness -----------------
		//
		// Every scan ships a height map. Until now all three were sampled and
		// used only to decide which layer won a blend -- nothing was ever
		// displaced, so the surface was a photograph of gravel printed on
		// glass. A normal map relights a flat surface; it never moves it, so
		// nothing slides against anything as the camera does, and there is no
		// depth cue at all at the distance a person actually stands.
		//
		// **Offset in world space, not in UV space.** The ground is triplanar:
		// three projections of the same position, blended. Offsetting a UV
		// would move one projection and leave the others where they were, and
		// the seams between them would swim. Displacing the *position* every
		// projection is derived from moves all three by the same amount, in the
		// same direction, and the blend stays put.
		//
		// One step, not a ray march. This is parallax offset mapping and not
		// parallax occlusion mapping, and the difference is honest: it makes
		// the surface parallax correctly under a moving camera and it does not
		// make a pebble occlude the pebble behind it. Occlusion needs a loop
		// per pixel, which is a rung further up and is measured before it is
		// taken. One extra triplanar sample buys the first thing; the second
		// costs a marched loop and is not free at a thousand pixels of ground.
		UMaterialExpression* ProbePosition = Graph.Multiply(WorldPosition,
			Graph.ScalarParameter(*SlotParameter(0, TEXT("Tiling")),
				1.0f / static_cast<float>(Flat.TilingMetres * 100.0)));
		UMaterialExpression* ProbeHeight = Graph.Mask(
			Graph.TriplanarParameter(*SlotParameter(0, TEXT("Packed")),
				Flat.Packed, ProbePosition, WeightX, WeightY, WeightZ, SAMPLERTYPE_Masks),
			false, false, true);

		// From the surface towards the eye.
		UMaterialExpressionCameraVectorWS* EyeDirection =
			Graph.Make<UMaterialExpressionCameraVectorWS>();

		// Only the part lying in the surface. The component along the normal
		// does not shift anything -- looking straight down at ground, there is
		// no parallax to have, which is exactly what this arithmetic says.
		UMaterialExpressionDotProduct* FacingDot = Graph.Make<UMaterialExpressionDotProduct>();
		FacingDot->A.Expression = EyeDirection;
		FacingDot->B.Expression = Normal;
		UMaterialExpression* Tangential = Graph.Subtract(
			EyeDirection, Graph.Multiply(Normal, FacingDot));

		// Divided by how square-on the surface is, floored so a grazing angle
		// does not divide by nothing and throw the sample across the texture.
		// Grazing is where parallax is largest in reality too.
		UMaterialExpression* Facing = Graph.Max(FacingDot, Graph.Constant(0.30f));

		// Height is 0..1 out of the scan; centred so the mean surface stays
		// where the mesh put it and only the relief moves.
		UMaterialExpressionScalarParameter* Depth3D =
			Graph.Make<UMaterialExpressionScalarParameter>();
		Depth3D->ParameterName = TEXT("ParallaxDepth");
		// Centimetres. Three is about what a gravel scan's height map covers,
		// and past five the offset outruns the sample it was measured from and
		// the surface starts to smear.
		// `-noparallax` sets it to nothing, which is the control arm: a
		// difference between two runs is only attributable to parallax if the
		// other run is the same build with parallax switched off.
		Depth3D->DefaultValue =
			FParse::Param(FCommandLine::Get(), TEXT("noparallax")) ? 0.0f : 3.0f;

		// Off beyond fifteen metres. A three-centimetre displacement is under a
		// pixel by then, and the sample it costs is not.
		UMaterialExpressionSaturate* ParallaxFade = Graph.Make<UMaterialExpressionSaturate>();
		ParallaxFade->Input.Expression = Graph.Divide(
			Graph.Subtract(Graph.Make<UMaterialExpressionPixelDepth>(), Graph.Constant(300.0f)),
			Graph.Constant(1200.0f));

		UMaterialExpression* ParallaxAmount = Graph.Multiply(
			Graph.Multiply(Graph.Subtract(ProbeHeight, Graph.Constant(0.5f)), Depth3D),
			Graph.OneMinus(ParallaxFade));

		UMaterialExpression* ParallaxPosition = Graph.Subtract(WorldPosition,
			Graph.Multiply(Graph.Divide(Tangential, Facing), ParallaxAmount));

		const FSampled Rock = SampleSet(Graph, Steep, ParallaxPosition, WeightX, WeightY, WeightZ);
		const FSampled ScreeSampled =
			SampleSet(Graph, Scree, ParallaxPosition, WeightX, WeightY, WeightZ);

		// ---- three grounds, weighted by the mesh --------------------------
		//
		// Vertex colour RGB is the weight of each ground slot at this vertex,
		// summing to one. The patch generator wrote them from the climate
		// field, so the boundary between two grounds is the boundary between
		// two biomes and nothing else decides it.
		//
		// Height blending across all three at once, not two nested lerps: a
		// nested pair makes the second boundary a blend of a blend, so the same
		// two biomes meeting read differently depending on which slot they
		// landed in. Every slot bids its own height plus its own weight and the
		// proudest texel within BlendDepth wins, which is symmetric in the
		// three by construction.
		UMaterialExpressionVertexColor* VertexColour = Graph.Make<UMaterialExpressionVertexColor>();
		UMaterialExpression* SlotWeight[GroundSlots] = {
			Graph.Mask(VertexColour, true, false, false),
			Graph.Mask(VertexColour, false, true, false),
			Graph.Mask(VertexColour, false, false, true),
		};

		FSampled Slot[GroundSlots];
		UMaterialExpression* SlotMean[GroundSlots] = {};
		UMaterialExpression* SlotTint[GroundSlots] = {};
		UMaterialExpression* SlotBid[GroundSlots] = {};
		for (int32 Index = 0; Index < GroundSlots; ++Index)
		{
			Slot[Index] = SampleSlot(Graph, Index, Flat, ParallaxPosition, WeightX, WeightY, WeightZ);
			SlotMean[Index] = Graph.VectorParameter(
				*SlotParameter(Index, TEXT("Mean")), Flat.MeanAlbedo);
			SlotTint[Index] = Graph.VectorParameter(
				*SlotParameter(Index, TEXT("Tint")), FLinearColor::White);
			SlotBid[Index] = Graph.Add(Slot[Index].Height, SlotWeight[Index]);
		}

		UMaterialExpression* GroundThreshold = Graph.Subtract(
			Graph.Max(SlotBid[0], Graph.Max(SlotBid[1], SlotBid[2])), Graph.Constant(BlendDepth));

		UMaterialExpression* SlotShare[GroundSlots] = {};
		UMaterialExpression* ShareSum = Graph.Constant(0.0001f);
		for (int32 Index = 0; Index < GroundSlots; ++Index)
		{
			// A slot with no weight must contribute nothing even if its height
			// map happens to be proud here, or an unused third slot would show
			// through the two that are actually on this patch.
			SlotShare[Index] = Graph.Multiply(
				Graph.Saturate(Graph.Subtract(SlotBid[Index], GroundThreshold)),
				SlotWeight[Index]);
			ShareSum = Graph.Add(ShareSum, SlotShare[Index]);
		}

		auto MixSlots = [&Graph, &SlotShare, ShareSum](
			UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* C)
		{
			return Graph.Divide(
				Graph.Add(Graph.Add(
					Graph.Multiply(A, SlotShare[0]),
					Graph.Multiply(B, SlotShare[1])),
					Graph.Multiply(C, SlotShare[2])),
				ShareSum);
		};

		FSampled Soil;
		Soil.Albedo = MixSlots(Slot[0].Albedo, Slot[1].Albedo, Slot[2].Albedo);
		Soil.Normal = MixSlots(Slot[0].Normal, Slot[1].Normal, Slot[2].Normal);
		Soil.Roughness = MixSlots(Slot[0].Roughness, Slot[1].Roughness, Slot[2].Roughness);
		Soil.Occlusion = MixSlots(Slot[0].Occlusion, Slot[1].Occlusion, Slot[2].Occlusion);
		Soil.Height = MixSlots(Slot[0].Height, Slot[1].Height, Slot[2].Height);

		UMaterialExpression* SoilMean = MixSlots(SlotMean[0], SlotMean[1], SlotMean[2]);
		UMaterialExpression* SoilTint = MixSlots(SlotTint[0], SlotTint[1], SlotTint[2]);

		// ---- which surface, and where the two meet -------------------------
		//
		// Slope decides: 1 where the ground faces straight out from the planet,
		// 0 on a vertical face.
		UMaterialExpressionDotProduct* Slope = Graph.Make<UMaterialExpressionDotProduct>();
		Slope->A.Expression = Normal;
		Slope->B.Expression = Radial;

		UMaterialExpression* RockWeight = Graph.Saturate(
			Graph.Divide(
				Graph.Subtract(Graph.Constant(SoilHoldsTo), Slope),
				Graph.Constant(SoilHoldsTo - RockTakesOver)));

		// ---- scree, at the angle of repose ---------------------------------
		//
		// A band, not a threshold: strongest just under the angle loose rock
		// can stand at, gone above it where there is nothing left to hold and
		// gone below it where the soil has won. That is a triangle in slope,
		// built from the two edges rather than from a curve nobody can read.
		UMaterialExpression* ScreeRising = Graph.Saturate(Graph.Divide(
			Graph.Subtract(Graph.Constant(SoilHoldsTo), Slope),
			Graph.Constant(SoilHoldsTo - ReposeCosine)));
		UMaterialExpression* ScreeFalling = Graph.Saturate(Graph.Divide(
			Graph.Subtract(Slope, Graph.Constant(RockTakesOver)),
			Graph.Constant(ReposeCosine - RockTakesOver)));
		UMaterialExpression* ScreeWeight = Graph.Multiply(ScreeRising, ScreeFalling);

		// Height blending, not alpha blending. Each surface's own height map is
		// added to its weight, and only the material that is *proud* at this
		// texel wins. That is what makes gravel sit in the gaps between grass
		// rather than the two dissolving through each other — a cross-fade
		// makes a region that is 50% of both, which is a material that exists
		// nowhere in the world.
		UMaterialExpression* SoilBid = Graph.Add(Soil.Height, Graph.OneMinus(RockWeight));
		UMaterialExpression* RockBid = Graph.Add(Rock.Height, RockWeight);

		UMaterialExpression* Threshold = Graph.Subtract(
			Graph.Max(SoilBid, RockBid), Graph.Constant(BlendDepth));

		// Saturate rather than max-with-zero: both bids are within BlendDepth
		// of the threshold or below it, so nothing reaches 1 and the clamp at
		// the top never fires.
		UMaterialExpression* SoilShare = Graph.Saturate(Graph.Subtract(SoilBid, Threshold));
		UMaterialExpression* RockShare = Graph.Saturate(Graph.Subtract(RockBid, Threshold));

		UMaterialExpression* Blend = Graph.Divide(
			RockShare,
			Graph.Add(Graph.Add(SoilShare, RockShare), Graph.Constant(0.0001f)));

		// ---- bedding ------------------------------------------------------
		//
		// Strata are horizontal, so they are a function of altitude and of
		// nothing else -- which on a sphere means distance from the centre, not
		// world Z. Sampling the rock scan a second time at a scale stretched
		// flat would cost nine more samples for a band pattern; modulating what
		// is already sampled by a function of altitude costs none.
		//
		// Two frequencies, forty metres and nine, so the beds group into
		// courses instead of reading as corduroy.
		UMaterialExpression* AltitudeAbove = Graph.Distance(
			AbsolutePosition, Graph.Mask(PlanetCentre, true, true, true));
		UMaterialExpression* Bedding = Graph.Add(
			Graph.Multiply(Graph.Sine(Graph.Multiply(AltitudeAbove,
				Graph.Constant(1.0f / 4000.0f))), Graph.Constant(0.10f)),
			Graph.Multiply(Graph.Sine(Graph.Multiply(AltitudeAbove,
				Graph.Constant(1.0f / 900.0f))), Graph.Constant(0.05f)));

		// Only where the rock is bare. Bedding through a meadow is a bug.
		UMaterialExpression* BeddedRock = Graph.Multiply(Rock.Albedo,
			Graph.Add(Graph.Constant(1.0f), Graph.Multiply(Bedding, RockWeight)));

		UMaterialExpression* AlbedoMix = Graph.Lerp(
			Graph.Lerp(Soil.Albedo, ScreeSampled.Albedo, ScreeWeight), BeddedRock, Blend);
		UMaterialExpression* NormalMix = Graph.Lerp(
			Graph.Lerp(Soil.Normal, ScreeSampled.Normal, ScreeWeight), Rock.Normal, Blend);
		UMaterialExpression* RoughMix = Graph.Lerp(
			Graph.Lerp(Soil.Roughness, ScreeSampled.Roughness, ScreeWeight),
			Rock.Roughness, Blend);
		UMaterialExpression* OcclusionMix = Graph.Lerp(
			Graph.Lerp(Soil.Occlusion, ScreeSampled.Occlusion, ScreeWeight),
			Rock.Occlusion, Blend);

		// ---- snow: the field is here, the blend is not --------------------
		//
		// The vertex colour's alpha carries snow cover (T060), the climate
		// function that produces it is tested, and the blend that would put it
		// on screen is NOT in this material.
		//
		// It was, for one evening. Height-blending a snow set over the finished
		// ground changed every terrain capture on the planet -- including at
		// season zero, where the climate is identical to before it and nothing
		// should have moved at all. That is a bug in the blend rather than the
		// snow, and shipping it would have meant every measurement in this
		// repository being taken against a look nobody chose. The channel and
		// the field stay; the blend comes back when it is understood.

		// ---- colour ---------------------------------------------------------
		//
		// Vertex colour still carries the biome: the planet is one material and
		// the climate bands run from beach through grass and scrub to rock and
		// snow. So the scan contributes *variation*, not colour — divide its
		// own average out first, or a green scan under a green tint is a swamp
		// and a sand scan under a snow tint is dirty snow. The averages were
		// measured off the images at import and live in the manifest.
		UMaterialExpression* MeanMix = Graph.Lerp(
			Graph.Lerp(SoilMean, Graph.Constant3(Scree.MeanAlbedo), ScreeWeight),
			Graph.Constant3(Steep.MeanAlbedo), Blend);
		UMaterialExpression* Variation = Graph.Divide(AlbedoMix, MeanMix);

		// Rock has no biome and takes no tint, so the ground's tint fades out
		// with it. Otherwise a cliff in a rainforest would be green rock.
		UMaterialExpression* TintMix = Graph.Lerp(
			Graph.Lerp(SoilTint, Graph.Constant3(FLinearColor(0.30f, 0.29f, 0.27f)),
				ScreeWeight),
			Graph.Constant(1.0f), Blend);

		// Macro breakup. One tile of ground is two metres; from a kilometre up,
		// two metres is a pixel and the repeat becomes a visible grid. A second
		// sample of the same scan at ninety metres, also mean-normalised,
		// modulates it at a scale the eye reads as terrain rather than tiling.
		UMaterialExpression* MacroPosition =
			Graph.Multiply(WorldPosition, Graph.Constant(1.0f / 9000.0f));
		UMaterialExpression* Macro = Graph.Divide(
			Graph.TriplanarParameter(*SlotParameter(0, TEXT("Albedo")), Flat.Albedo,
				MacroPosition, WeightX, WeightY, WeightZ, SAMPLERTYPE_Color),
			SlotMean[0]);

		// And again at 1.2 km, because ninety metres is itself a tile once the
		// camera is high enough, and a repeat at any scale is the tell. The two
		// are coprime enough that their product does not beat: the visible
		// period is the least common multiple, which is far past the horizon.
		UMaterialExpression* MacroFarPosition =
			Graph.Multiply(WorldPosition, Graph.Constant(1.0f / 120000.0f));
		UMaterialExpression* MacroFar = Graph.Divide(
			Graph.TriplanarParameter(*SlotParameter(0, TEXT("Albedo")), Flat.Albedo,
				MacroFarPosition, WeightX, WeightY, WeightZ, SAMPLERTYPE_Color),
			SlotMean[0]);

		// Distance fade, from the distance the pattern actually stops
		// resolving. T432.
		//
		// This was `saturate(Depth / 600 m)`, a ramp that begins at the camera.
		// It reached a quarter at 150 m and a half at 300 m, so the ground
		// three hundred metres away was drawn with half its detail already
		// deleted -- which is what the mid-ground wash in the eye-height
		// captures was. Nothing about a mip needs that.
		//
		// The criterion is when a tile is about two pixels across, which is
		// arithmetic rather than taste. A two-metre tile at distance D subtends
		// 2/D radians; on a 1920 px viewport at 90 degrees that is (2/D) x 960
		// pixels, so two pixels happens at 960 m. Held to 400 m, gone by
		// 1.2 km, which brackets it.
		UMaterialExpressionPixelDepth* Depth = Graph.Make<UMaterialExpressionPixelDepth>();
		UMaterialExpressionSaturate* Fade = Graph.Make<UMaterialExpressionSaturate>();
		Fade->Input.Expression = Graph.Divide(
			Graph.Subtract(Depth, Graph.Constant(40000.0f)),   // held to 400 m
			Graph.Constant(80000.0f));                          // gone by 1.2 km

		UMaterialExpression* FadedVariation = Graph.Lerp(Variation, Graph.Constant(1.0f), Fade);

		// The ninety-metre macro does not fade with the detail, and used to.
		//
		// Same arithmetic as above, applied honestly: ninety metres is two
		// pixels across at forty-three kilometres, so within any distance this
		// planet draws ground at, it is never near its mip range. It was being
		// faded out over the same 600 m as the two-metre band -- so the moment
		// the fine detail went, the thing that was supposed to replace it went
		// with it, and the mid-ground collapsed to a flat tint. It fades over
		// its own distance, far past where anything else matters.
		UMaterialExpressionSaturate* MacroFade = Graph.Make<UMaterialExpressionSaturate>();
		MacroFade->Input.Expression = Graph.Divide(
			Graph.Subtract(Depth, Graph.Constant(2000000.0f)),  // held to 20 km
			Graph.Constant(4000000.0f));                        // gone by 60 km
		UMaterialExpression* FadedMacro = Graph.Lerp(Macro, Graph.Constant(1.0f), MacroFade);

		// Saturated, because three mean-one multipliers stacked on a tint have
		// a tail that goes above one and albedo above one is not a colour, it
		// is a light source. This clamps rather than rescales on purpose: if it
		// is doing real work the ground is blowing out and the tints are wrong,
		// which is a thing to fix in the data, not to hide with a divide.
		UMaterialExpression* BaseColour = Graph.Saturate(Graph.Multiply(
			Graph.Multiply(Graph.Multiply(TintMix, FadedVariation), FadedMacro), MacroFar));

		// Normal, faded to flat over the same distance. A normal map that
		// survives past its mip range is the other half of the shimmer.
		// Faded to the *vertex* normal, not to +Z. In world space "flat" means
		// the surface the mesh describes, and on a sphere +Z is flat in exactly
		// one place.
		UMaterialExpression* FadedNormal = Graph.Lerp(NormalMix, Normal, Fade);

		// Roughness and occlusion fade to their far-field values too: at a
		// kilometre the per-texel variation is below a pixel, and holding it
		// only buys sparkle.
		UMaterialExpression* FadedRough = Graph.Lerp(RoughMix, Graph.Constant(0.93f), Fade);
		UMaterialExpression* FadedOcclusion = Graph.Lerp(OcclusionMix, Graph.Constant(1.0f), Fade);

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

		EditorData->WorldPositionOffset.Expression =
			Graph.Multiply(Radial, Graph.Multiply(MorphDelta, MorphFactor));

		// `-rawsurface` puts the sampled scan straight into base colour: no biome
		// tint, no mean division, no macro, no fade. If the ground still reads
		// as a flat wash under that, the problem is the sampling and not any of
		// the things layered on top of it — which is a question worth being
		// able to answer in one run rather than by reasoning about six
		// multiplications.
		const bool bRaw = FParse::Param(FCommandLine::Get(), TEXT("rawsurface"));

		// `-surfaceuv` paints the texture coordinate itself. If that comes back
		// as a flat colour instead of a repeating ramp, the sampling has no
		// coordinate to work with and nothing layered on top of it can matter.
		// A picture of the input beats another round of reasoning about the
		// output.
		UMaterialExpression* Debug = AlbedoMix;
		if (FParse::Param(FCommandLine::Get(), TEXT("surfaceuv")))
		{
			const float Scale = 1.0f / static_cast<float>(Flat.TilingMetres * 100.0);
			Debug = Graph.Frac(Graph.Mask(
				Graph.Multiply(WorldPosition, Graph.Constant(Scale)), true, true, false));
		}
		EditorData->BaseColor.Expression = bRaw ? Debug : BaseColour;
		EditorData->Normal.Expression = bRaw ? NormalMix : FadedNormal;
		EditorData->Roughness.Expression = FadedRough;
		EditorData->AmbientOcclusion.Expression = FadedOcclusion;
		EditorData->Specular.Expression = Graph.Constant(0.05f);

		// ---- one channel at a time, unlit. T433 --------------------------
		//
		// `-channel=albedo|normal|roughness|ao|height`.
		//
		// Every one of these is a one-line bug that costs the whole surface its
		// realism and none of them announce themselves: AO reaching base colour
		// instead of the AO input, a normal map sampled as sRGB, roughness that
		// was authored as gloss. All five look like "the ground is a bit off".
		//
		// Emissive and unlit, so what is photographed is the channel and not
		// the channel times a sun angle. Routed after every mix and fade, so
		// this shows what the shading model is actually handed rather than what
		// the scan contains -- which is the question, and is why this is here
		// rather than in a texture viewer.
		// ---- the terrain diagnostic, straight through. T066 ---------------
		//
		// The generator has already replaced every vertex colour with whatever
		// -terrainvis was asked for. All this has to do is not shade it: a LOD
		// ramp multiplied by a sun angle is not a LOD ramp.
		// The switch is read here rather than through LedgerTerrainVis, which
		// owns it: this module does not depend on LedgerTerrain and inverting
		// that to share one bool would be a worse trade than naming the switch
		// twice. The modes themselves live in LedgerTerrainVis.h.
		FString VisMode;
		if (FParse::Value(FCommandLine::Get(), TEXT("terrainvis="), VisMode))
		{
			Material->SetShadingModel(MSM_Unlit);
			EditorData->EmissiveColor.Expression =
				Graph.Make<UMaterialExpressionVertexColor>();
			Material->PostEditChange();
			UE_LOG(LogLedger, Log, TEXT("terrain material: -terrainvis, unlit"));
			return Material;
		}

		FString Channel;
		if (FParse::Value(FCommandLine::Get(), TEXT("channel="), Channel))
		{
			UMaterialExpression* Isolated = nullptr;
			if (Channel == TEXT("albedo"))
			{
				Isolated = BaseColour;
			}
			else if (Channel == TEXT("normal"))
			{
				// Remapped from [-1,1] to [0,1] the way a normal map is stored,
				// so a correct one photographs as the familiar lilac.
				Isolated = Graph.Add(
					Graph.Multiply(FadedNormal, Graph.Constant(0.5f)),
					Graph.Constant(0.5f));
			}
			else if (Channel == TEXT("roughness"))
			{
				Isolated = FadedRough;
			}
			else if (Channel == TEXT("ao"))
			{
				Isolated = FadedOcclusion;
			}
			else if (Channel == TEXT("height"))
			{
				Isolated = Soil.Height;
			}

			if (Isolated != nullptr)
			{
				Material->SetShadingModel(MSM_Unlit);
				EditorData->EmissiveColor.Expression = Isolated;
				UE_LOG(LogLedger, Log,
					TEXT("terrain material: isolating %s, unlit"), *Channel);
			}
			else
			{
				UE_LOG(LogLedger, Error,
					TEXT("terrain material: -channel=%s is not one of "
					     "albedo, normal, roughness, ao, height"), *Channel);
			}
		}

		Material->PostEditChange();

		UE_LOG(LogLedger, Log,
			TEXT("terrain material: %s at %.1f m over %s at %.1f m, height-blended"),
			*Flat.Name, Flat.TilingMetres, *Steep.Name, Steep.TilingMetres);
		return Material;
	}
}

#endif
