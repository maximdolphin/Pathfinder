// Noon, at any latitude and any date. T072.
//
// The acceptance is that local noon -- the moment the body has turned this place
// to face the star, which is arithmetic on the rotation and the orbit -- is when
// the sun is at its highest. Those are two different computations of the same
// instant, and the test is that they agree.

#include "LedgerSky.h"
#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// An anchor at a latitude and longitude, in the body frame.
	FVector3d SkyAnchor(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat));
	}

	/// The highest the sun gets on the day around a time, and when.
	///
	/// A scan to bracket it and a ternary search to sharpen it. The scan is
	/// there because altitude is unimodal over a day and not over two, so a
	/// search started on the wrong side of midnight would find a minimum with
	/// great precision.
	double SkyDaysBest(const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& Anchor, double Around, double DaySeconds, double& OutWhen)
	{
		constexpr int32 Samples = 2000;
		const double Step = DaySeconds / Samples;
		double BestAt = Around;
		double Best = -10.0;
		for (int32 Index = 0; Index <= Samples; ++Index)
		{
			const double At = Around - DaySeconds * 0.5 + Index * Step;
			const double Altitude = LedgerSky::SolarAltitude(System, BodyIndex, Anchor, At);
			if (Altitude > Best)
			{
				Best = Altitude;
				BestAt = At;
			}
		}

		double Low = BestAt - Step;
		double High = BestAt + Step;
		for (int32 Iteration = 0; Iteration < 200; ++Iteration)
		{
			const double A = Low + (High - Low) / 3.0;
			const double B = High - (High - Low) / 3.0;
			if (LedgerSky::SolarAltitude(System, BodyIndex, Anchor, A)
				< LedgerSky::SolarAltitude(System, BodyIndex, Anchor, B))
			{
				Low = A;
			}
			else
			{
				High = B;
			}
		}
		OutWhen = (Low + High) * 0.5;
		return LedgerSky::SolarAltitude(System, BodyIndex, Anchor, OutWhen);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSkyNoon,
	"Ledger.Sky.LocalNoonIsWhenTheSunIsHighest",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSkyNoon::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);

	// The planet and its moon. The moon is here because its sun direction is
	// the star minus the MOON, not the star minus the planet, and a version
	// that took the parent's would still pass at the planet.
	const int32 Bodies[] = { 1, 2 };

	// Off the exact poles, where the hour angle is undefined -- the anchor has
	// no azimuth there and the altitude barely moves all day. That is a
	// property of a pole rather than a gap in the test, and 85 degrees is close
	// enough to it to be where a mistake would show.
	const double Latitudes[] = { -85.0, -66.5, -40.0, -10.0, 0.0, 23.5, 51.5, 72.0, 85.0 };
	const double Longitudes[] = { 0.0, 97.0, 231.0 };

	double WorstAltitudeGap = 0.0;
	double WorstTimeOffset = 0.0;
	double WorstTimeOffsetFraction = 0.0;
	double WorstHourAngle = 0.0;
	FString WorstTimeWhere;

	for (const int32 BodyIndex : Bodies)
	{
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			continue;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];
		const double Day = FMath::Abs(Body.RotationPeriodSeconds);
		if (!(Day > 0.0))
		{
			continue;
		}

		// Dates across a year, so the declination is somewhere different every
		// time. Solstices are where the sun is furthest from the equator, which
		// is where a sign error in the tilt shows and an equinox hides it.
		const double Year = LedgerEphemeris::PeriodSeconds(
			System.Bodies[0].MassKg, System.Bodies[1].Orbit.SemiMajorAxisMetres);
		const double Dates[] = { 0.0, Year * 0.25, Year * 0.5, Year * 0.75, Year * 7.3 };

		for (const double Date : Dates)
		{
			for (const double Latitude : Latitudes)
			{
				for (const double Longitude : Longitudes)
				{
					const FVector3d Anchor = SkyAnchor(Latitude, Longitude);

					const double Noon =
						LedgerSky::NextLocalNoon(System, BodyIndex, Anchor, Date);
					const double AtNoon =
						LedgerSky::SolarAltitude(System, BodyIndex, Anchor, Noon);

					double HighestAt = 0.0;
					const double Highest =
						SkyDaysBest(System, BodyIndex, Anchor, Noon, Day, HighestAt);

					// The claim: at noon the sun is at its highest.
					const double Gap = Highest - AtNoon;
					WorstAltitudeGap = FMath::Max(WorstAltitudeGap, Gap);

					// And that the solver really put the hour angle at zero,
					// rather than agreeing by both being wrong.
					const double H = FMath::Abs(
						LedgerSky::HourAngle(System, BodyIndex, Anchor, Noon));
					WorstHourAngle = FMath::Max(WorstHourAngle, H);

					const double Offset = FMath::Abs(HighestAt - Noon);
					WorstTimeOffsetFraction =
						FMath::Max(WorstTimeOffsetFraction, Offset / Day);
					if (Offset > WorstTimeOffset)
					{
						WorstTimeOffset = Offset;
						WorstTimeWhere = FString::Printf(
							TEXT("body %d, latitude %.1f, longitude %.0f"),
							BodyIndex, Latitude, Longitude);
					}
				}
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("worst altitude below the peak at noon: %.3e rad (%.2e degrees)"),
		WorstAltitudeGap, FMath::RadiansToDegrees(WorstAltitudeGap)));
	AddInfo(FString::Printf(TEXT("worst hour angle at the solved noon: %.3e rad"),
		WorstHourAngle));

	AddInfo(FString::Printf(
		TEXT("gap between noon and the altitude peak: worst %.1f s -- %s, "
			 "which is %.3f%% of that day"),
		WorstTimeOffset, *WorstTimeWhere, WorstTimeOffsetFraction * 100.0));

	// **The peak is not exactly at the meridian crossing, and that is physics.**
	//
	// The star keeps moving in declination while the body turns, so the
	// altitude is still climbing slightly as the sun crosses the meridian. The
	// displacement goes as the declination rate over the SQUARE of the rotation
	// rate, and the first run of this test found the consequence: 7054 s at
	// latitude 85 on the moon, whose rotation is tidally locked to its month.
	// Slow rotation squared is what makes that two hours rather than the
	// planet's few seconds, and near a pole cos(latitude) is small, which buys
	// the same drift a bigger slice of the day.
	//
	// So the bound is on the thing the acceptance is actually about -- whether
	// the sun is at its highest -- and not on the clock. 1e-4 rad is 20
	// arcseconds, about one per cent of the width of the sun: at noon the star
	// is inside the top hundredth of its own disc. The measured worst is
	// 1.054e-05.
	TestTrue(*FString::Printf(
		TEXT("noon is the highest the sun gets (worst %.3e rad below the peak)"),
		WorstAltitudeGap),
		WorstAltitudeGap < 1e-4);

	// And the clock still gets a bound, as a fraction of the body's own day so
	// that it means the same thing on a planet and on a moon. An hour angle
	// with a sign error or a constant offset in it fails this by a mile; the
	// declination drift uses 0.3% of it.
	TestTrue(*FString::Printf(
		TEXT("noon is within one per cent of a day of the peak (worst %.3f%%)"),
		WorstTimeOffsetFraction * 100.0),
		WorstTimeOffsetFraction < 0.01);

	TestTrue(*FString::Printf(
		TEXT("the solved noon has a zero hour angle (worst %.3e rad)"), WorstHourAngle),
		WorstHourAngle < 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSkyDayAndNight,
	"Ledger.Sky.ADayIsALightHalfAndADarkHalf",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSkyDayAndNight::RunTest(const FString&)
{
	// The other half of "day and night": that there is a night at all, that it
	// comes once per rotation, and that the sun goes round rather than back and
	// forth. A noon test on its own passes on a sun nailed to the meridian.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const FLedgerBody& Body = System.Bodies[Planet];
	const double Day = FMath::Abs(Body.RotationPeriodSeconds);

	const FVector3d Anchor = SkyAnchor(12.0, 40.0);

	constexpr int32 Samples = 4000;
	int32 Lit = 0;
	int32 Crossings = 0;
	double Previous = LedgerSky::SolarAltitude(System, Planet, Anchor, 0.0);
	double Turned = 0.0;
	double PreviousAzimuth = 0.0;
	{
		const FVector3d Sun = LedgerSky::SunDirectionInSurface(System, Planet, Anchor, 0.0);
		PreviousAzimuth = FMath::Atan2(Sun.X, Sun.Y);
	}

	for (int32 Index = 1; Index <= Samples; ++Index)
	{
		const double At = Day * Index / Samples;
		const double Altitude = LedgerSky::SolarAltitude(System, Planet, Anchor, At);
		if (Altitude > 0.0) { ++Lit; }
		if ((Altitude > 0.0) != (Previous > 0.0)) { ++Crossings; }
		Previous = Altitude;

		// Azimuth swept, unwrapped. One full turn per day, and all of it the
		// same way round.
		const FVector3d Sun = LedgerSky::SunDirectionInSurface(System, Planet, Anchor, At);
		const double Azimuth = FMath::Atan2(Sun.X, Sun.Y);
		double Step = Azimuth - PreviousAzimuth;
		if (Step > LedgerPi) { Step -= LedgerTwoPi; }
		if (Step < -LedgerPi) { Step += LedgerTwoPi; }
		Turned += Step;
		PreviousAzimuth = Azimuth;
	}

	const double LitFraction = static_cast<double>(Lit) / Samples;
	AddInfo(FString::Printf(
		TEXT("at latitude 12: %.1f%% of the day is lit, %d horizon crossings, "
			 "azimuth swept %.4f turns"),
		LitFraction * 100.0, Crossings, Turned / LedgerTwoPi));

	// Near the equator a day is close to half light, and the departure from
	// exactly half is the tilt and the date rather than an error.
	TestTrue(*FString::Printf(TEXT("about half the day is lit (%.1f%%)"), LitFraction * 100.0),
		LitFraction > 0.40 && LitFraction < 0.60);
	TestEqual(TEXT("the sun rises once and sets once"), Crossings, 2);
	TestTrue(*FString::Printf(TEXT("the sun goes round exactly once (%.4f turns)"),
		Turned / LedgerTwoPi),
		FMath::IsNearlyEqual(FMath::Abs(Turned / LedgerTwoPi), 1.0, 0.01));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
