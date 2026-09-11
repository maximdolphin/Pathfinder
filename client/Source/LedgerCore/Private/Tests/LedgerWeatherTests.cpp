// A storm tracked for a week, and the same storm again. T092.

#include "LedgerWeather.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "LedgerMath.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// Great-circle distance in kilometres, for reporting a track.
	double WeatherTestStep(double RadiusMetres,
		const FLedgerPressureCell& A, const FLedgerPressureCell& B)
	{
		const double SinLat = FMath::Sin((B.LatitudeRadians - A.LatitudeRadians) * 0.5);
		const double SinLon = FMath::Sin(
			FMath::UnwindRadians(B.LongitudeRadians - A.LongitudeRadians) * 0.5);
		const double H = SinLat * SinLat
			+ FMath::Cos(A.LatitudeRadians) * FMath::Cos(B.LatitudeRadians)
			* SinLon * SinLon;
		return 2.0 * RadiusMetres
			* FMath::Atan2(FMath::Sqrt(H), FMath::Sqrt(FMath::Max(1.0 - H, 0.0)))
			/ 1000.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerWeatherTrack,
	"Ledger.Weather.AStormTrackedForAWeekFollowsACoherentPath",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerWeatherTrack::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);
	const int32 Home = LedgerBodies::HomeIndex(System);
	const FLedgerBody& Body = System.Bodies[Home];

	AddInfo(FString::Printf(
		TEXT("%s turns once in %.1f hours, so its Hadley cell reaches %.1f degrees "
			 "and it has %d circulation cells a hemisphere"),
		*Body.Name, Body.RotationPeriodSeconds / 3600.0,
		FMath::RadiansToDegrees(LedgerWeather::HadleyEdgeRadians(Body)),
		LedgerWeather::CirculationCells(Body)));

	// **Follow one slot for a week and watch what it does.** An hour a sample,
	// which is fine enough that a storm cannot cross a continent between two of
	// them.
	constexpr int32 Hours = 24 * 7;
	constexpr int32 Which = 4;

	TArray<FLedgerPressureCell> Track;
	for (int32 Hour = 0; Hour <= Hours; ++Hour)
	{
		Track.Add(LedgerWeather::CellAt(System, Home, Which, Hour * 3600.0));
	}

	double Longest = 0.0;
	double Total = 0.0;
	int32 Births = 0;
	for (int32 Index = 1; Index < Track.Num(); ++Index)
	{
		const double Step = WeatherTestStep(
			Body.RadiusMetres, Track[Index - 1], Track[Index]);
		// A slot turning over is a new storm somewhere else, and that jump is
		// not a discontinuity in a track -- it is the end of one.
		if (Track[Index].Age < Track[Index - 1].Age)
		{
			++Births;
			continue;
		}
		Longest = FMath::Max(Longest, Step);
		Total += Step;
	}

	AddInfo(FString::Printf(
		TEXT("cell %d over a week: %.0f km travelled, longest hourly step %.1f km, "
			 "%d storms in the slot"),
		Which, Total, Longest, Births + 1));
	AddInfo(FString::Printf(
		TEXT("it starts at %.1f N %.1f E at %.0f%% strength and ends at %.1f N "
			 "%.1f E at %.0f%%"),
		FMath::RadiansToDegrees(Track[0].LatitudeRadians),
		FMath::RadiansToDegrees(Track[0].LongitudeRadians),
		Track[0].Strength * 100.0,
		FMath::RadiansToDegrees(Track.Last().LatitudeRadians),
		FMath::RadiansToDegrees(Track.Last().LongitudeRadians),
		Track.Last().Strength * 100.0));

	// **Coherent means no teleporting.** A storm riding a fifteen-metre-a-second
	// westerly covers 54 km in an hour; a hundred and fifty is generous and a
	// thousand would mean the track is a sequence of unrelated points.
	TestTrue(*FString::Printf(TEXT("no hourly step exceeds 150 km (%.1f)"), Longest),
		Longest < 150.0);
	TestTrue(TEXT("and it actually goes somewhere"), Total > 1000.0);

	// A slot has to turn over during a week, or the population never refreshes.
	TestTrue(TEXT("the slot carries more than one storm in a week"), Births >= 1);

	// **Poleward, because that is what mid-latitude cyclones do.** Within one
	// storm's life the latitude only ever moves away from the equator.
	double WorstEquatorward = 0.0;
	for (int32 Index = 1; Index < Track.Num(); ++Index)
	{
		if (Track[Index].Age < Track[Index - 1].Age)
		{
			continue;
		}
		const double Moved = FMath::Abs(Track[Index].LatitudeRadians)
			- FMath::Abs(Track[Index - 1].LatitudeRadians);
		WorstEquatorward = FMath::Min(WorstEquatorward, Moved);
	}
	AddInfo(FString::Printf(
		TEXT("the worst equatorward step in a life is %.2e radians"),
		WorstEquatorward));
	TestTrue(TEXT("a storm never wanders back towards the equator"),
		WorstEquatorward > -1e-12);

	// **And the same seed reproduces it exactly.** Not nearly: the same double.
	const FLedgerSystem Again = LedgerBodies::Generate(20260910u);
	bool bIdentical = true;
	for (int32 Hour = 0; Hour <= Hours && bIdentical; ++Hour)
	{
		const FLedgerPressureCell Repeat =
			LedgerWeather::CellAt(Again, Home, Which, Hour * 3600.0);
		bIdentical = Repeat.LatitudeRadians == Track[Hour].LatitudeRadians
			&& Repeat.LongitudeRadians == Track[Hour].LongitudeRadians
			&& Repeat.AnomalyPascals == Track[Hour].AnomalyPascals;
	}
	TestTrue(TEXT("the same seed gives the same week, to the bit"), bIdentical);

	// **And asking out of order changes nothing**, which is the property an
	// integrated model cannot have. Day six is day six whether or not day five
	// was asked for first.
	const FLedgerPressureCell Straight =
		LedgerWeather::CellAt(System, Home, Which, 6.0 * 86400.0);
	TestEqual(TEXT("day six asked cold is day six asked in sequence"),
		Straight.LatitudeRadians, Track[6 * 24].LatitudeRadians, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerWeatherBands,
	"Ledger.Weather.HowFastABodyTurnsDecidesHowManyWindBandsItHas",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerWeatherBands::RunTest(const FString&)
{
	// **Held and Hou, checked against the three bodies whose banding everybody
	// knows.** A slow rotator has one enormous cell and no trade winds; Earth
	// has three; a fast one is striped.
	struct FSpin { const TCHAR* Name; double Hours; double Radius; int32 Low; int32 High; const TCHAR* Observed; };
	const FSpin Spins[] =
	{
		{ TEXT("Venus"),   5832.0,  6.052e6, 1, 1, TEXT("one, pole to pole") },
		{ TEXT("Earth"),     23.93, 6.371e6, 3, 3, TEXT("three") },
		{ TEXT("Jupiter"),    9.93, 6.991e7, 4, 7, TEXT("about six jets") },
	};

	for (const FSpin& Spin : Spins)
	{
		FLedgerBody Body;
		Body.RotationPeriodSeconds = Spin.Hours * 3600.0;
		Body.RadiusMetres = Spin.Radius;
		const int32 Cells = LedgerWeather::CirculationCells(Body);
		AddInfo(FString::Printf(
			TEXT("%-8s turns in %8.2f h: Hadley edge %5.1f deg, %d cells "
				 "(observed: %s)"),
			Spin.Name, Spin.Hours,
			FMath::RadiansToDegrees(LedgerWeather::HadleyEdgeRadians(Body)),
			Cells, Spin.Observed));
		// **Earth and Venus are exact and Jupiter is a range**, because that is
		// how well the observations are known. Jupiter's jets are counted
		// differently by different people and the scaling is a single power
		// law, so pinning it to one integer would be pinning the test to a
		// guess rather than to the sky.
		TestTrue(*FString::Printf(TEXT("%s has %d cells, wanted %d to %d"),
			Spin.Name, Cells, Spin.Low, Spin.High),
			Cells >= Spin.Low && Cells <= Spin.High);
	}

	// The bands alternate and the boundaries are calm. That is not decoration:
	// a boundary is where air rises or sinks, and vertical air has no east-west
	// preference.
	FLedgerBody Earth;
	Earth.RotationPeriodSeconds = 86164.0;
	Earth.RadiusMetres = 6.371e6;

	AddInfo(TEXT("latitude   zonal wind"));
	for (int32 Degrees = 0; Degrees <= 90; Degrees += 10)
	{
		AddInfo(FString::Printf(TEXT("%7d    %+6.1f m/s"), Degrees,
			LedgerWeather::ZonalWindAt(
				Earth, FMath::DegreesToRadians(static_cast<double>(Degrees)))));
	}

	const double Trades = LedgerWeather::ZonalWindAt(
		Earth, FMath::DegreesToRadians(15.0));
	const double Westerlies = LedgerWeather::ZonalWindAt(
		Earth, FMath::DegreesToRadians(45.0));
	TestTrue(*FString::Printf(TEXT("the tropics blow easterly (%+.1f)"), Trades),
		Trades < -3.0);
	TestTrue(*FString::Printf(TEXT("the mid latitudes blow westerly (%+.1f)"),
		Westerlies), Westerlies > 3.0);
	TestTrue(TEXT("and the boundary between them is calm"),
		FMath::Abs(LedgerWeather::ZonalWindAt(
			Earth, FMath::DegreesToRadians(30.0))) < 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerWeatherWind,
	"Ledger.Weather.WindRunsAlongTheIsobarsRatherThanDownThem",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerWeatherWind::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);
	const int32 Home = LedgerBodies::HomeIndex(System);
	const FLedgerAirProfile Air = LedgerAir::For(System, Home, 0.0);

	// Find a deep low well out of the tropics, and walk a ring around it.
	TArray<FLedgerPressureCell> Cells;
	LedgerWeather::CellsAt(System, Home, 3.0 * 86400.0, Cells);

	int32 Deepest = INDEX_NONE;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		if (Cells[Index].bLow
			&& FMath::Abs(Cells[Index].LatitudeRadians)
				> FMath::DegreesToRadians(25.0)
			&& (Deepest == INDEX_NONE
				|| Cells[Index].AnomalyPascals < Cells[Deepest].AnomalyPascals))
		{
			Deepest = Index;
		}
	}
	TestTrue(TEXT("there is a low to look at"), Deepest != INDEX_NONE);
	if (Deepest == INDEX_NONE)
	{
		return false;
	}

	const FLedgerPressureCell& Low = Cells[Deepest];
	const FLedgerBody& Body = System.Bodies[Home];
	AddInfo(FString::Printf(
		TEXT("a %.0f Pa low at %.1f N %.1f E, %.0f km across"),
		Low.AnomalyPascals, FMath::RadiansToDegrees(Low.LatitudeRadians),
		FMath::RadiansToDegrees(Low.LongitudeRadians),
		Low.RadiusMetres / 1000.0));

	// **Circulation, not angle.**
	//
	// The first version measured the angle between the wind and the line to the
	// centre and found it 57 degrees off square -- because the prevailing
	// westerly is added on top of the storm's own flow and a thirteen-metre
	// band swamps it locally. That is physically right and it makes the angle
	// the wrong measurement.
	//
	// The line integral of the wind around a closed loop is the right one: a
	// uniform flow through the loop contributes exactly zero to it, however
	// strong, so what is left is the storm's own rotation. Positive is
	// anticlockwise seen from above, which is cyclonic in the north and
	// anticyclonic in the south.
	constexpr int32 Around = 24;
	const double Ring = Low.RadiusMetres / Body.RadiusMetres;
	double Circulation = 0.0;
	double Speed = 0.0;
	for (int32 Step = 0; Step < Around; ++Step)
	{
		const double Bearing = Step * LedgerTwoPi / Around;
		const double Latitude = Low.LatitudeRadians + Ring * FMath::Cos(Bearing);
		const double Longitude = Low.LongitudeRadians
			+ Ring * FMath::Sin(Bearing)
			/ FMath::Max(FMath::Cos(Low.LatitudeRadians), 0.1);

		const FVector2D Wind = LedgerWeather::WindAt(
			System, Home, Air, Latitude, Longitude, 3.0 * 86400.0);
		Speed = FMath::Max(Speed, static_cast<double>(Wind.Size()));

		// **The anticlockwise tangent, and getting it backwards inverts the
		// answer.** The point on the ring is at (east, north) = (sin b, cos b),
		// so the outward radial is (sin b, cos b) and turning that a quarter
		// anticlockwise -- which takes east to north, so (x, y) goes to (-y, x)
		// -- gives (-cos b, sin b). The first version had the sign the other
		// way and reported every storm on the planet spinning backwards, which
		// at least failed loudly.
		const FVector2D Tangent = FVector2D(
			static_cast<float>(-FMath::Cos(Bearing)),
			static_cast<float>(FMath::Sin(Bearing)));
		const double Segment = Low.RadiusMetres * LedgerTwoPi / Around;
		Circulation += FVector2D::DotProduct(Wind, Tangent) * Segment;
	}

	AddInfo(FString::Printf(
		TEXT("circulation around the low is %+.3e m2/s, with winds to %.1f m/s "
			 "on the ring"),
		Circulation, Speed));

	// A low in the southern hemisphere turns clockwise, which is a negative
	// circulation; in the north it is positive. Either way it is the sign of
	// the Coriolis parameter, and getting it backwards would mean every storm
	// on the planet spun the wrong way.
	const double F = LedgerWeather::CoriolisAt(Body, Low.LatitudeRadians);
	AddInfo(FString::Printf(
		TEXT("the Coriolis parameter there is %+.2e /s, so a low should turn %s"),
		F, F > 0.0 ? TEXT("anticlockwise") : TEXT("clockwise")));
	TestTrue(TEXT("the low turns the way Coriolis says it must"),
		(F > 0.0) == (Circulation > 0.0));

	// And it is a real circulation rather than a rounding error: comparable to
	// the wind speed times the circumference.
	const double Scale = Speed * Low.RadiusMetres * LedgerTwoPi;
	AddInfo(FString::Printf(
		TEXT("that is %.0f%% of what a wind of that speed all the way round "
			 "would give"),
		FMath::Abs(Circulation) / FMath::Max(Scale, 1.0) * 100.0));
	TestTrue(TEXT("the circulation is a real fraction of the flow"),
		FMath::Abs(Circulation) > Scale * 0.05);

	// **And a high turns the other way**, which is the check that the sign is
	// coming from the physics and not from a constant.
	int32 Tallest = INDEX_NONE;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		if (!Cells[Index].bLow
			&& FMath::Abs(Cells[Index].LatitudeRadians)
				> FMath::DegreesToRadians(25.0)
			&& (Tallest == INDEX_NONE
				|| Cells[Index].AnomalyPascals > Cells[Tallest].AnomalyPascals))
		{
			Tallest = Index;
		}
	}
	if (Tallest != INDEX_NONE)
	{
		const FLedgerPressureCell& High = Cells[Tallest];
		const double HighRing = High.RadiusMetres / Body.RadiusMetres;
		double HighCirculation = 0.0;
		for (int32 Step = 0; Step < Around; ++Step)
		{
			const double Bearing = Step * LedgerTwoPi / Around;
			const FVector2D Wind = LedgerWeather::WindAt(System, Home, Air,
				High.LatitudeRadians + HighRing * FMath::Cos(Bearing),
				High.LongitudeRadians + HighRing * FMath::Sin(Bearing)
					/ FMath::Max(FMath::Cos(High.LatitudeRadians), 0.1),
				3.0 * 86400.0);
			const FVector2D Tangent = FVector2D(
				static_cast<float>(-FMath::Cos(Bearing)),
				static_cast<float>(FMath::Sin(Bearing)));
			HighCirculation += FVector2D::DotProduct(Wind, Tangent)
				* High.RadiusMetres * LedgerTwoPi / Around;
		}
		const double HighF = LedgerWeather::CoriolisAt(Body, High.LatitudeRadians);
		AddInfo(FString::Printf(
			TEXT("a %+.0f Pa high at %.1f N circulates %+.3e m2/s"),
			High.AnomalyPascals,
			FMath::RadiansToDegrees(High.LatitudeRadians), HighCirculation));
		TestTrue(TEXT("a high turns the opposite way to a low"),
			(HighF > 0.0) != (HighCirculation > 0.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerWeatherStorms,
	"Ledger.Weather.StormsAreTheDeepestLowsAndTheirGustsRepeat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerWeatherStorms::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);
	const int32 Home = LedgerBodies::HomeIndex(System);
	const FLedgerAirProfile Air = LedgerAir::For(System, Home, 0.0);

	// The deepest low in ten days, hourly. T097.
	FLedgerPressureCell Worst;
	double WorstWhen = 0.0;
	for (double When = 0.0; When <= 10.0 * 86400.0; When += 3600.0)
	{
		for (int32 Index = 0; Index < LedgerWeather::CellCount(); ++Index)
		{
			const FLedgerPressureCell Cell = LedgerWeather::CellAt(System, Home, Index, When);
			if (Cell.bLow && Cell.AnomalyPascals < Worst.AnomalyPascals)
			{
				Worst = Cell;
				WorstWhen = When;
			}
		}
	}
	const FLedgerStorm Core = LedgerWeather::StormAt(System, Home, Air,
		Worst.LatitudeRadians, Worst.LongitudeRadians, WorstWhen);
	AddInfo(FString::Printf(TEXT("deepest low %.0f Pa: severity %.2f, %.1f flashes a minute, gusts %.1f m/s"),
		Worst.AnomalyPascals, Core.Severity, Core.FlashesPerMinute, Core.GustMetresPerSecond));
	TestTrue(TEXT("the deepest low in ten days is a storm"), Core.IsSevere());
	TestTrue(TEXT("and on a world with cloud it flashes"),
		!Air.bHasClouds || Core.FlashesPerMinute > 0.0);

	// Ordinary weather at the same moment is not a storm and has no gusts.
	bool bFoundCalm = false;
	for (int32 Degrees = 30; Degrees < 360 && !bFoundCalm; Degrees += 10)
	{
		const double Longitude = Worst.LongitudeRadians + FMath::DegreesToRadians(static_cast<double>(Degrees));
		if (LedgerWeather::CellAnomalyPascals(System, Home, Worst.LatitudeRadians, Longitude, WorstWhen) > -1000.0)
		{
			bFoundCalm = true;
			TestTrue(TEXT("ordinary weather is not a storm"), !LedgerWeather::StormAt(
				System, Home, Air, Worst.LatitudeRadians, Longitude, WorstWhen).IsSevere());
			TestTrue(TEXT("and has no storm gusts"), LedgerWeather::GustAt(
				System, Home, Air, Worst.LatitudeRadians, Longitude, 3000.0, WorstWhen).IsZero());
		}
	}
	TestTrue(TEXT("somewhere at that latitude is calm"), bFoundCalm);

	// The same place and moment is the same gust, and a line of them through
	// the core has the stated size.
	const FVector3d Once = LedgerWeather::GustAt(System, Home, Air,
		Worst.LatitudeRadians, Worst.LongitudeRadians, 3000.0, WorstWhen);
	const FVector3d Again = LedgerWeather::GustAt(System, Home, Air,
		Worst.LatitudeRadians, Worst.LongitudeRadians, 3000.0, WorstWhen);
	TestTrue(TEXT("a gust is the same every time it is asked for"), Once == Again);
	double Squares = 0.0;
	constexpr int32 Samples = 2000;
	const double Step = 50.0 / (System.Bodies[Home].RadiusMetres * FMath::Cos(Worst.LatitudeRadians));
	for (int32 Sample = 0; Sample < Samples; ++Sample)
	{
		Squares += LedgerWeather::GustAt(System, Home, Air, Worst.LatitudeRadians,
			Worst.LongitudeRadians + Sample * Step, 3000.0, WorstWhen).SizeSquared();
	}
	const double Rms = FMath::Sqrt(Squares / Samples);
	AddInfo(FString::Printf(TEXT("gusts along 100 km of the core: %.1f m/s rms against %.1f stated"),
		Rms, Core.GustMetresPerSecond));
	TestTrue(TEXT("the gusts are the size the severity says"),
		Rms > 0.6 * Core.GustMetresPerSecond && Rms < 1.4 * Core.GustMetresPerSecond);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerWeatherForecast,
	"Ledger.Weather.ASixHourForecastLandsInsideItsOwnConfidence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerWeatherForecast::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);
	const int32 Home = LedgerBodies::HomeIndex(System);
	const FLedgerAirProfile Air = LedgerAir::For(System, Home, 0.0);

	// A hundred places and times, each forecast six hours ahead. T104.
	FRandomStream Pick(20260911);
	constexpr int32 Samples = 100;
	int32 Inside = 0;
	int32 RainRight = 0;
	int32 Rained = 0;
	int32 Unsure = 0;
	double Worst = 0.0;
	for (int32 Sample = 0; Sample < Samples; ++Sample)
	{
		const double Latitude = FMath::DegreesToRadians(static_cast<double>(Pick.FRandRange(-65.0f, 65.0f)));
		const double Longitude = FMath::DegreesToRadians(static_cast<double>(Pick.FRandRange(-180.0f, 180.0f)));
		const double Valid = 86400.0 * static_cast<double>(Pick.FRandRange(1.0f, 30.0f));
		const FLedgerForecast Forecast = LedgerWeather::ForecastAt(
			System, Home, Air, Latitude, Longitude, Valid - 6.0 * 3600.0, Valid);
		const double Actual = LedgerWeather::CellAnomalyPascals(System, Home, Latitude, Longitude, Valid);
		const double Error = FMath::Abs(Actual - Forecast.AnomalyPascals);
		Worst = FMath::Max(Worst, Error);
		Inside += Error <= 2.0 * Forecast.UncertaintyPascals + 1.0 ? 1 : 0;
		Unsure += Forecast.UncertaintyPascals > 1.0 ? 1 : 0;
		const bool bRained = Actual < -300.0;
		Rained += bRained ? 1 : 0;
		RainRight += bRained == (Forecast.RainChance > 0.5) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d of %d inside two sigma, worst miss %.0f Pa, %d forecasts carried uncertainty; rain %d times, called right %d of %d"),
		Inside, Samples, Worst, Unsure, Rained, RainRight, Samples));
	TestTrue(TEXT("at least 90 of 100 six-hour forecasts land inside two sigma"), Inside >= 90);
	TestTrue(TEXT("and rain is called right at least 90 times in 100"), RainRight >= 90);

	// Issued at the moment it is for, a forecast is the weather.
	const double Now = 5.0 * 86400.0;
	const FLedgerForecast Nowcast = LedgerWeather::ForecastAt(System, Home, Air, 0.7, 1.1, Now, Now);
	TestTrue(TEXT("a forecast for now knows every system and is exact"),
		Nowcast.UnbornCells == 0 && FMath::Abs(Nowcast.AnomalyPascals
			- LedgerWeather::CellAnomalyPascals(System, Home, 0.7, 1.1, Now)) < 1.0e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerWeatherAgreement,
	"Ledger.Weather.TheSameSeedIsTheSameWeatherAndTheSkyShowsTheStorm",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerWeatherAgreement::RunTest(const FString&)
{
	// Two systems generated apart from the same seed: nothing shared but it. T106.
	const FLedgerSystem One = LedgerBodies::Generate(20260910u);
	const FLedgerSystem Two = LedgerBodies::Generate(20260910u);
	const int32 Home = LedgerBodies::HomeIndex(One);
	const FLedgerAirProfile AirOne = LedgerAir::For(One, Home, 0.0);
	const FLedgerAirProfile AirTwo = LedgerAir::For(Two, Home, 0.0);

	FRandomStream Pick(20260912);
	int32 Same = 0;
	int32 Storms = 0;
	int32 Shown = 0;
	int32 Tries = 0;
	for (int32 Sample = 0; Sample < 100; ++Sample)
	{
		const double Latitude = FMath::DegreesToRadians(static_cast<double>(Pick.FRandRange(-70.0f, 70.0f)));
		const double Longitude = FMath::DegreesToRadians(static_cast<double>(Pick.FRandRange(-180.0f, 180.0f)));
		const double When = 86400.0 * static_cast<double>(Pick.FRandRange(0.0f, 60.0f));
		const FVector2D WindOne = LedgerWeather::WindAtAltitude(One, Home, AirOne, Latitude, Longitude, 500.0, When);
		const FVector2D WindTwo = LedgerWeather::WindAtAltitude(Two, Home, AirTwo, Latitude, Longitude, 500.0, When);
		const FLedgerStorm StormOne = LedgerWeather::StormAt(One, Home, AirOne, Latitude, Longitude, When);
		const FLedgerStorm StormTwo = LedgerWeather::StormAt(Two, Home, AirTwo, Latitude, Longitude, When);
		const FVector3d GustOne = LedgerWeather::GustAt(One, Home, AirOne, Latitude, Longitude, 3000.0, When);
		const FVector3d GustTwo = LedgerWeather::GustAt(Two, Home, AirTwo, Latitude, Longitude, 3000.0, When);
		Same += WindOne == WindTwo && StormOne.Severity == StormTwo.Severity && GustOne == GustTwo
			&& LedgerWeather::PressureAt(One, Home, AirOne, Latitude, Longitude, When)
				== LedgerWeather::PressureAt(Two, Home, AirTwo, Latitude, Longitude, When) ? 1 : 0;
	}
	TestEqual(TEXT("the same seed and time is the same weather, a hundred times in a hundred"), Same, 100);

	// A hundred severe places, and whether the sky draws a storm over each:
	// inside one of the lows the clouds are given, within its radius.
	while (Storms < 100 && Tries < 200000)
	{
		++Tries;
		const double Latitude = FMath::DegreesToRadians(static_cast<double>(Pick.FRandRange(-70.0f, 70.0f)));
		const double Longitude = FMath::DegreesToRadians(static_cast<double>(Pick.FRandRange(-180.0f, 180.0f)));
		const double When = 86400.0 * static_cast<double>(Pick.FRandRange(0.0f, 60.0f));
		if (!LedgerWeather::StormAt(One, Home, AirOne, Latitude, Longitude, When).IsSevere())
		{
			continue;
		}
		++Storms;
		TArray<FLedgerPressureCell> Lows;
		LedgerWeather::DeepestLows(One, Home, When, 12, Lows);
		const FVector3d Here(FMath::Cos(Latitude) * FMath::Cos(Longitude),
			FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
		for (const FLedgerPressureCell& Low : Lows)
		{
			const FVector3d Centre(FMath::Cos(Low.LatitudeRadians) * FMath::Cos(Low.LongitudeRadians),
				FMath::Cos(Low.LatitudeRadians) * FMath::Sin(Low.LongitudeRadians), FMath::Sin(Low.LatitudeRadians));
			const double Angle = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Here, Centre), -1.0, 1.0));
			if (Angle < 1.5 * Low.RadiusMetres / One.Bodies[Home].RadiusMetres)
			{
				++Shown;
				break;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d severe places found in %d tries; %d under a low the clouds are drawn from"),
		Storms, Tries, Shown));
	TestEqual(TEXT("a hundred storms found"), Storms, 100);
	TestEqual(TEXT("every storm flown into is one the sky shows"), Shown, Storms);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
