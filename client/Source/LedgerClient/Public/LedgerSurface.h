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
	/// A tiling detail texture with a **full mip chain**, generated from noise.
	///
	/// `UTexture2D::CreateTransient` produces a single mip, which is exactly the
	/// aliasing problem again. This builds every level down to 1x1 by box
	/// filtering, so the GPU has something to select between.
	LEDGERCLIENT_API UTexture2D* CreateDetailAlbedo(UObject* Outer, int32 Size, uint32 Seed);

	/// A tangent-space normal map derived from the same height field, so the
	/// grain in the albedo and the grain in the lighting agree.
	LEDGERCLIENT_API UTexture2D* CreateDetailNormal(UObject* Outer, int32 Size, uint32 Seed, double Strength);

	/// The terrain material: vertex colour from the mesh, modulated by
	/// triplanar detail at two scales, with a tangent-space normal that fades
	/// out with distance before it can alias.
	///
	/// Editor-only — shaders compile at runtime only in an editor build. A
	/// packaged game needs this saved as an asset.
	LEDGERCLIENT_API UMaterialInterface* CreateTerrainMaterial(UObject* Outer, uint32 Seed);

	/// A plain lit material of a given colour, for the buildings and trees.
	LEDGERCLIENT_API UMaterialInterface* CreateFlatMaterial(UObject* Outer, const FLinearColor& Colour, float Roughness);
}
