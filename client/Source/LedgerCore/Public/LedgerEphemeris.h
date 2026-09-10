// Where every body is, at any time. T070.
//
// **Analytic, not integrated.** An integrator has to be stepped from the epoch
// to the moment you care about, so knowing where a moon was last Tuesday costs
// a simulated week, and every step adds error that never comes back. A Kepler
// solution is a function of time: the same call at t = 0 and at t = a century
// costs the same and neither drifts.
//
// That is also what makes the map and the sky agree without either telling the
// other, and what lets a save file be a seed and a timestamp.
//
// The one place this is wrong is that it ignores gravitational interaction
// between bodies. Real orbits precess; these do not. For a game whose bodies are
// a star, a planet and a moon that is a rounding error against the century the
// acceptance asks about, and if a system ever needs perturbation it needs a
// different model rather than a fudge factor in this one.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"

/// Where a body is and how fast, in the frame of whatever it orbits.
struct FLedgerState
{
	/// Metres, from the parent's centre.
	FVector3d PositionMetres = FVector3d::ZeroVector;

	/// Metres per second, relative to the parent.
	FVector3d VelocityMetresPerSecond = FVector3d::ZeroVector;
};

namespace LedgerEphemeris
{
	/// Gravitational constant, SI. CODATA 2018.
	inline constexpr double GravitationalConstant = 6.67430e-11;

	/// Mean motion: radians of mean anomaly per second.
	LEDGERCORE_API double MeanMotion(double ParentMassKg, double SemiMajorAxisMetres);

	/// The orbital period in seconds, for reporting and for tests that want to
	/// step exactly one revolution.
	LEDGERCORE_API double PeriodSeconds(double ParentMassKg, double SemiMajorAxisMetres);

	/// Solves Kepler's equation, M = E - e sin E, for the eccentric anomaly.
	///
	/// Newton-Raphson from a good first guess. It is exposed because it is the
	/// one part of this with a convergence property worth testing on its own:
	/// the naive starting point (E = M) takes many more iterations at high
	/// eccentricity, and "many more" in a per-frame call is a frame time.
	LEDGERCORE_API double EccentricAnomaly(double MeanAnomalyRadians, double Eccentricity);

	/// One body's state relative to its parent, at a time measured in seconds
	/// from the system's epoch.
	LEDGERCORE_API FLedgerState StateAt(
		const FLedgerBody& Body, double ParentMassKg, double SecondsFromEpoch);

	/// Every body's state in the frame of the system's primary, which is the
	/// frame the renderer and the map both want.
	///
	/// A moon's position is its own orbit around its planet plus the planet's
	/// around the star, which is why this walks the parent chain rather than
	/// returning what StateAt does.
	LEDGERCORE_API void StatesAt(
		const FLedgerSystem& System, double SecondsFromEpoch, TArray<FLedgerState>& Out);

	/// How far a body has turned about its own axis, radians, at a time.
	LEDGERCORE_API double RotationAt(const FLedgerBody& Body, double SecondsFromEpoch);
}
