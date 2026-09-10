// Three decks, at the heights the air puts them. T094.
//
// **Cloud altitude is a thermometer reading, not an art choice.** A cumulus base
// is the height at which a rising parcel has cooled to its dew point, and it is
// flat across a whole landscape because every parcel starts at the same
// temperature and cools at the same rate -- which is why fair-weather cumulus
// look like they are resting on a sheet of glass. A cirrus deck is high because
// it is ice, and ice needs about forty below.
//
// So a deck is a temperature, converted to a height by a lapse rate. Change the
// air and the decks move, and nothing has to be told.

#pragma once

#include "CoreMinimal.h"

#include "LedgerAir.h"
#include "LedgerBody.h"

/// One layer of cloud.
struct FLedgerCloudDeck
{
	bool bPresent = false;

	/// Metres above the datum.
	double BaseMetres = 0.0;
	double TopMetres = 0.0;

	/// How much of the sky it covers, 0 to 1.
	double Coverage = 0.0;

	/// How opaque it is where it is there. A cumulus is nearly solid and a
	/// cirrus is a veil, and that is the difference between water droplets and
	/// sparse ice needles.
	double Opacity = 0.0;

	/// How fast it is carried, metres per second, and which way -- the wind at
	/// its own altitude, which is not the wind at the ground.
	FVector2D DriftMetresPerSecond = FVector2D::ZeroVector;
};

/// The whole sky's worth.
struct FLedgerCloudDecks
{
	FLedgerCloudDeck Cumulus;
	FLedgerCloudDeck Middle;
	FLedgerCloudDeck Cirrus;

	/// Where the temperature stops falling, metres. Nothing convective goes
	/// above it, which is why thunderstorm anvils are flat.
	double TropopauseMetres = 0.0;

	/// The lowest base and the highest top, for whoever has to fit a renderer
	/// around all three.
	double LowestMetres = 0.0;
	double HighestMetres = 0.0;
};

namespace LedgerCloud
{
	/// The height at which the air is a given temperature, metres above the
	/// datum. Negative if it is already colder than that at the ground.
	LEDGERCORE_API double HeightOfTemperature(
		const FLedgerAirProfile& Air, double Kelvin);

	/// The lapse rate the *atmosphere* has, as opposed to the one a dry parcel
	/// follows, kelvin per metre.
	///
	/// **These are two different numbers and using one for both is the mistake
	/// this file exists to avoid.** A dry parcel cools at g/cp -- 9.8 K/km on
	/// Earth -- and that is what sets a cumulus base, because the parcel rising
	/// off the ground is unsaturated. The atmosphere as a whole cools at about
	/// 6.5, because condensation releases heat into it on the way up. Use the
	/// dry rate for the environment and cirrus lands at five kilometres instead
	/// of eight and a half.
	LEDGERCORE_API double EnvironmentalLapseRate(const FLedgerAirProfile& Air);

	/// Every deck, at a place and a time.
	LEDGERCORE_API FLedgerCloudDecks DecksAt(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double SecondsFromEpoch);
}
