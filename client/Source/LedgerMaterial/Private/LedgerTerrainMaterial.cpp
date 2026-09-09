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

		/// The three parameterised ground slots.
		constexpr int32 GroundSlots = 3;

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
			Out.Normal = Graph.TriplanarParameter(*SlotParameter(Slot, TEXT("Normal")),
				Default.Normal, Position, WeightX, WeightY, WeightZ, SAMPLERTYPE_Normal);

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
			Out.Normal = Graph.Triplanar(
				Set.Normal, Position, WeightX, WeightY, WeightZ, SAMPLERTYPE_Normal);

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
		if (!Flat.IsValid() || !Steep.IsValid())
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

		const FSampled Rock = SampleSet(Graph, Steep, WorldPosition, WeightX, WeightY, WeightZ);

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
			Slot[Index] = SampleSlot(Graph, Index, Flat, WorldPosition, WeightX, WeightY, WeightZ);
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

		UMaterialExpression* AlbedoMix = Graph.Lerp(Soil.Albedo, Rock.Albedo, Blend);
		UMaterialExpression* NormalMix = Graph.Lerp(Soil.Normal, Rock.Normal, Blend);
		UMaterialExpression* RoughMix = Graph.Lerp(Soil.Roughness, Rock.Roughness, Blend);
		UMaterialExpression* OcclusionMix = Graph.Lerp(Soil.Occlusion, Rock.Occlusion, Blend);

		// ---- colour ---------------------------------------------------------
		//
		// Vertex colour still carries the biome: the planet is one material and
		// the climate bands run from beach through grass and scrub to rock and
		// snow. So the scan contributes *variation*, not colour — divide its
		// own average out first, or a green scan under a green tint is a swamp
		// and a sand scan under a snow tint is dirty snow. The averages were
		// measured off the images at import and live in the manifest.
		UMaterialExpression* MeanMix = Graph.Lerp(
			SoilMean, Graph.Constant3(Steep.MeanAlbedo), Blend);
		UMaterialExpression* Variation = Graph.Divide(AlbedoMix, MeanMix);

		// Rock has no biome and takes no tint, so the ground's tint fades out
		// with it. Otherwise a cliff in a rainforest would be green rock.
		UMaterialExpression* TintMix = Graph.Lerp(SoilTint, Graph.Constant(1.0f), Blend);

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

		// Distance fade. Mips stop the near detail aliasing, but by the time a
		// two-metre pattern is a couple of pixels across it is only noise on
		// the histogram — fading it to neutral is both cheaper and cleaner.
		UMaterialExpressionPixelDepth* Depth = Graph.Make<UMaterialExpressionPixelDepth>();
		UMaterialExpressionSaturate* Fade = Graph.Make<UMaterialExpressionSaturate>();
		Fade->Input.Expression = Graph.Divide(Depth, Graph.Constant(60000.0f)); // 600 m

		UMaterialExpression* FadedVariation = Graph.Lerp(Variation, Graph.Constant(1.0f), Fade);

		// The ninety-metre macro fades out with the detail it modulates, and for
		// the same reason: past its mip range it is a few pixels per tile, and
		// what it contributes there is a visible grid rather than variation.
		// The kilometre one does not fade -- at ten kilometres its features are
		// still hundreds of pixels across, which is exactly why it is there.
		UMaterialExpression* FadedMacro = Graph.Lerp(Macro, Graph.Constant(1.0f), Fade);

		// Saturated, because three mean-one multipliers stacked on a tint have
		// a tail that goes above one and albedo above one is not a colour, it
		// is a light source. This clamps rather than rescales on purpose: if it
		// is doing real work the ground is blowing out and the tints are wrong,
		// which is a thing to fix in the data, not to hide with a divide.
		UMaterialExpression* BaseColour = Graph.Saturate(Graph.Multiply(
			Graph.Multiply(Graph.Multiply(TintMix, FadedVariation), FadedMacro), MacroFar));

		// Normal, faded to flat over the same distance. A normal map that
		// survives past its mip range is the other half of the shimmer.
		UMaterialExpressionConstant3Vector* FlatNormal = Graph.Make<UMaterialExpressionConstant3Vector>();
		FlatNormal->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
		UMaterialExpression* FadedNormal = Graph.Lerp(NormalMix, FlatNormal, Fade);

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

		Material->PostEditChange();

		UE_LOG(LogLedger, Log,
			TEXT("terrain material: %s at %.1f m over %s at %.1f m, height-blended"),
			*Flat.Name, Flat.TilingMetres, *Steep.Name, Steep.TilingMetres);
		return Material;
	}
}

#endif
