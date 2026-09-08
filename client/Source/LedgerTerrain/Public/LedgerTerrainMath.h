// Cube-sphere mapping and the planet's height function. Design §6.8.
//
// Pure functions, no engine state, no allocation, no globals. Everything the
// quadtree does geometrically lives here so it can be reasoned about — and run
// off the game thread — without a world, an actor, or a frame.
//
// The noise primitives this is built on are in LedgerCore: the material module
// needs them too, and this module is above that one.

#pragma once

#include "CoreMinimal.h"
#include "LedgerNoise.h"

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

/// Everything the height function needs, gathered so a worker thread can be
/// handed a copy rather than a pointer back into an actor.
struct FLedgerTerrainParams
{
	uint32 Seed = 0;
	/// Reference sphere radius, centimetres.
	double Radius = 0.0;
	/// Peak elevation above the reference sphere, centimetres.
	double MaxElevation = 0.0;
	/// Fraction of `MaxElevation` that counts as sea level, from the bottom.
	double SeaLevel = 0.0;
};

namespace LedgerTerrain
{
	/// Maps a face and a `[0,1]^2` coordinate on it to a point on the unit cube.
	LEDGERTERRAIN_API FVector3d FaceToCube(ELedgerCubeFace Face, double U, double V);

	/// Cube to sphere, using the equal-area-ish mapping rather than plain
	/// normalisation. Straight normalisation bunches vertices toward the face
	/// centres and leaves the corners stretched by about 1.4x; this evens it out
	/// for the cost of three multiplies.
	LEDGERTERRAIN_API FVector3d CubeToSphere(const FVector3d& OnCube);

	/// Which cube face a direction points at, and where on it. The inverse
	/// of FaceToCube, and the way anything holding a direction finds the
	/// node that contains it.
	LEDGERTERRAIN_API void DirectionToFace(const FVector3d& Direction, ELedgerCubeFace& OutFace, double& OutU, double& OutV);


	/// Terrain elevation in centimetres above the reference sphere, for a point
	/// on the unit sphere.
	/// How far past the coastline the continental field has fallen, in [0,1].
	/// Zero at the waterline, one at the lowest the field ever goes. The
	/// bathymetric profile is a function of this and nothing else, so a
	/// transect that reports it is a transect that can be calibrated against.
	LEDGERTERRAIN_API double OffshoreParameter(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params);

	LEDGERTERRAIN_API double Elevation(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params);

	/// Screen-space error for a node, in pixels.
	///
	/// This is the whole LOD decision: a node subdivides when the world-space
	/// error it would remove projects to more than a few pixels. Distance-based
	/// LOD rings are the alternative and they are wrong at every field of view
	/// but the one they were tuned at.
	LEDGERTERRAIN_API double ScreenSpaceError(
		double NodeWorldSize,
		double DistanceToCamera,
		double ViewportWidthPixels,
		double HorizontalFovRadians);
}
