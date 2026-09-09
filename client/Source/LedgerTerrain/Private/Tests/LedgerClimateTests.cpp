// Climate without a planet, a world or a frame.
//
// The acceptance for T051 is a pole-to-equator transect, and a transect is a
// file somebody reads. These are the parts of it that can fail on their own,
// so a regression shows up as a red test rather than as a number nobody
// re-read.

#include "LedgerClimate.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// The planet these tests are about, with ALedgerPlanet's own defaults.
	///
	/// Sea level was 0.55 here for a while against the planet's 0.14, which put
	/// almost the whole surface under water: the rain-shadow test found zero
	/// land points to examine and said so. A fixture that quietly describes a
	/// different world than the one shipping is worse than no fixture.
	FLedgerTerrainParams ThisPlanet()
	{
		FLedgerTerrainParams Params;
		Params.Seed = 1337;
		Params.Radius = 637100000.0;     // 6,371 km in centimetres
		Params.MaxElevation = 900000.0;  // 9 km
		Params.SeaLevel = 0.14;
		return Params;
	}

	/// A point on the prime meridian at a latitude in degrees.
	FVector3d AtLatitude(double Degrees)
	{
		const double Radians = FMath::DegreesToRadians(Degrees);
		return FVector3d(FMath::Cos(Radians), 0.0, FMath::Sin(Radians)).GetSafeNormal();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerClimateSeaLevelTemperatureIsMonotonic,
	"Ledger.Climate.SeaLevelTemperatureIsMonotonicPoleToEquator",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerClimateSeaLevelTemperatureIsMonotonic::RunTest(const FString&)
{
	const FLedgerTerrainParams Params = ThisPlanet();

	// Pole to equator in one-degree steps. Strictly increasing, every step.
	double Previous = -1000.0;
	for (int32 Degrees = 90; Degrees >= 0; --Degrees)
	{
		const FLedgerClimate Climate = LedgerClimate::At(AtLatitude(Degrees), Params);
		if (!(Climate.SeaLevelTemperatureC > Previous))
		{
			AddError(FString::Printf(
				TEXT("sea-level temperature is not monotonic: %.2f C at %d deg "
					 "is not warmer than %.2f C one degree poleward"),
				Climate.SeaLevelTemperatureC, Degrees, Previous));
			return false;
		}
		Previous = Climate.SeaLevelTemperatureC;
	}

	// And the ends are the ends.
	TestEqual(TEXT("equator"),
		LedgerClimate::At(AtLatitude(0.0), Params).SeaLevelTemperatureC, LedgerClimate::EquatorC, 0.001);
	TestEqual(TEXT("pole"),
		LedgerClimate::At(AtLatitude(90.0), Params).SeaLevelTemperatureC, LedgerClimate::PoleC, 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerClimateLapseRateCoolsWithAltitude,
	"Ledger.Climate.AltitudeCoolsAtTheLapseRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerClimateLapseRateCoolsWithAltitude::RunTest(const FString&)
{
	const FLedgerTerrainParams Params = ThisPlanet();

	// Whatever the ground does, surface temperature is the sea-level band minus
	// the lapse rate times the altitude. Checked against the climate's own
	// reported altitude so this does not become a second implementation of the
	// height function pretending to be a test.
	for (int32 Degrees = 0; Degrees <= 80; Degrees += 7)
	{
		const FLedgerClimate Climate = LedgerClimate::At(AtLatitude(Degrees), Params);
		const double Expected = Climate.SeaLevelTemperatureC
			- LedgerClimate::LapseRateCPerKm * FMath::Max(0.0, Climate.AltitudeMetres) / 1000.0;
		TestEqual(FString::Printf(TEXT("surface temperature at %d deg"), Degrees),
			Climate.TemperatureC, Expected, 0.001);

		// Under water there is no lapse term to apply.
		if (Climate.AltitudeMetres <= 0.0)
		{
			TestEqual(FString::Printf(TEXT("submerged point at %d deg is not chilled"), Degrees),
				Climate.TemperatureC, Climate.SeaLevelTemperatureC, 0.001);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerClimateMoistureIsBounded,
	"Ledger.Climate.MoistureStaysInRange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerClimateMoistureIsBounded::RunTest(const FString&)
{
	const FLedgerTerrainParams Params = ThisPlanet();

	// The march multiplies and lerps its way along sixteen steps; an unbounded
	// result would poison every biome read from it, and the failure would show
	// up somewhere else entirely.
	for (int32 Degrees = -90; Degrees <= 90; Degrees += 3)
	{
		for (int32 Turn = 0; Turn < 8; ++Turn)
		{
			const double Longitude = FMath::DegreesToRadians(Turn * 45.0);
			const double Radians = FMath::DegreesToRadians(static_cast<double>(Degrees));
			const FVector3d Point(
				FMath::Cos(Radians) * FMath::Cos(Longitude),
				FMath::Cos(Radians) * FMath::Sin(Longitude),
				FMath::Sin(Radians));

			const FLedgerClimate Climate = LedgerClimate::At(Point.GetSafeNormal(), Params);
			if (Climate.Moisture < 0.0 || Climate.Moisture > 1.0
				|| !FMath::IsFinite(Climate.Moisture))
			{
				AddError(FString::Printf(TEXT("moisture %.4f out of range at %d deg, turn %d"),
					Climate.Moisture, Degrees, Turn));
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerClimateWindBandsReverse,
	"Ledger.Climate.WindBandsReverseWithLatitude",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerClimateWindBandsReverse::RunTest(const FString&)
{
	// The bands are the reason a rain shadow falls on opposite sides of a range
	// at 20 and 45 degrees. If they stop reversing, every shadow lands the same
	// way round and the map reads as wrong without anything looking broken.
	auto Eastwardness = [](double Degrees)
	{
		const FVector3d Point = AtLatitude(Degrees);
		const FVector3d East =
			FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Point).GetSafeNormal();
		return FVector3d::DotProduct(LedgerClimate::PrevailingWind(Point), East);
	};

	TestTrue(TEXT("tropics are easterly"), Eastwardness(15.0) > 0.9);
	TestTrue(TEXT("mid latitudes are westerly"), Eastwardness(45.0) < -0.9);
	TestTrue(TEXT("polar band is easterly again"), Eastwardness(75.0) > 0.9);
	TestTrue(TEXT("southern tropics are easterly"), Eastwardness(-15.0) > 0.9);
	TestTrue(TEXT("southern mid latitudes are westerly"), Eastwardness(-45.0) < -0.9);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
