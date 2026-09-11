// One place gameplay asks the ground a question. T061, ARCH §3.
//
// **The answer has to be the mesh, not the height function.** They are not the
// same surface: the mesh is a bilinear interpolation of the field sampled every
// few metres, and on rough ground it sits over a metre away from the field it
// interpolates (docs/comparisons/scatter/). A physics trace hits the mesh, a
// character stands on the mesh, and an API that answered from the field would
// disagree with both by exactly that much -- while looking authoritative.
//
// So this reads the live patch: the same vertices that were uploaded, at the
// LOD currently drawn, interpolated over the same triangles the collision was
// cooked from. Where there is no patch loaded there is no answer, and it says
// so rather than guessing.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBiome.h"
#include "LedgerClimate.h"

/// What the ground is at a point.
struct FLedgerTerrainSample
{
	/// False when no patch is loaded at this direction. Everything below is
	/// meaningless then, and a caller that ignores this is a caller that will
	/// put something at the centre of the planet.
	bool bValid = false;

	/// Distance from the planet centre to the surface, centimetres. The world
	/// position is `PlanetOrigin + Direction * RadiusCm`.
	double RadiusCm = 0.0;

	/// Metres above sea level, which is what most callers actually want.
	double AltitudeMetres = 0.0;

	/// The drawn surface normal, in world space.
	FVector3d Normal = FVector3d::ZeroVector;

	/// Angle between that normal and straight up, degrees.
	double SlopeDegrees = 0.0;

	/// Temperature, moisture and altitude here.
	FLedgerClimate Climate;

	/// Which three biomes this ground is made of, and in what proportion. The
	/// weights are over the palette's slots and sum to one.
	FLedgerBiomePalette Palette;
	FVector3f BiomeWeights = FVector3f::ZeroVector;

	/// Snow lying, 0 to 1.
	float Snow = 0.0f;

	/// How wide the patch that answered is, centimetres. A caller that cares
	/// how precise the answer is should look at this: the mesh resolves about
	/// a sixty-fourth of it.
	double PatchWorldSize = 0.0;
	/// Whether the drawn section answering has collision enabled.
	bool bCollision = false;
};
