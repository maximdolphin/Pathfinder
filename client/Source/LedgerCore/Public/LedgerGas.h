// Air, and how much of it is above you. T079.
//
// **A gas giant has no surface, so "altitude" needs a datum that is not one.**
// The convention astronomers use is the one-bar level: the depth at which the
// pressure equals Earth's at sea level. A gas giant's quoted radius is that
// level, and so is this model's, which means a body's RadiusMetres means the
// same thing whether or not there is anything solid there.
//
// Depth is measured downwards from it and is positive going down, because
// everything interesting about a gas giant is below.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"

namespace LedgerGas
{
	/// Earth's sea level, and a gas giant's datum. Pascals.
	inline constexpr double OneBarPascals = 101325.0;

	/// Surface gravity, or the gravity at the one-bar level. m/s^2.
	LEDGERCORE_API double GravityAt(const FLedgerBody& Body, double DepthMetres);

	/// How far you have to rise for the pressure to fall by a factor of e.
	///
	/// kT / (m g). Thin air over a small cold world, deep air over a big warm
	/// one -- and it is why Jupiter's is 27 km against Earth's 8.5 despite
	/// Jupiter's far stronger gravity: hydrogen is fourteen times lighter than
	/// the nitrogen it is being compared with.
	LEDGERCORE_API double ScaleHeightMetres(
		const FLedgerBody& Body, double TemperatureKelvin);

	/// Pressure at a depth below the one-bar level, pascals.
	///
	/// Isothermal and hydrostatic: the closed-form solution of dP/dh = -rho g
	/// with rho = P m / (k T). Real giants get hotter as you go down, which
	/// makes the true pressure rise a little more slowly than this -- so this
	/// model predicts a crush depth slightly shallower than the real one, which
	/// is the safe direction for a thing that kills the ship.
	LEDGERCORE_API double PressurePascals(
		const FLedgerBody& Body, double TemperatureKelvin, double DepthMetres);

	/// The depth at which pressure reaches a limit, metres below one bar.
	///
	/// The inverse of the above. This is the number the acceptance is about:
	/// predicted before the descent, then compared with where the ship actually
	/// failed.
	LEDGERCORE_API double DepthForPressure(
		const FLedgerBody& Body, double TemperatureKelvin, double Pascals);

	/// The mean molecular mass of what a body's atmosphere is made of, kg.
	///
	/// Hydrogen and helium for anything that could hold them, nitrogen for
	/// anything that could not. That is one branch rather than a composition
	/// model, and it is the branch that matters: a hydrogen atmosphere has a
	/// scale height fourteen times a nitrogen one at the same temperature.
	LEDGERCORE_API double MolecularMassKg(const FLedgerBody& Body);
}
