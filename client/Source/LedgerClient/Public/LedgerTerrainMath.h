// Cube-sphere mapping and terrain height. Design §6.8.
//
// Pure functions, no engine state, no allocation. Everything the quadtree does
// geometrically lives here so it can be reasoned about — and tested — without a
// world, an actor, or a frame.
//
// Floats throughout: this is the presentation layer, and §5.2 only forbids
// floating point in *authoritative* state. The planet's shape is derived from a
// seed, so it is reproducible without being part of the sim's fold.

#pragma once

#include "CoreMinimal.h"

/// The six roots of the quadtree. A cube-sphere has no poles and no seams that
/// need special casing, which is why it beats a lat/long grid here (§8.4:
/// handle the edge case with the shape of the solution).
enum class ELedgerCubeFace : uint8
{
	PositiveX,
	NegativeX,
	PositiveY,
	NegativeY,
	PositiveZ,
	NegativeZ,
	Count
};

namespace LedgerTerrain
{
	/// Maps a face and a `[0,1]^2` coordinate on it to a point on the unit cube.
	LEDGERCLIENT_API FVector3d FaceToCube(ELedgerCubeFace Face, double U, double V);

	/// Cube to sphere, using the equal-area-ish mapping rather than plain
	/// normalisation. Straight normalisation bunches vertices toward the face
	/// centres and leaves the corners stretched by about 1.4x; this evens it out
	/// for the cost of three multiplies.
	LEDGERCLIENT_API FVector3d CubeToSphere(const FVector3d& OnCube);

	/// Deterministic value noise in 3d. Hash-based, so the same seed and
	/// position give the same height on every machine — which matters because
	/// the sim will eventually place things on this surface by coordinate.
	LEDGERCLIENT_API double ValueNoise(const FVector3d& Position, uint32 Seed);

	/// Fractal Brownian motion over `ValueNoise`. Returns roughly `[-1, 1]`.
	LEDGERCLIENT_API double FractalNoise(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double Lacunarity = 2.0,
		double Gain = 0.5);

	/// Terrain elevation in centimetres above the reference sphere, for a point
	/// on the unit sphere. Continents from low-frequency noise, ridges from the
	/// absolute value of a higher-frequency band.
	LEDGERCLIENT_API double Elevation(const FVector3d& UnitSphere, uint32 Seed, double MaxElevation);

	/// Surface position in planet-local space.
	LEDGERCLIENT_API FVector3d SurfacePoint(
		const FVector3d& UnitSphere,
		double Radius,
		uint32 Seed,
		double MaxElevation);

	/// Screen-space error for a node, in pixels.
	///
	/// This is the whole LOD decision: a node subdivides when the world-space
	/// error it would remove projects to more than a few pixels. Distance-based
	/// LOD rings are the alternative and they are wrong at every field of view
	/// but the one they were tuned at.
	LEDGERCLIENT_API double ScreenSpaceError(
		double NodeWorldSize,
		double DistanceToCamera,
		double ViewportWidthPixels,
		double HorizontalFovRadians);
}
