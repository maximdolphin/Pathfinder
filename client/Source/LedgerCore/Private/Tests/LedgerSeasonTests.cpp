// Axial tilt, and what it does to a year. T073.
//
// Half of the acceptance is here: a high-latitude site has a measurably shorter
// day in winter. The other half -- the snow line moving -- belongs to the
// climate field, which is where the insolation this computes ends up.

#include "LedgerSky.h"
#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FVector3d SeasonAnchor(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat));
	}

	/// How long the sun is actually up, by asking. Sampled across one solar day
	/// centred on noon and counting the lit fraction.
	double SeasonMeasuredDaylight(const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& Anchor, double Around)
	{
		const double Day = LedgerSky::SolarDaySeconds(System, BodyIndex, Anchor, Around);
		const double Noon = LedgerSky::NextLocalNoon(System, BodyIndex, Anchor, Around);
		constexpr int32 Samples = 20000;
		int32 Lit = 0;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const double At = Noon - Day * 0.5 + Day * Index / Samples;
			if (LedgerSky::SolarAltitude(System, BodyIndex, Anchor, At) > 0.0)
			{
				++Lit;
			}
		}
		return Day * Lit / Samples;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSeasonDayLength,
	"Ledger.Season.AHighLatitudeDayIsShorterInWinter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSeasonDayLength::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const FLedgerBody& Body = System.Bodies[Planet];
	const double Year = LedgerEphemeris::PeriodSeconds(
		System.Bodies[0].MassKg, Body.Orbit.SemiMajorAxisMetres);
	const double Tilt = FMath::RadiansToDegrees(Body.AxialTiltRadians);

	// Phase 0 is maximum northern declination, so 0.5 is the other solstice.
	// Solved for rather than assumed, because the epoch is not a solstice.
	double SummerAt = 0.0;
	double WinterAt = 0.0;
	{
		constexpr int32 Samples = 2000;
		double BestPhase = 1.0;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const double At = Year * Index / Samples;
			const double Phase = LedgerSky::SeasonPhase(System, Planet, At);
			if (Phase < BestPhase) { BestPhase = Phase; SummerAt = At; }
		}
		WinterAt = SummerAt + Year * 0.5;
	}

	AddInfo(FString::Printf(
		TEXT("axial tilt %.2f degrees, year %.0f s, solar day %.0f s"),
		Tilt, Year, LedgerSky::SolarDaySeconds(System, Planet, SeasonAnchor(60.0, 0.0), 0.0)));
	AddInfo(FString::Printf(
		TEXT("declination at the two solstices: %+.2f and %+.2f degrees"),
		FMath::RadiansToDegrees(LedgerSky::SolarDeclination(System, Planet, SummerAt)),
		FMath::RadiansToDegrees(LedgerSky::SolarDeclination(System, Planet, WinterAt))));

	// The declination has to actually swing, or nothing below means anything.
	const double Swing =
		FMath::RadiansToDegrees(LedgerSky::SolarDeclination(System, Planet, SummerAt)
			- LedgerSky::SolarDeclination(System, Planet, WinterAt));
	TestTrue(*FString::Printf(
		TEXT("declination swings about twice the tilt (%.2f degrees against a tilt of %.2f)"),
		Swing, Tilt),
		Swing > Tilt * 1.5 && Swing < Tilt * 2.5);

	const double Latitudes[] = { 0.0, 15.0, 40.0, 60.0, 75.0 };
	double WorstFormulaError = 0.0;

	for (const double Latitude : Latitudes)
	{
		const FVector3d Anchor = SeasonAnchor(Latitude, 23.0);

		const double Summer = LedgerSky::DayLengthSeconds(System, Planet, Anchor, SummerAt);
		const double Winter = LedgerSky::DayLengthSeconds(System, Planet, Anchor, WinterAt);
		const double Day = LedgerSky::SolarDaySeconds(System, Planet, Anchor, SummerAt);

		// And the same question asked a completely different way: sample the
		// altitude across the day and count. The closed form and the count have
		// nothing in common but the answer.
		const double MeasuredSummer =
			SeasonMeasuredDaylight(System, Planet, Anchor, SummerAt);
		const double MeasuredWinter =
			SeasonMeasuredDaylight(System, Planet, Anchor, WinterAt);
		WorstFormulaError = FMath::Max(WorstFormulaError,
			FMath::Max(FMath::Abs(Summer - MeasuredSummer),
				FMath::Abs(Winter - MeasuredWinter)) / Day);

		AddInfo(FString::Printf(
			TEXT("latitude %4.1f: summer %.2f h, winter %.2f h, difference %+.2f h "
				 "(sampled %.2f and %.2f)"),
			Latitude, Summer / 3600.0, Winter / 3600.0, (Summer - Winter) / 3600.0,
			MeasuredSummer / 3600.0, MeasuredWinter / 3600.0));

		if (Latitude >= 40.0)
		{
			// The acceptance. An hour is "measurable" by any standard -- at 60
			// degrees on Earth the swing is nearer nine.
			TestTrue(*FString::Printf(
				TEXT("latitude %.0f has a shorter day in winter (%.2f h against %.2f h)"),
				Latitude, Winter / 3600.0, Summer / 3600.0),
				Summer - Winter > 3600.0);
		}
		if (Latitude == 0.0)
		{
			// And the equator does not, which is what makes it a season rather
			// than a global brightening. Twelve hours either way, always.
			TestTrue(*FString::Printf(
				TEXT("the equator barely changes (%.3f h difference)"),
				FMath::Abs(Summer - Winter) / 3600.0),
				FMath::Abs(Summer - Winter) < Day * 0.02);
		}
	}

	AddInfo(FString::Printf(
		TEXT("closed form against sampling: worst %.4f of a day"), WorstFormulaError));
	TestTrue(*FString::Printf(
		TEXT("the formula agrees with counting sunlit samples (worst %.4f of a day)"),
		WorstFormulaError),
		WorstFormulaError < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSeasonInsolation,
	"Ledger.Season.InsolationFollowsTheTilt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSeasonInsolation::RunTest(const FString&)
{
	// Insolation is what the climate field will read, so it is worth checking
	// it behaves like sunlight rather than like a number that happens to cycle.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const double Year = LedgerEphemeris::PeriodSeconds(
		System.Bodies[0].MassKg, System.Bodies[Planet].Orbit.SemiMajorAxisMetres);

	double SummerAt = 0.0;
	{
		constexpr int32 Samples = 2000;
		double BestPhase = 1.0;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const double At = Year * Index / Samples;
			const double Phase = LedgerSky::SeasonPhase(System, Planet, At);
			if (Phase < BestPhase) { BestPhase = Phase; SummerAt = At; }
		}
	}
	const double WinterAt = SummerAt + Year * 0.5;

	const FVector3d North = SeasonAnchor(65.0, 0.0);
	const FVector3d South = SeasonAnchor(-65.0, 0.0);
	const FVector3d Equator = SeasonAnchor(0.0, 0.0);

	const double NorthSummer = LedgerSky::DailyInsolation(System, Planet, North, SummerAt);
	const double NorthWinter = LedgerSky::DailyInsolation(System, Planet, North, WinterAt);
	const double SouthSummer = LedgerSky::DailyInsolation(System, Planet, South, SummerAt);
	const double SouthWinter = LedgerSky::DailyInsolation(System, Planet, South, WinterAt);
	const double EquatorSummer = LedgerSky::DailyInsolation(System, Planet, Equator, SummerAt);
	const double EquatorWinter = LedgerSky::DailyInsolation(System, Planet, Equator, WinterAt);

	AddInfo(FString::Printf(
		TEXT("insolation at +65: %.4f summer, %.4f winter"), NorthSummer, NorthWinter));
	AddInfo(FString::Printf(
		TEXT("insolation at -65: %.4f summer, %.4f winter"), SouthSummer, SouthWinter));
	AddInfo(FString::Printf(
		TEXT("insolation at the equator: %.4f and %.4f"), EquatorSummer, EquatorWinter));

	TestTrue(TEXT("a high northern latitude gets more sun at the northern solstice"),
		NorthSummer > NorthWinter * 2.0);

	// The hemispheres are opposite, which is the part a hand-written seasonal
	// curve gets wrong by being the same everywhere.
	TestTrue(TEXT("the southern hemisphere has the opposite season"),
		SouthWinter > SouthSummer * 2.0);

	// The equator has a year too, but a small one, and it is not the same shape.
	TestTrue(*FString::Printf(
		TEXT("the equator varies far less than the poles (%.4f against %.4f)"),
		FMath::Abs(EquatorSummer - EquatorWinter), FMath::Abs(NorthSummer - NorthWinter)),
		FMath::Abs(EquatorSummer - EquatorWinter)
			< FMath::Abs(NorthSummer - NorthWinter) * 0.5);

	// Never negative, never above one: it is a fraction of overhead sun.
	for (double Phase = 0.0; Phase < 1.0; Phase += 0.05)
	{
		for (double Latitude = -90.0; Latitude <= 90.0; Latitude += 10.0)
		{
			const double Value = LedgerSky::DailyInsolation(
				System, Planet, SeasonAnchor(Latitude, 0.0), SummerAt + Year * Phase);
			TestTrue(*FString::Printf(
				TEXT("insolation stays a fraction (%.4f at latitude %.0f, phase %.2f)"),
				Value, Latitude, Phase),
				Value >= 0.0 && Value <= 1.0);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
