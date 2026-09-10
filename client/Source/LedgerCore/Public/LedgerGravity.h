// Every body pulls. T083.
//
// **One hardcoded constant is a lie that only shows up between places.** A ship
// sitting on a planet feels that planet and nothing else to a part in a million,
// so a single surface gravity is right there and wrong the moment anybody
// leaves. Coasting between two bodies, the thing that decides where you end up
// is which one is winning, and that changes on the way.
//
// So the field is a sum over every body the ephemeris knows about, and the
// question "which body am I near" has an answer with a boundary in it.
//
// Metres, seconds, and metres per second squared.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"

namespace LedgerGravity
{
	/// The acceleration at a point in the system frame, from every body.
	LEDGERCORE_API FVector3d FieldAt(
		const FLedgerSystem& System, const FVector3d& PositionMetres,
		double SecondsFromEpoch);

	/// How far out a body's pull beats its parent's, metres.
	///
	/// The sphere of influence: a(m/M)^(2/5). Not the distance at which the two
	/// pulls are equal -- that is a different and less useful number -- but the
	/// distance inside which treating the body as the primary and the parent as
	/// a perturbation is the better approximation. It is the boundary a
	/// trajectory planner switches at, which is why it is the one computed.
	LEDGERCORE_API double SphereOfInfluenceMetres(
		const FLedgerSystem& System, int32 BodyIndex);

	/// Which body's sphere of influence a point is in.
	///
	/// The deepest one: a point inside a moon's sphere is also inside its
	/// planet's, and the moon is the answer. Falls back to the primary, whose
	/// sphere is everything.
	LEDGERCORE_API int32 DominantBody(
		const FLedgerSystem& System, const FVector3d& PositionMetres,
		double SecondsFromEpoch);

	/// One step of a trajectory under the full field.
	///
	/// Fourth-order Runge-Kutta, because a coast between two bodies is exactly
	/// where a first-order integrator's error accumulates in the direction of
	/// travel and quietly changes the destination.
	LEDGERCORE_API void Step(
		const FLedgerSystem& System, FVector3d& Position, FVector3d& Velocity,
		double SecondsFromEpoch, double StepSeconds);
}
