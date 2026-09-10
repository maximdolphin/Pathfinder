#include "LedgerClimate.h"

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

	double SeasonalOffsetC(const FVector3d& UnitSphere, double SeasonPhase)
	{
		// Odd in the sine of latitude, so the two hemispheres are opposite
		// without a rule anybody has to remember, and zero at the equator
		// because a tropical year is not a sequence of seasons.
		return SeasonalSwingC * SinLatitude(UnitSphere)
			* FMath::Sin(2.0 * PI * SeasonPhase);
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
			+ SeasonalOffsetC(UnitSphere, SeasonPhase);

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
		const FVector3d Wind = PrevailingWind(UnitSphere);
		const double ArcPerStep =
			(UpwindFetchMetres / static_cast<double>(UpwindSteps)) / (Params.Radius / 100.0);

		// Walk out against the wind first, then come back with it.
		FVector3d Sample = UnitSphere;
		for (int32 Step = 0; Step < UpwindSteps; ++Step)
		{
			Sample = StepAlong(Sample, -Wind, ArcPerStep);
		}

		double Moisture = 0.5;
		double PreviousAltitude = AltitudeMetres(Sample, Params);
		for (int32 Step = 0; Step < UpwindSteps; ++Step)
		{
			Sample = StepAlong(Sample, Wind, ArcPerStep);
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
