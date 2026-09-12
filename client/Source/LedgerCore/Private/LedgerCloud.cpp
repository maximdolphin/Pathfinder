#include "LedgerCloud.h"

#include "LedgerFog.h"
#include "LedgerMath.h"
#include "LedgerWeather.h"

namespace
{
	/// The temperatures the decks live between, kelvin.
	///
	/// **Cloud classification is a temperature classification.** A cumulus is
	/// water and glaciates around ten below; the middle deck is the mixed-phase
	/// range; a cirrus is pure ice and needs about forty below, which is where
	/// water freezes without anything to freeze onto. The heights follow from
	/// the lapse rate and are different on every body.
	constexpr double CloudCumulusTopKelvin = 263.0;   // -10 C
	constexpr double CloudMiddleTopKelvin = 248.0;    // -25 C
	constexpr double CloudCirrusBaseKelvin = 233.0;   // -40 C

	/// Where the temperature stops falling. Earth's stratosphere starts near
	/// 217 K and the same number is used here, because what sets it is ozone
	/// absorbing sunlight and the profile above it is not modelled.
	constexpr double CloudTropopauseKelvin = 217.0;

	double CloudRandom(uint32 Seed, int32 Index, int32 Stream)
	{
		uint64 X = static_cast<uint64>(Seed) * 0x9E3779B97F4A7C15ull
			^ static_cast<uint64>(static_cast<uint32>(Index)) * 0xBF58476D1CE4E5B9ull
			^ static_cast<uint64>(static_cast<uint32>(Stream)) * 0x94D049BB133111EBull;
		X ^= X >> 30; X *= 0xBF58476D1CE4E5B9ull;
		X ^= X >> 27; X *= 0x94D049BB133111EBull;
		X ^= X >> 31;
		return static_cast<double>(X >> 11) / 9007199254740992.0;
	}
}

namespace LedgerCloud
{
	double EnvironmentalLapseRate(const FLedgerAirProfile& Air)
	{
		// Two thirds of the dry adiabat, which is Earth's 6.5 against 9.8. What
		// makes up the difference is latent heat: rising air that condenses
		// gives its heat back to the column, so the column cools more slowly
		// than a parcel that stays dry.
		//
		// On a world with nothing to condense there is nothing to release, and
		// the environment really does follow the dry rate.
		return Air.LapseRateKelvinPerMetre
			* (Air.bHasClouds ? 0.665 : 1.0);
	}

	double HeightOfTemperature(const FLedgerAirProfile& Air, double Kelvin)
	{
		const double Lapse = EnvironmentalLapseRate(Air);
		if (!(Lapse > 0.0))
		{
			return 0.0;
		}
		return (Air.SurfaceTemperatureKelvin - Kelvin) / Lapse;
	}

