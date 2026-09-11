// Temperature and pressure as physical fields. T099.
//
// **One function, asked by everything.** The climate knows the temperature at
// the ground and the weather knows the pressure at sea level; this puts the
// lapse rate and the scale height between them and answers for any point in
// the air. The settlement, the renderer's rain-or-snow and anything later that
// sizes life support or picks a building material read this, so they cannot
// hold different numbers for the same place.

#pragma once

#include "CoreMinimal.h"

class UWorld;

struct FLedgerAirHere
{
	/// Kelvin at the point, and at the ground under it.
	double Kelvin = 0.0;
	double GroundKelvin = 0.0;

	/// Pascals at the point, and at sea level under it, weather included.
	double Pascals = 0.0;
	double SeaLevelPascals = 0.0;

	/// Kilograms per cubic metre at the point.
	double DensityKgPerM3 = 0.0;

	/// Metres above the datum, and above the ground.
	double AltitudeMetres = 0.0;
	double AboveGroundMetres = 0.0;

	/// Kelvin lost per metre of height here: the prediction the field keeps.
	double LapseKelvinPerMetre = 0.0;

	bool bValid = false;
};

namespace LedgerEnvironment
{
	/// The air at a world position, at the world's time.
	///
	/// Temperature is the climate's at the ground -- latitude, season and the
	/// ground's own height -- less the environmental lapse rate for every metre
	/// above it. Pressure is the weather's at sea level, low or high included,
	/// falling off over the scale height.
	/// ponytail: no frontal temperature contrast; a low changes the pressure
	/// and not the temperature. Add a cell term here when a front has to feel
	/// cold, and every consumer gets it at once.
	LEDGERCLIENT_API FLedgerAirHere At(const UWorld* World, const FVector& WorldPosition);
}
