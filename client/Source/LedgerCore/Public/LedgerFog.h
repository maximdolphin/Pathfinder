// Ground fog, and why it has a top. T091.
//
// **Exponential height fog fills space from orbit because it has no notion of a
// sphere.** Its density is a function of world Z against an infinite horizontal
// plane, so on a planet that plane cuts through the ground on one side and out
// into space on the other. It was switched off in T089 for exactly that reason.
//
// What replaces it is not a better global fog. It is the observation that
// ground fog is a *local* phenomenon with a *flat top*: overnight the ground
// radiates its heat away, the air in contact with it cools, and the cold air
// runs downhill and pools. The top of that pool is level, which is why fog
// fills a valley and leaves the ridge above it clear -- and why a fog bank is
// invisible from orbit, being a hundred metres deep on a body six thousand
// kilometres across.
//
// So the model is a height, not a shape.

#pragma once

#include "CoreMinimal.h"

#include "LedgerAir.h"

/// A night's worth of cold air sitting in the low ground.
struct FLedgerFog
{
	/// Whether there is any at all.
	bool bForms = false;

	/// How deep the cold pool is above the lowest ground, metres. The fog's top
	/// is that height above the valley floor and it is level.
	double DepthMetres = 0.0;

	/// Extinction inside it, per metre. Visibility is about 3 / extinction, so
	/// 0.015 is two hundred metres and the definition of fog rather than mist.
	double ExtinctionPerMetre = 0.0;

	/// How much of it is left, 0 to 1. One at dawn, falling as the sun climbs.
	double Fraction = 0.0;
};

namespace LedgerFog
{
	/// The dew point depression assumed at the surface, kelvin.
	///
	/// **The one number here that is assumed rather than derived**, and it is
	/// the same one T089 uses to place a cloud base -- one humidity assumption
	/// used twice rather than two that can drift apart. A real humidity field
	/// belongs to T099, and when it exists this reads it instead.
	LEDGERCORE_API double DewPointDepressionKelvin();

	/// How deep the nocturnal cold pool has grown after a given time in the dark.
	///
	/// Turbulent diffusion into still air: h = sqrt(2 K t), with K the eddy
	/// diffusivity of a calm night. Eight hours gives about eighty metres, which
	/// is the depth a valley fog actually has.
	LEDGERCORE_API double InversionDepthMetres(double SecondsInDarkness);

	/// Thermal inertia of the ground, J m^-2 K^-1 s^-1/2.
	///
	/// The brake on a night's cooling, and the reason Mars swings ninety kelvin
	/// where Earth swings ten: damp soil conducts and stores heat several times
	/// better than dry dust.
	LEDGERCORE_API double ThermalInertia(const FLedgerAirProfile& Air);

	/// How far the ground has cooled after a given time in the dark, kelvin.
	///
	/// The surface radiates sigma T^4 and the sky radiates some of it back --
	/// the more blanket, the less net loss, which is why a cloudy night does not
	/// get cold and a desert one does. What is left is drawn out of the ground,
	/// which is a semi-infinite solid: dT = 2 F sqrt(t) / (I sqrt(pi)).
	LEDGERCORE_API double NightCoolingKelvin(
		const FLedgerAirProfile& Air, double SecondsInDarkness);

	/// The fog at a place, given how long it has been dark and where the sun is.
	LEDGERCORE_API FLedgerFog At(
		const FLedgerAirProfile& Air, double SecondsInDarkness,
		double SolarAltitudeRadians);
}