	FLedgerCloudDecks DecksAt(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double SecondsFromEpoch)
	{
		FLedgerCloudDecks Out;
		if (!Air.HasAir() || !System.Bodies.IsValidIndex(BodyIndex))
		{
			return Out;
		}

		Out.TropopauseMetres = FMath::Max(
			HeightOfTemperature(Air, CloudTropopauseKelvin), 0.0);

		// **The cumulus base is the lifting condensation level**, and it uses
		// the DRY rate: the parcel rising off the ground has not condensed
		// anything yet, which is the whole reason it has a base at all.
		const double Depression = LedgerFog::DewPointDepressionKelvin();
		const double Lifting = Air.LapseRateKelvinPerMetre > 0.0
			? Depression / Air.LapseRateKelvinPerMetre : 0.0;

		// How much sky each deck covers, and how solid it is. The weather field
		// decides: a deep low is overcast and a ridge is clear, which is why a
		// coverage number is a query and not a setting.
		const double Pressure = LedgerWeather::PressureAt(
			System, BodyIndex, Air, LatitudeRadians, LongitudeRadians,
			SecondsFromEpoch);
		// A thousand pascals below the local mean is a solid overcast; a
		// thousand above is a clear ridge.
		const double Anomaly = Pressure - Air.SurfacePressurePascals;
		const double Wetness = FMath::Clamp(0.5 - Anomaly / 4000.0, 0.0, 1.0);

		const double Base = FMath::Max(Lifting, 100.0);
		const double CumulusTop = FMath::Max(
			HeightOfTemperature(Air, CloudCumulusTopKelvin), Base * 1.4);
		const double MiddleTop = FMath::Max(
			HeightOfTemperature(Air, CloudMiddleTopKelvin), CumulusTop * 1.2);
		const double CirrusBase = HeightOfTemperature(Air, CloudCirrusBaseKelvin);

		// Water clouds need water. The rule is T089's, so a world cannot have a
		// cumulus deck without the conditions that give it a fog and a rain.
		if (Air.bHasClouds)
		{
			Out.Cumulus.bPresent = true;
			Out.Cumulus.BaseMetres = Base;
			Out.Cumulus.TopMetres = CumulusTop;
			// **Broken, not overcast.** A deck that always covers the whole sky
			// hides everything above it -- which is exactly what stopped a moon
			// being photographed in T088. Fair weather is a third of the sky.
			Out.Cumulus.Coverage = FMath::Clamp(0.15 + 0.6 * Wetness, 0.0, 0.9);
			Out.Cumulus.Opacity = 0.9;

			Out.Middle.bPresent = Wetness > 0.35;
			Out.Middle.BaseMetres = CumulusTop;
			Out.Middle.TopMetres = MiddleTop;
			Out.Middle.Coverage = FMath::Clamp((Wetness - 0.35) * 1.2, 0.0, 0.8);
			Out.Middle.Opacity = 0.6;

			// Cirrus only if the air gets cold enough before it stops cooling.
			// A hot thin atmosphere never reaches minus forty below its
			// tropopause and simply has no high cloud.
			Out.Cirrus.bPresent =
				CirrusBase > 0.0 && CirrusBase < Out.TropopauseMetres;
			Out.Cirrus.BaseMetres = CirrusBase;
			Out.Cirrus.TopMetres = FMath::Max(
				Out.TropopauseMetres, CirrusBase * 1.15);
			// Cirrus is fickle and mostly independent of the surface weather,
			// so it gets its own seeded coverage rather than following the low.
			Out.Cirrus.Coverage = 0.15 + 0.45 * CloudRandom(
				System.Seed, BodyIndex, 771
					+ static_cast<int32>(SecondsFromEpoch / 43200.0));
			// **Thin, because cirrus is.** Ice cloud has an optical depth of about
			// one through its whole thickness, and this deck is two and a half
			// kilometres of it: at 0.25 of the material's 0.04 per metre it came to
			// twenty-five, and the climb photographed the inside of a brown wall and
			// the underside of an opaque lid. 0.03 was about 3 where fully covered.
			//
			// **0.01, because 3 was never the target** (T445). The paragraph above
			// names the physics -- an optical depth of about one -- and then records
			// landing at three, which is roughly 95% extinction: overcast, not
			// cirrus. A 43%-coverage sheet of that lying above everything is what
			// drained the colour out of the planet seen from orbit. Isolating the
			// deck (-cirruscover=0) changed 38.18% of the disc against a 0.47%
			// noise floor; the owner's report was that planets looked "almost low
			// poly, animated", and this is the half of it that reads as the ground
			// being wrong rather than as weather.
			//
			// Measured both ways before changing it, because a sky is not only seen
			// from space. Orbital wash against a no-cirrus reference: 14.95 mean
			// tint at 0.03, 11.00 at 0.01 -- the heavy filtering (>32) halves, 16.4%
			// to 7.7%. From the ground the deck stays plainly a deck: upper-sky
			// disturbance 7.82 mean with 2.9% of pixels over 32, against 18.17 and
			// 20.7% for removing cirrus altogether. Thinner still (0.005) reached
			// 8.16 from orbit but cost 11.78 / 17.2% on the ground -- most of the
			// way to deleting it -- which is why this stops at the physics value.
			//
			// Coverage is the stronger lever and is NOT touched here: trimming it to
			// 0.25 reaches 2.72. That is a weather change -- fewer high clouds on
			// this world, always -- and it belongs to the owner, not to a rendering
			// complaint. Its numbers are in T445 if it is ever wanted.
			Out.Cirrus.Opacity = 0.01;
		}

		// **Each deck drifts with the wind at its own height.** That is why a
		// cirrus streak and the cumulus under it go different ways, and it is
		// the most legible thing about a sky with more than one layer in it.
		FLedgerCloudDeck* Decks[] = { &Out.Cumulus, &Out.Middle, &Out.Cirrus };
		for (FLedgerCloudDeck* Deck : Decks)
		{
			if (!Deck->bPresent)
			{
				continue;
			}
			const double Middle = (Deck->BaseMetres + Deck->TopMetres) * 0.5;
			Deck->DriftMetresPerSecond = LedgerWeather::WindAtAltitude(
				System, BodyIndex, Air, LatitudeRadians, LongitudeRadians,
				Middle, SecondsFromEpoch);

			Out.LowestMetres = Out.LowestMetres > 0.0
				? FMath::Min(Out.LowestMetres, Deck->BaseMetres)
				: Deck->BaseMetres;
			Out.HighestMetres = FMath::Max(Out.HighestMetres, Deck->TopMetres);
		}
		return Out;
	}
}
