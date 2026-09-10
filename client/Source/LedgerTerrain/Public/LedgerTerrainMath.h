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

	/// How far the rotation axis leans from the orbit's normal, radians. T073.
	///
	/// **This is what a season is.** A body with no tilt has the star over its
	/// equator all year, the same insolation at every latitude on every day,
	/// and no seasons whatever its orbit does. The climate field reads it, so
	/// it belongs to the terrain's parameters rather than to the renderer.
	///
	/// The default is Earth's, for a fixture that builds params by hand. The
	/// world overwrites it from the body the ephemeris describes.
	double AxialTiltRadians = 0.40910518;

	/// Whether this body holds an atmosphere. T078.
	///
	/// **No air, no water cycle.** The climate field marches moisture upwind
	/// from an ocean; on an airless body there is neither wind nor ocean, so
	/// moisture is zero everywhere and everything downstream of it -- snow,
	/// vegetation, the wet end of the biome set -- goes with it. Without this
	/// the first moon came back with snow-capped peaks under a black sky.
	///
	/// Decided by LedgerSky::RetainsAtmosphere from the body's mass, radius and
	/// temperature, not declared. See the note there.
	bool bHasAtmosphere = true;

	/// Ground somebody has changed, or null for a planet as generated (T062).
	///
	/// A shared pointer to an immutable delta, not a delta. The terrain is
	/// sampled on worker threads while the game thread may be adding an edit;
	/// publishing a whole new delta and swapping the pointer means a worker
	/// reads a consistent one without a lock, and the copy is cheap because
	/// there are tens of edits rather than millions of samples.
	TSharedPtr<const class FLedgerTerrainDelta> Delta;
};

namespace LedgerTerrain
{
	/// Where the ridge field's zero really is, and how far it runs above it.
	///
	/// **Measured, not assumed, and that is the whole of T428.**
	/// `LedgerNoise::ErodedRidged` ends with `(Sum / Normalisation) * 2 - 1`,
	/// which is the right remap for a field whose mean is 0.5. This one's is
	/// not: a ridged multifractal with a continuity weight and erosion damping
	/// puts its energy into a few ridges, so most of the surface sits low.
	/// Surveyed over the land of this planet, the returned value runs
	///
	///     min -1.000   p50 -0.493   p90 -0.139   max 0.440
	///
	/// so `FMath::Max(0.0, ...)` in `Elevation` was deleting everything below
	/// the ninety-seventh percentile. The mountain band was exactly zero on
	/// ninety per cent of the land and the whole planet topped out at 1,372 m
	/// against a 9 km maximum.
	///
	/// The pivot is set near the eighty-fifth percentile, so ranges stay rare,
	/// and the span carries the rest of the measured range up to one -- which
	/// is what gives a peak its full share of `MaxElevation` instead of the
	/// 0.44 the old remap left it.
	///
	/// These are numbers about a distribution, so they belong next to the
	/// survey that produced them: `-climate` prints it, and
	/// docs/comparisons/relief/ keeps it.
	constexpr double RidgePivot = -0.20;
	constexpr double RidgeSpan = 0.64;

	/// The ridge field, renormalised onto [0,1] against its own distribution.
	inline double RidgeStrength(double Raw)
	{
		return FMath::Clamp((Raw - RidgePivot) / RidgeSpan, 0.0, 1.0);
	}

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
	/// Which cube face a direction points at, and where on that face's *plane*.
	///
	/// **This is the inverse of `FaceToCube`, and not of `CubeToSphere`.** Given
	/// a point on the cube it is exact. Given a point on the sphere it is not:
	/// `CubeToSphere` applies an area-evening warp on top of the projection and
	/// this does not undo it, so
	/// `DirectionToFace(CubeToSphere(FaceToCube(f, u, v)))` comes back with
	/// coordinates that are wrong by up to 0.066 of a face -- 657 km on this
	/// planet, worst at the quarter points and zero at the centre and the
	/// edges. Use `SphereToFace` for anything that started on the sphere.
	LEDGERTERRAIN_API void DirectionToFace(const FVector3d& Direction, ELedgerCubeFace& OutFace, double& OutU, double& OutV);

	/// Which cube face a *sphere* direction belongs to, and where on it.
	///
	/// The one that round-trips: `SphereToFace(CubeToSphere(FaceToCube(f, u, v)))`
	/// returns (f, u, v). `CubeToSphere` has no closed-form inverse, so this is
	/// a damped fixed point -- twelve iterations, converging to well under a
	/// micro-face.
	///
	/// Anything that asks "which patch is under this point" wants this one.
	/// `DirectionToFace` answered that question in the quadtree's neighbour
	/// probes for as long as they existed, which meant edge stitching was
	/// decided by looking at ground up to six hundred kilometres away.
	LEDGERTERRAIN_API void SphereToFace(const FVector3d& UnitSphere, ELedgerCubeFace& OutFace, double& OutU, double& OutV);


	/// Terrain elevation in centimetres above the reference sphere, for a point
	/// on the unit sphere.
	/// How far past the coastline the continental field has fallen, in [0,1].
	/// Zero at the waterline, one at the lowest the field ever goes. The
	/// bathymetric profile is a function of this and nothing else, so a
	/// transect that reports it is a transect that can be calibrated against.
	LEDGERTERRAIN_API double OffshoreParameter(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params);

	/// `SampleSpacingMetres` is the distance between neighbouring vertices on
	/// the grid this sample belongs to, and it band-limits the near-field
	/// detail band (30 m down to 1.9 m, under 1.5 m of amplitude) to what that
	/// grid can actually resolve.
	///
	/// **This makes the height field a function of the grid as well as the
	/// position, which is a change to an invariant and is deliberate.** Two
	/// patches at different depths now disagree about the ground between them
	/// by up to a metre and a half. The alternatives were worse: without a
	/// near-field band the ground is a plane for thirty metres in every
	/// direction, and with an unlimited one a coarse patch samples a 1.9 m
	/// wavelength at 4.8 m spacing and produces a different random surface
	/// every time it is rebuilt at a different depth.
	///
	/// The disagreement is bounded and already handled: LOD transitions morph
	/// (T048), and SampleTerrain reads the drawn mesh rather than this function
	/// (T061), so collision and queries agree with what is on screen by
	/// construction rather than by luck.
	///
	/// Zero -- the default -- means no grid: the full field, which is what
	/// SurfaceRadiusAt and the diagnostics want.
	LEDGERTERRAIN_API double Elevation(
		const FVector3d& UnitSphere, const FLedgerTerrainParams& Params,
		double SampleSpacingMetres = 0.0);

	/// The pieces Elevation multiplies together, for diagnosis.
	///
	/// The height function stacks three independent masks on the mountain band
	/// -- ridged noise, a land mask and a province mask -- and a product of
	/// three numbers that each average well under one is a number that averages
	/// very much under one. Whether that is what flattens this planet is a
	/// question about the terms, and it cannot be answered from outside without
	/// being able to see them.
	struct FLedgerElevationTerms
	{
		double Continent = 0.0;
		double Land = 0.0;
		double Province = 0.0;
		double Ridges = 0.0;

		/// The ridge field before Elevation clamps it at zero. The clamp is what
		/// deletes the mountains (T428), so the raw value is the thing to look
		/// at when deciding what to replace the remap with.
		double RidgesRaw = 0.0;
		double Mountains = 0.0;
		double HeightFraction = 0.0;
	};

	LEDGERTERRAIN_API FLedgerElevationTerms ElevationTerms(
		const FVector3d& UnitSphere, const FLedgerTerrainParams& Params);


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
