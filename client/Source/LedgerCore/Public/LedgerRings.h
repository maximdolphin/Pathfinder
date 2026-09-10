// Rings, and the shadow they throw. T080.
//
// **A ring is where a moon cannot be.** Inside the Roche limit, tidal forces
// pull a self-gravitating body apart faster than its own gravity can hold it
// together, so anything there stays rubble. That is not a decorative fact: it
// is what decides where a ring's outer edge is, and it means the extent is
// derived from the two bodies' densities rather than chosen.
//
// The gaps are the same kind of fact. A particle whose orbital period is a
// simple ratio of a shepherd moon's gets the same nudge at the same point every
// few orbits and is eventually swept out, which is why Saturn's Cassini
// division is where it is.
//
// Radii in metres, measured from the body's centre.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"

/// A ring system around one body.
struct FLedgerRings
{
	int32 BodyIndex = INDEX_NONE;
	double InnerRadiusMetres = 0.0;
	double OuterRadiusMetres = 0.0;

	/// How much light the ring blocks looking straight through it, at its
	/// densest. Saturn's B ring is around 1.5; a faint ring is a hundredth.
	double PeakOpticalDepth = 0.0;
};

namespace LedgerRings
{
	/// The distance inside which a fluid body is pulled apart, metres.
	///
	/// 2.44 R (rho_primary / rho_satellite)^(1/3). The fluid form rather than
	/// the rigid one, because rubble behaves like a fluid on this scale -- a
	/// solid moonlet can survive closer in, which is why shepherd moons exist
	/// inside rings.
	LEDGERCORE_API double RocheLimitMetres(
		const FLedgerBody& Primary, double SatelliteDensityKgPerM3);

	/// The rings a body would have, or an empty extent if it would have none.
	///
	/// Derived: the outer edge is the Roche limit for icy rubble, the inner is
	/// where the body's own atmosphere would drag them down. A body with no
	/// room between the two gets no rings, which is why small ones do not have
	/// them and gas giants do.
	LEDGERCORE_API FLedgerRings For(
		const FLedgerSystem& System, int32 BodyIndex);

	/// How much of the light is blocked at a radius, looking straight down.
	///
	/// Zero outside the ring, zero in the gaps, and a profile between. This is
	/// the quantity a shadow is made of and the quantity a renderer would
	/// sample, so there is one of it.
	LEDGERCORE_API double OpticalDepthAt(
		const FLedgerRings& Rings, const FLedgerSystem& System, double RadiusMetres);

	/// Whether a radius falls in a gap swept by a resonance with a moon.
	LEDGERCORE_API bool InGap(
		const FLedgerRings& Rings, const FLedgerSystem& System, double RadiusMetres);

	/// How much of the star the rings hide from a point on the body, 0 to 1.
	///
	/// The point is a unit direction in the body frame; the ring lies in that
	/// frame's equatorial plane, which is what makes this a line crossing a
	/// plane rather than a projection anybody has to think about.
	///
	/// **This is the number that moves across a year.** The shadow's width is
	/// set by how far the star is out of the ring plane, so it is a thin line
	/// on the equator at an equinox and a broad band across the winter
	/// hemisphere at a solstice -- which is what Saturn does and what nobody
	/// had to write down for it to happen.
	LEDGERCORE_API double ShadowAt(
		const FLedgerRings& Rings, const FLedgerSystem& System,
		const FVector3d& SurfaceDirection, double SecondsFromEpoch);
}
