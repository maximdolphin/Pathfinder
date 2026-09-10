// A body that is not a sphere. T081.
//
// **Gravity on an irregular body does not point at its centre**, and that is the
// whole of what makes this different from every other body in the project. On a
// sphere the shell theorem lets you pretend all the mass is at a point; on
// something shaped like a peanut it is simply false, and a lander that assumed
// it would touch down leaning.
//
// So gravity here is a sum over the body's actual mass rather than a formula
// about its centre. That is expensive, which is why it is computed against a
// cached set of mass points built once per body, and why nothing else in the
// project does it this way -- a planet is a sphere to a part in a thousand and
// deserves the cheap answer.
//
// Metres and kilogrammes, like everything else physical.

#pragma once

#include "CoreMinimal.h"

/// The shape of an irregular body, as a radius in every direction.
///
/// A displaced sphere rather than a mesh: it is defined everywhere, it can be
/// asked about a direction without a ray cast, and the same function that gives
/// the surface gives the mass distribution. A concave body would need a real
/// mesh; asteroids that are merely lumpy do not.
struct FLedgerAsteroidShape
{
	uint32 Seed = 0;

	/// The radius this varies about, metres.
	double MeanRadiusMetres = 5000.0;

	/// How far the surface departs from that, as a fraction. 0 is a sphere;
	/// 0.35 is a convincing rubble pile; past about 0.5 the shape folds over
	/// itself and stops being a function of direction.
	double Irregularity = 0.3;

	double DensityKgPerM3 = 2000.0;
};

namespace LedgerAsteroid
{
	/// The surface radius in a direction, metres.
	LEDGERCORE_API double RadiusInDirection(
		const FLedgerAsteroidShape& Shape, const FVector3d& Direction);

	/// Total mass, by integrating the shape.
	LEDGERCORE_API double MassKg(const FLedgerAsteroidShape& Shape);

	/// Gravitational acceleration at a point, m/s^2, in the body's frame.
	///
	/// **Do not call GetSafeNormal on the result without a tolerance.** An
	/// asteroid pulls at a few thousandths of a metre per second squared, and a
	/// few radii out it is tens of microns -- whose SQUARED length is below
	/// Unreal's default 1e-8 threshold, so the default normalise returns a zero
	/// vector and every angle computed from it comes out as ninety degrees.
	/// That is what the first run of Ledger.Asteroid.ASphereGivesBackTheTextbook
	/// reported at ten radii, with the magnitudes right to a fifth of a per
	/// cent all the way out.
	///
	/// Summed over the body's mass, not taken from its centre. Outside the body
	/// this converges to GM/r^2 as the distance grows, which is the check that
	/// the sum is a gravity field and not a shape function wearing one.
	LEDGERCORE_API FVector3d GravityAt(
		const FLedgerAsteroidShape& Shape, const FVector3d& PointMetres);

	/// The outward normal of the surface at a direction.
	///
	/// Needed to ask the question the acceptance asks: whether a person standing
	/// there is pulled INTO the ground rather than along it or off it.
	LEDGERCORE_API FVector3d SurfaceNormal(
		const FLedgerAsteroidShape& Shape, const FVector3d& Direction);
}
