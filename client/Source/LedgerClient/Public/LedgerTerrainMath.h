// Cube-sphere mapping and terrain height. Design §6.8.
//
// Pure functions, no engine state, no allocation, no globals. Everything the
// quadtree does geometrically lives here so it can be reasoned about — and run
// off the game thread — without a world, an actor, or a frame.
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
	LEDGERCLIENT_API FVector3d FaceToCube(ELedgerCubeFace Face, double U, double V);

	/// Cube to sphere, using the equal-area-ish mapping rather than plain
	/// normalisation. Straight normalisation bunches vertices toward the face
	/// centres and leaves the corners stretched by about 1.4x; this evens it out
	/// for the cost of three multiplies.
	LEDGERCLIENT_API FVector3d CubeToSphere(const FVector3d& OnCube);

	/// Deterministic gradient noise in 3d. Hash-based, so the same seed and
	/// position give the same height on every machine — which matters because
	/// the sim will eventually place things on this surface by coordinate.
	///
	/// Gradient rather than value noise: value noise has visible axis-aligned
	/// structure that survives every amount of octave stacking, and on a planet
	/// it reads as a grid pressed into the continents.
	LEDGERCLIENT_API double GradientNoise(const FVector3d& Position, uint32 Seed);

	/// Fractal Brownian motion. Returns roughly `[-1, 1]`.
	LEDGERCLIENT_API double FractalNoise(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double Lacunarity = 2.02,
		double Gain = 0.5);

	/// Ridged multifractal: folds each octave about zero and weights the next by
	/// the last, so ridges reinforce into connected ranges instead of scattering
	/// into isolated bumps. This is what makes mountains look like mountains.
	LEDGERCLIENT_API double RidgedNoise(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double Lacunarity = 2.03,
		double Gain = 0.5);

	/// Noise value together with its analytic gradient.
	struct FNoiseSample
	{
		double Value = 0.0;
		FVector3d Derivative = FVector3d::ZeroVector;
	};

	/// Gradient noise and its exact derivative, from the same eight corners.
	/// Finite differences would cost four evaluations and be wrong at the octave
	/// boundaries; this is one evaluation and exact.
	LEDGERCLIENT_API FNoiseSample GradientNoiseWithDerivative(const FVector3d& Position, uint32 Seed);

	/// Erosion, done analytically.
	///
	/// Real hydraulic erosion is an iterative simulation over a heightfield, and
	/// a quadtree cannot run one: patches are generated independently, at
	/// different times, at different resolutions, so any regional simulation
	/// produces seams at every LOD boundary. What it *can* do is the effect
	/// erosion has on the shape.
	///
	/// Each octave is damped by the accumulated slope of the octaves above it.
	/// Detail therefore collects in the flats and thins out on the steeps, which
	/// is what water does: valleys widen and smooth, ridges stay sharp, and the
	/// whole surface acquires drainage. It is a pure function of position, so it
	/// costs nothing at a patch boundary and is identical at every LOD.
	LEDGERCLIENT_API double ErodedNoise(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double ErosionStrength,
		double Lacunarity = 2.01,
		double Gain = 0.5);

	/// Ridged multifractal with the same slope damping. Mountains that drain.
	LEDGERCLIENT_API double ErodedRidgedNoise(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double ErosionStrength,
		double Lacunarity = 2.01,
		double Gain = 0.5);

	/// Terrain elevation in centimetres above the reference sphere, for a point
	/// on the unit sphere.
	/// How far past the coastline the continental field has fallen, in [0,1].
	/// Zero at the waterline, one at the lowest the field ever goes. The
	/// bathymetric profile is a function of this and nothing else, so a
	/// transect that reports it is a transect that can be calibrated against.
	LEDGERCLIENT_API double OffshoreParameter(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params);

	LEDGERCLIENT_API double Elevation(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params);

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
