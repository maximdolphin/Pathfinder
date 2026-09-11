#include "LedgerClimate.h"
#include "LedgerMath.h"
#include "LedgerSky.h"

namespace
{
	/// Sine of latitude. The planet's axis is Z, so this is just the component.
	double SinLatitude(const FVector3d& UnitSphere)
	{
		return FMath::Clamp(UnitSphere.Z, -1.0, 1.0);
	}

	/// Metres above sea level. Sea level is the reference sphere, which is what
	/// the water sections are generated at, so the elevation function's zero is
	/// already the waterline.
	double AltitudeMetres(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params)
	{
		return LedgerTerrain::Elevation(UnitSphere, Params) / 100.0;
	}

	/// Rotates a point on the sphere along a tangent direction by an arc.
	///
	/// Great-circle, not a linear offset then renormalise. Over 25 km on a
	/// 6,371 km sphere the difference is small, but it accumulates over sixteen
	/// steps and the whole point of the march is that the last step is 400 km
	/// away and still on the sphere.
	FVector3d StepAlong(const FVector3d& From, const FVector3d& Tangent, double ArcRadians)
	{
		return (From * FMath::Cos(ArcRadians) + Tangent * FMath::Sin(ArcRadians)).GetSafeNormal();
	}
}

namespace LedgerClimate
{
	FVector3d PrevailingWind(const FVector3d& UnitSphere)
	{
		// East, as a tangent: the direction of rotation at this point.
		const FVector3d Axis(0.0, 0.0, 1.0);
		FVector3d East = FVector3d::CrossProduct(Axis, UnitSphere);
		if (East.IsNearlyZero())
		{
			// Directly over a pole. Any tangent will do and none of them is
			// meaningfully "east", so pick one rather than returning a zero
			// vector that would make the march stand still.
			East = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), UnitSphere);
		}
		East.Normalize();

		const double LatitudeDegrees = FMath::RadiansToDegrees(FMath::Asin(SinLatitude(UnitSphere)));
		const double Absolute = FMath::Abs(LatitudeDegrees);

		// Trade easterlies, mid-latitude westerlies, polar easterlies. The
		// blends across the boundaries are deliberate: a hard switch puts a
		// discontinuity in the moisture field at 30 degrees, and a biome map
		// read from a discontinuous field has a seam running round the planet.
		double Eastward = 1.0;
		if (Absolute > 30.0 && Absolute <= 60.0)
		{
			Eastward = -1.0;
		}
		if (Absolute > 25.0 && Absolute <= 35.0)
		{
			Eastward = FMath::Lerp(1.0, -1.0, (Absolute - 25.0) / 10.0);
		}
		else if (Absolute > 55.0 && Absolute <= 65.0)
		{
			Eastward = FMath::Lerp(-1.0, 1.0, (Absolute - 55.0) / 10.0);
		}

		// Wind blows *from* here toward there; the march walks the other way.
		return (East * Eastward).GetSafeNormal();
	}

	double SeasonalOffsetC(
		const FVector3d& UnitSphere, double SeasonPhase, double TiltRadians)
	{
		// **Insolation, not a drawn curve.** T073.
		//
		// This used to be `SeasonalSwingC * sin(latitude) * sin(2 pi phase)`,
		// which has the right shape and no cause: it produced seasons on a
		// planet with no tilt, and identical seasons on planets tilted 10
		// degrees and 40. Now it is the difference between how much sun this
		// latitude gets today and how much it gets averaged over the year, and
		// the tilt is the only thing that makes those differ.
		//
		// The two agree closely where the old one was calibrated, which is why
		// the swap does not move the biomes: at 65 degrees the old curve gave
		// +18.1 C at the solstice and this gives +18.2. At the pole both give
		// the full +/- 20. Below the tropics they diverge, and this one is
		// right -- the equator is very slightly *cooler* at a solstice, because
		// the star has moved off it.
		//
		// The declination is modelled as tilt * sin(2 pi phase) rather than
		// read from the ephemeris. Eccentricity makes the real one asymmetric
		// by a few per cent, which is a smaller error than the one-value-per-run
		// season already carries, and it keeps the phase as the single knob
		// every existing caller already holds.
		const double Declination = TiltRadians * FMath::Sin(LedgerTwoPi * SeasonPhase);
		const double Latitude =
			FMath::Asin(FMath::Clamp(SinLatitude(UnitSphere), -1.0, 1.0));

		const double Today = LedgerSky::DailyInsolationAt(Latitude, Declination);

		// The year's mean, integrated. **The midpoint of the two solstices is
		// not it, and the pole is where that shows.** There the sun is on the
		// horizon at an equinox and below it all winter, so insolation is zero
		// at both -- the solstice midpoint reads half the summer value, and an
		// equinox came out 20 C below its own annual mean. Eight samples of the
		// real year cost eight sines against a climate that already marches
		// forty steps upwind resampling the height field for every point.
		constexpr int32 YearSamples = 8;
		double Mean = 0.0;
		for (int32 Step = 0; Step < YearSamples; ++Step)
		{
			Mean += LedgerSky::DailyInsolationAt(Latitude,
				TiltRadians * FMath::Sin(LedgerTwoPi * Step / YearSamples));
		}
		Mean /= YearSamples;

		// Normalised by the largest anomaly a REFERENCE planet would see, which
		// is at its pole: there the summer mean is sin(tilt) and the winter
		// mean is zero, so half of sin(tilt) is the amplitude.
		//
		// The reference, not this body's own tilt. Dividing by its own tilt
		// divides out the season: a 5 degree lean and a 35 degree lean would
		// come out identical, which is precisely the failure the drawn sine
		// had. Ledger.Snow.APlanetWithNoTiltHasNoSeasons is where that shows.
		// Computed rather than written down: a derived constant sitting beside
		// the thing it is derived from is a constant waiting to disagree with
		// it. One more sine, against the three already here.
		// The reference planet's polar summer anomaly: its pole sees sin(tilt)
		// at the solstice against an annual mean of tilt/pi, and the difference
		// is what SeasonalSwingC is quoted against.
		const double Amplitude = FMath::Sin(ReferenceTiltRadians)
			- ReferenceTiltRadians / LedgerPi;
		return SeasonalSwingC * (Today - Mean) / Amplitude;
	}

	double SnowCover(const FLedgerClimate& Climate)
	{
		// Ramped over four degrees rather than switched at zero: a snow line is
		// a band a few hundred metres deep, not a contour, and a hard threshold
		// would draw one on the ground.
		const double Cold = FMath::Clamp(-Climate.TemperatureC / 4.0, 0.0, 1.0);

		// And there has to be something to fall. Fifteen per cent humidity is
		// where the driest places that still hold snow sit; below it the ground
		// is cold rock.
		const double Wet = FMath::Clamp(Climate.Moisture / 0.15, 0.0, 1.0);
		return Cold * Wet;
	}

	FLedgerClimate At(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params,
		double SeasonPhase)
	{
		FLedgerClimate Climate;

		const double SinLat = SinLatitude(UnitSphere);
		// sin^2 rather than |sin|: it is monotonic in |latitude| either way, and
		// the square puts the steep part of the gradient in the mid latitudes
		// where the real one is, instead of spreading it evenly.
		Climate.SeaLevelTemperatureC = FMath::Lerp(EquatorC, PoleC, SinLat * SinLat)
			+ SeasonalOffsetC(UnitSphere, SeasonPhase, Params.AxialTiltRadians);

		// An airless body has no water cycle at all: no ocean to evaporate
		// from, no wind to carry it, no snow to fall. Answered before the
		// upwind march rather than after it, because marching forty steps to
		// arrive at zero is forty steps wasted on every climate sample.
		if (!Params.bHasAtmosphere)
		{
			Climate.AltitudeMetres = AltitudeMetres(UnitSphere, Params);
			const double AboveDatum = FMath::Max(0.0, Climate.AltitudeMetres);
			Climate.TemperatureC = Climate.SeaLevelTemperatureC
				- LapseRateCPerKm * (AboveDatum / 1000.0);
			Climate.Moisture = 0.0;
			return Climate;
		}

		Climate.AltitudeMetres = AltitudeMetres(UnitSphere, Params);
		const double AboveWater = FMath::Max(0.0, Climate.AltitudeMetres);
		Climate.TemperatureC = Climate.SeaLevelTemperatureC
			- LapseRateCPerKm * (AboveWater / 1000.0);

		// ---- moisture, marched upwind ------------------------------------
		//
		// Start at the far end of the fetch and walk downwind to the point,
		// picking up water over ocean and dropping it wherever the ground
		// rises. That is what a rain shadow is: the range takes the moisture
		// out on the way up and there is none left on the far side.
		// **The local wind at every step, not the wind at the start.** The
		// march used to take the direction once and walk a great circle along
		// it; a band wind blows along the parallel, and at 70 degrees a few
		// hundred kilometres of great circle leave the parallel far behind --
		// the air it sampled never passed over the range. 29 of the 40 ranges
		// wetter on the lee side lay poleward of 60. Where the band wind turns
		// through zero the march stands still, which is what no fetch means.
		const double ArcPerStep =
			(UpwindFetchMetres / static_cast<double>(UpwindSteps)) / (Params.Radius / 100.0);

		// Walk out against the wind, then come back along the SAME path.
		//
		// It used to come back by stepping with the wind at each sample, which
		// retraces the way out only where the wind does not turn. Where it does --
		// near the poles, where east swings round in a few steps, and at a band
		// edge -- the walk back ended somewhere else, and the moisture reported
		// for this point was the air arriving at another one. The ranges whose lee
		// read wetter than their windward side sat at 80-88 and 18-26 degrees.
		FVector3d Path[UpwindSteps + 1];
		Path[0] = UnitSphere;
		for (int32 Step = 0; Step < UpwindSteps; ++Step)
		{
			Path[Step + 1] = StepAlong(Path[Step], -PrevailingWind(Path[Step]), ArcPerStep);
		}

		double Moisture = 0.5;
		double PreviousAltitude = AltitudeMetres(Path[UpwindSteps], Params);
		for (int32 Step = UpwindSteps - 1; Step >= 0; --Step)
		{
			const FVector3d& Sample = Path[Step];
			const double Altitude = AltitudeMetres(Sample, Params);

			if (Altitude <= 0.0)
			{
				// Over water. Evaporation refills the air toward saturation,
				// and a long enough fetch saturates it completely.
				Moisture = FMath::Lerp(Moisture, 1.0, 0.35);
			}
			else
			{
				// Over land it dries out slowly whatever the terrain does.
				//
				// 0.99, not 0.97, and the difference is the whole rain-shadow
				// acceptance. Over the forty steps of the march, 0.97 costs
				// 70% of the moisture before any mountain is involved; 0.99
				// costs 33%. At 0.97 the background decay was doing more work
				// than the terrain, which is backwards for a model whose
				// acceptance is about terrain: half the land came out desert,
				// and a lee side cannot be shown to be drier than a windward
				// side when both are already on the floor. 609 ranges, 296
				// shadowed, 58% against a bar of 75%.
				//
				// Continental interiors being drier than coasts is real. Being
				// uniformly at zero is not, and it is what hid the effect this
				// task exists to demonstrate.
				Moisture *= 0.99;

				// Orographic lift. Rising ground condenses what it lifts, and
				// the loss is exponential in the rise so a 2,000 m range takes
				// far more than twice what a 1,000 m one does.
				const double Rise = Altitude - PreviousAltitude;
				if (Rise > 0.0)
				{
					Moisture *= FMath::Exp(-Rise / 900.0);
				}
			}

			PreviousAltitude = Altitude;
		}

		Climate.Moisture = FMath::Clamp(Moisture, 0.0, 1.0);
		return Climate;
	}
}
