// Terrain surface look: generated detail textures and the triplanar material
// that samples them. Design §6.8 ("Runtime Virtual Texture for surface material
// blending by slope and altitude" — same intent, simpler mechanism).
//
// **Why textures rather than a noise node.** A `Noise` material expression has
// no mip chain, so it aliases the moment its features fall below a pixel — which
// on a planet is most of the screen. Every attempt to tune the noise node
// traded one artefact for another: fine features gave pumice, coarse features
// gave nothing. A texture has mips, and trilinear filtering does the
// anti-aliasing for free. That is the whole reason this file exists.
//
// **Why triplanar.** A sphere has no UV parameterisation that does not pinch
// somewhere, and the terrain patches carry per-patch UVs that would seam at
// every LOD boundary. World-aligned projection along the three axes, blended by
// the surface normal, has no seams and no parameterisation at all.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UTexture2D;

namespace LedgerSurface
{
	/// Where baked materials live. One place, derived from, never typed twice.
	inline constexpr const TCHAR* MaterialPackageRoot = TEXT("/Game/Materials/");

	/// Runs every material builder once and saves the result as an asset.
	/// Editor only, and the whole reason a packaged build has a look at all.
	LEDGERMATERIAL_API bool BakeMaterials(FString& Report);

	/// A tiling detail texture with a **full mip chain**, generated from noise.
	///
	/// `UTexture2D::CreateTransient` produces a single mip, which is exactly the
	/// aliasing problem again. This builds every level down to 1x1 by box
	/// filtering, so the GPU has something to select between.
	LEDGERMATERIAL_API UTexture2D* CreateDetailAlbedo(UObject* Outer, int32 Size, uint32 Seed);

	/// A tangent-space normal map derived from the same height field, so the
	/// grain in the albedo and the grain in the lighting agree.
	LEDGERMATERIAL_API UTexture2D* CreateDetailNormal(UObject* Outer, int32 Size, uint32 Seed, double Strength);

	/// The terrain material: vertex colour from the mesh, modulated by
	/// triplanar detail at two scales, with a tangent-space normal that fades
	/// out with distance before it can alias.
	///
	/// Editor-only — shaders compile at runtime only in an editor build. A
	/// packaged game needs this saved as an asset.
	LEDGERMATERIAL_API UMaterialInterface* CreateTerrainMaterial(UObject* Outer, uint32 Seed);

	/// A plain lit material of a given colour, for the buildings and trees.
	LEDGERMATERIAL_API UMaterialInterface* CreateFlatMaterial(UObject* Outer, const FLinearColor& Colour, float Roughness);

	/// The sea. Depth-tinted from vertex alpha, Fresnel toward a sky-facing
	/// tint at grazing angles, and smooth enough that screen-space reflections
	/// have something to work with.
	///
	/// **Opaque, not translucent.** Translucent surfaces do not write depth and
	/// so are skipped by screen-space reflection, which is the effect that makes
	/// an ocean read as water rather than as a blue plane. Depth-tinting the
	/// albedo gets most of what transparency would have bought, and keeps the
	/// reflection.
	LEDGERMATERIAL_API UMaterialInterface* CreateWaterMaterial(UObject* Outer);

	/// Post-process murk, applied to the camera while it is below the sea.
	///
	/// A post-process **material** rather than a post-process volume: the
	/// underwater look that matters is loss of contrast with distance, and
	/// distance is exactly what `FPostProcessSettings` has no field for. It has
	/// colour grading, bloom, exposure — every one of them a whole-screen
	/// constant. Reaching scene depth means a material, and once there is a
	/// material there is no reason left for the volume.
	LEDGERMATERIAL_API UMaterialInterface* CreateUnderwaterMaterial(UObject* Outer);

	/// The volumetric cloud material: three decks in one layer. T094.
	///
	/// Unreal draws one volumetric cloud per scene, so three decks cannot be
	/// three components -- they are three bands inside one layer, and the
	/// material reads its own altitude to know which is which. The band
	/// centres, widths, coverages and densities are all parameters, because a
	/// deck's height is a temperature and temperatures move.
	LEDGERMATERIAL_API UMaterialInterface* CreateCloudMaterial(UObject* Outer);

	/// Unlit emissive, for the star field. T077 computed eight hundred stars
	/// and nothing drew them; this is what draws them.
	LEDGERMATERIAL_API UMaterialInterface* CreateStarMaterial(UObject* Outer);
}
