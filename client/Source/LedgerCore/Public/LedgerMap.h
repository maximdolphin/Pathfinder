// What is where, how far, and how long to get there. T087.
//
// **The map is a query, not a data structure.** Nothing here is stored: a
// site's position is a function of the ephemeris and the time, and a travel
// time is a function of both endpoints and the ship. That falls straight out of
// T070's decision to solve rather than integrate, and it is what lets a route
// be priced before it is flown -- and priced for next Tuesday as cheaply as for
// now.
//
// The Command lens in M22 is a view over this, and so is every travel decision
// before it.

#pragma once

#include "CoreMinimal.h"

#include "LedgerBody.h"

/// A place worth going: somewhere on or above a body.
///
/// An anchor direction rather than a latitude and longitude, for the same
/// reason the surface frame uses one -- the poles are places too.
struct FLedgerSite
{
	FString Name;

	/// Which body. An index into the system, because a pointer would let a site
	/// outlive the system that gives it meaning.
	int32 BodyIndex = INDEX_NONE;

	/// Where on it, as a unit direction in the body's rotating frame.
	FVector3d AnchorDirection = FVector3d::UnitZ();

	/// How far above the surface, metres. Zero is the ground; an orbit or a
	/// station stand-off is a positive number.
	double AltitudeMetres = 0.0;
};

namespace LedgerMap
{
	/// Every body's default site: the ground at its prime meridian, or for a
	/// station the point itself. This is the "what is where" half.
	LEDGERCORE_API void Sites(const FLedgerSystem& System, TArray<FLedgerSite>& Out);

	/// Where a site is in the system frame at a time, metres.
	LEDGERCORE_API FVector3d PositionAt(
		const FLedgerSystem& System, const FLedgerSite& Site, double SecondsFromEpoch);

	/// How fast a site is moving in the system frame, metres per second.
	///
	/// A central difference rather than the ephemeris's own velocity, because a
	/// site is not a body: it is on the outside of one, and the body's spin
	/// carries it at a few hundred metres a second that the orbital velocity
	/// knows nothing about.
	LEDGERCORE_API FVector3d VelocityAt(
		const FLedgerSystem& System, const FLedgerSite& Site, double SecondsFromEpoch);

	/// Straight-line distance between two sites at one instant, metres.
	LEDGERCORE_API double DistanceMetres(
		const FLedgerSystem& System, const FLedgerSite& From, const FLedgerSite& To,
		double SecondsFromEpoch);

	/// How long a constant-thrust crossing takes, seconds. Negative if the ship
	/// cannot make it -- which it always can, given time, so negative here means
	/// the inputs were nonsense rather than the trip being impossible.
	///
	/// **Accelerate, flip, decelerate**, which is the brachistochrone a torch
	/// ship actually flies and not a Hohmann transfer: for a ship that can
	/// thrust the whole way, the fast route is the straight one. From rest that
	/// is T = 2 sqrt(d / a), and the flip is at the midpoint.
	///
	/// It is not from rest. The ship leaves a body that is already moving, so
	/// it starts with a closing speed v along the route and the flip is no
	/// longer halfway:
	///
	///     T = (2 sqrt(v^2/2 + a d) - v) / a
	///
	/// And **the destination moves while you are on the way**. The distance to
	/// solve for is the distance to where the site WILL be, which depends on
	/// how long the trip takes, which depends on the distance. So this iterates
	/// the fixed point rather than measuring the gap at departure and calling
	/// it the trip.
	///
	/// What it does not model is gravity, or the part of the relative velocity
	/// that points across the route rather than along it. Both are real and
	/// both make the flight longer than the quote; how much longer is measured
	/// by flying it, in docs/comparisons/travel-time/.
	LEDGERCORE_API double TravelTimeSeconds(
		const FLedgerSystem& System, const FLedgerSite& From, const FLedgerSite& To,
		double SecondsFromEpoch, double AccelerationMetresPerSecond2);

	/// Where to point at departure: the intercept the travel time solved for.
	LEDGERCORE_API FVector3d InterceptPosition(
		const FLedgerSystem& System, const FLedgerSite& From, const FLedgerSite& To,
		double SecondsFromEpoch, double AccelerationMetresPerSecond2);
}
