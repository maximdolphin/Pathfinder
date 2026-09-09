// Snow, the season, and the line between them. T060.
//
//   The same location has snow in winter and not in summer, and the snow line
//   moves with altitude.
//
// Both halves are properties of the climate function, so both are tests rather
// than a pair of screenshots taken six months apart.

#include "LedgerClimate.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double NorthernSummer = 0.25;
	constexpr double NorthernWinter = 0.75;

	FLedgerTerrainParams SnowPlanet()
	{
		FLedgerTerrainParams Params;
		Params.Seed = 1337;
		Params.Radius = 637100000.0;
		Params.MaxElevation = 900000.0;
		Params.SeaLevel = 0.14;
		return Params;
	}

	FVector3d AtLatitude(double Degrees)
	{
		const double Radians = FMath::DegreesToRadians(Degrees);
		return FVector3d(FMath::Cos(Radians), 0.0, FMath::Sin(Radians)).GetSafeNormal();
	}

	/// Climate at a latitude and an altitude, with the moisture of somewhere it
	/// can actually snow.
	///
	/// Built rather than sampled from the terrain: the acceptance is about the
	/// snow line, and sampling real ground would make every answer depend on
	/// whether that particular hillside happened to be at that height.
	FLedgerClimate Weather(double LatitudeDegrees, double AltitudeMetres, double SeasonPhase)
	{
		const FVector3d Point = AtLatitude(LatitudeDegrees);
		FLedgerClimate Climate;
		const double SinLat = FMath::Sin(FMath::DegreesToRadians(LatitudeDegrees));
		Climate.SeaLevelTemperatureC =
			FMath::Lerp(LedgerClimate::EquatorC, LedgerClimate::PoleC, SinLat * SinLat)
			+ LedgerClimate::SeasonalOffsetC(Point, SeasonPhase);
		Climate.AltitudeMetres = AltitudeMetres;
		Climate.TemperatureC = Climate.SeaLevelTemperatureC
			- LedgerClimate::LapseRateCPerKm * FMath::Max(0.0, AltitudeMetres) / 1000.0;
		Climate.Moisture = 0.6;
		return Climate;
	}

	/// The altitude at which snow first lies, or -1 if it never does below the
	/// planet's own maximum.
	double SnowLine(double LatitudeDegrees, double SeasonPhase)
	{
		for (double Altitude = 0.0; Altitude <= 9000.0; Altitude += 10.0)
		{
			if (LedgerClimate::SnowCover(Weather(LatitudeDegrees, Altitude, SeasonPhase)) > 0.5)
			{
				return Altitude;
			}
		}
		return -1.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSnowComesAndGoesWithTheSeason,
	"Ledger.Snow.SameLocationHasSnowInWinterAndNotInSummer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSnowComesAndGoesWithTheSeason::RunTest(const FString&)
{
	// Fifty-five north at eight hundred metres: sea-level temperature there is
	// about 6 C on the annual mean, the swing is +/-16, and the lapse rate
	// takes another 5. That puts it either side of freezing across the year,
	// which is exactly the case the acceptance is about -- somewhere the answer
	// is not obvious from the latitude alone.
	const double Summer = LedgerClimate::SnowCover(Weather(55.0, 800.0, NorthernSummer));
	const double Winter = LedgerClimate::SnowCover(Weather(55.0, 800.0, NorthernWinter));

	AddInfo(FString::Printf(TEXT("55 N at 800 m: %.2f in summer, %.2f in winter"),
		Summer, Winter));

	TestTrue(TEXT("bare in summer"), Summer < 0.01);
	TestTrue(TEXT("covered in winter"), Winter > 0.9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSnowLineMovesWithAltitude,
	"Ledger.Snow.TheSnowLineMovesWithAltitudeAndLatitude",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSnowLineMovesWithAltitude::RunTest(const FString&)
{
	// The line is not a parameter anywhere: it is where the surface temperature
	// crosses freezing, and the lapse rate puts that at a different altitude
	// for every latitude. So it must fall monotonically as the pole is
	// approached, and there must be somewhere it exists at all.
	double Previous = TNumericLimits<double>::Max();
	int32 Found = 0;
	for (double Latitude = 0.0; Latitude <= 80.0; Latitude += 5.0)
	{
		const double Line = SnowLine(Latitude, 0.0);
		if (Line < 0.0)
		{
			// No snow at any altitude this planet reaches. Only legal while
			// nothing colder has been seen.
			TestTrue(FString::Printf(
				TEXT("no snow line at %.0f deg, and none below it either"), Latitude),
				Found == 0);
			continue;
		}

		++Found;
		if (Line > Previous)
		{
			AddError(FString::Printf(
				TEXT("snow line rises from %.0f m to %.0f m going poleward to %.0f deg"),
				Previous, Line, Latitude));
			return false;
		}
		Previous = Line;
	}

	TestTrue(TEXT("the snow line exists somewhere"), Found > 0);

	// And within one latitude, higher ground is snowier. That is the half of
	// the acceptance about altitude, and it is what the lapse rate buys.
	const double Low = LedgerClimate::SnowCover(Weather(45.0, 0.0, 0.0));
	const double High = LedgerClimate::SnowCover(Weather(45.0, 4000.0, 0.0));
	AddInfo(FString::Printf(TEXT("45 N: %.2f at sea level, %.2f at 4,000 m"), Low, High));
	TestTrue(TEXT("higher ground carries more snow"), High > Low);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSnowSeasonsAreOpposite,
	"Ledger.Snow.SeasonsAreOppositeInTheTwoHemispheres",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSnowSeasonsAreOpposite::RunTest(const FString&)
{
	// Not a nicety. A seasonal term that is even in latitude gives both poles
	// winter at once, which is wrong everywhere and obviously wrong from orbit.
	for (double Latitude = 10.0; Latitude <= 80.0; Latitude += 10.0)
	{
		const double North = LedgerClimate::SeasonalOffsetC(
			AtLatitude(Latitude), NorthernSummer);
		const double South = LedgerClimate::SeasonalOffsetC(
			AtLatitude(-Latitude), NorthernSummer);

		TestTrue(FString::Printf(TEXT("%.0f N is warmed in northern summer"), Latitude),
			North > 0.0);
		TestEqual(FString::Printf(TEXT("%.0f S is cooled by as much"), Latitude),
			South, -North, 0.001);
	}

	// And the equator does not have seasons at all.
	TestEqual(TEXT("the equator"),
		LedgerClimate::SeasonalOffsetC(AtLatitude(0.0), NorthernWinter), 0.0, 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSnowNeedsMoisture,
	"Ledger.Snow.ColdAndDryIsBareRock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSnowNeedsMoisture::RunTest(const FString&)
{
	// The coldest deserts on Earth are bare, and a model that puts a snowfield
	// on every cold thing paints the whole of this planet's high ground white.
	FLedgerClimate Dry = Weather(70.0, 1500.0, NorthernWinter);
	Dry.Moisture = 0.0;
	FLedgerClimate Wet = Dry;
	Wet.Moisture = 0.6;

	TestTrue(TEXT("cold and dry is bare"), LedgerClimate::SnowCover(Dry) < 0.01);
	TestTrue(TEXT("cold and wet is covered"), LedgerClimate::SnowCover(Wet) > 0.9);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
