// Snow, the season, and the line between them. T060.
//
//   The same location has snow in winter and not in summer, and the snow line
//   moves with altitude.
//
// Both halves are properties of the climate function, so both are tests rather
// than a pair of screenshots taken six months apart.

#include "LedgerClimate.h"

#include "Misc/AutomationTest.h"
#include "LedgerTerrainMath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// The tilt these tests reason about, read from the terrain parameters
	/// rather than written down again -- T073 made the seasonal swing depend on
	/// it, so a test carrying its own copy would pass while the generator did
	/// something else.
	const double SnowTiltRadians = FLedgerTerrainParams().AxialTiltRadians;

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

	FVector3d SnowAtLatitude(double Degrees)
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
	FLedgerClimate SnowWeather(double LatitudeDegrees, double AltitudeMetres, double SeasonPhase)
	{
		const FVector3d Point = SnowAtLatitude(LatitudeDegrees);
		FLedgerClimate Climate;
		const double SinLat = FMath::Sin(FMath::DegreesToRadians(LatitudeDegrees));
		Climate.SeaLevelTemperatureC =
			FMath::Lerp(LedgerClimate::EquatorC, LedgerClimate::PoleC, SinLat * SinLat)
			+ LedgerClimate::SeasonalOffsetC(Point, SeasonPhase, SnowTiltRadians);
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
			if (LedgerClimate::SnowCover(SnowWeather(LatitudeDegrees, Altitude, SeasonPhase)) > 0.5)
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
	const double Summer = LedgerClimate::SnowCover(SnowWeather(55.0, 800.0, NorthernSummer));
	const double Winter = LedgerClimate::SnowCover(SnowWeather(55.0, 800.0, NorthernWinter));

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
	const double Low = LedgerClimate::SnowCover(SnowWeather(45.0, 0.0, 0.0));
	const double High = LedgerClimate::SnowCover(SnowWeather(45.0, 4000.0, 0.0));
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
	//
	// **What is antisymmetric is the swing, not the offset.** This used to
	// assert that the southern offset was the exact negative of the northern
	// one, which was true of `sin(latitude) * sin(2 pi phase)` by construction
	// and is not true of sunlight: a latitude's annual mean insolation is not
	// the midpoint of its two solstices, so its summer excess and its winter
	// deficit are not equal. At 10 degrees they now read +1.65 and -3.51. What
	// is exactly equal and opposite is the distance between the two solstices,
	// because I(-lat, d) = I(lat, -d) and the year is the same year.
	for (double Latitude = 10.0; Latitude <= 80.0; Latitude += 10.0)
	{
		const double North = LedgerClimate::SeasonalOffsetC(
			SnowAtLatitude(Latitude), NorthernSummer, SnowTiltRadians);
		const double South = LedgerClimate::SeasonalOffsetC(
			SnowAtLatitude(-Latitude), NorthernSummer, SnowTiltRadians);

		TestTrue(FString::Printf(TEXT("%.0f N is warmed in northern summer"), Latitude),
			North > 0.0);
		TestTrue(FString::Printf(
			TEXT("%.0f S is cooled at the same moment (%.2f C)"), Latitude, South),
			South < 0.0);

		const double NorthSwing = North - LedgerClimate::SeasonalOffsetC(
			SnowAtLatitude(Latitude), NorthernWinter, SnowTiltRadians);
		const double SouthSwing = LedgerClimate::SeasonalOffsetC(
			SnowAtLatitude(-Latitude), NorthernWinter, SnowTiltRadians) - South;
		TestEqual(FString::Printf(
			TEXT("%.0f S has the same size of year as %.0f N"), Latitude, Latitude),
			SouthSwing, NorthSwing, 0.001);
	}

	// And the equator's two solstices are the same as each other, which is the
	// equatorial version of the claim: no hemisphere, no opposite season. It is
	// not zero -- the star is off the equator at both, so both are slightly
	// cooler than an equinox -- but the year has no north and south to it.
	TestEqual(TEXT("the equator's two solstices match"),
		LedgerClimate::SeasonalOffsetC(SnowAtLatitude(0.0), NorthernWinter, SnowTiltRadians),
		LedgerClimate::SeasonalOffsetC(SnowAtLatitude(0.0), NorthernSummer, SnowTiltRadians),
		0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSnowNeedsMoisture,
	"Ledger.Snow.ColdAndDryIsBareRock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSnowNeedsMoisture::RunTest(const FString&)
{
	// The coldest deserts on Earth are bare, and a model that puts a snowfield
	// on every cold thing paints the whole of this planet's high ground white.
	FLedgerClimate Dry = SnowWeather(70.0, 1500.0, NorthernWinter);
	Dry.Moisture = 0.0;
	FLedgerClimate Wet = Dry;
	Wet.Moisture = 0.6;

	TestTrue(TEXT("cold and dry is bare"), LedgerClimate::SnowCover(Dry) < 0.01);
	TestTrue(TEXT("cold and wet is covered"), LedgerClimate::SnowCover(Wet) > 0.9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSnowLineAcrossAYear,
	"Ledger.Snow.TheSnowLineMovesAcrossAYear",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSnowLineAcrossAYear::RunTest(const FString&)
{
	// The second half of T073's acceptance. The first half -- a high-latitude
	// day being measurably shorter in winter -- is Ledger.Season; this is the
	// consequence on the ground.
	constexpr double Latitude = 55.0;

	double Lowest = TNumericLimits<double>::Max();
	double Highest = -TNumericLimits<double>::Max();
	double LowestAt = 0.0;
	double HighestAt = 0.0;

	FString Table;
	for (int32 Step = 0; Step < 12; ++Step)
	{
		const double Phase = Step / 12.0;
		const double Line = SnowLine(Latitude, Phase);
		Table += FString::Printf(TEXT("  phase %.2f -> %s\n"), Phase,
			Line < 0.0 ? TEXT("no snow below the summit")
				: *FString::Printf(TEXT("%.0f m"), Line));
		if (Line >= 0.0)
		{
			if (Line < Lowest) { Lowest = Line; LowestAt = Phase; }
			if (Line > Highest) { Highest = Line; HighestAt = Phase; }
		}
	}
	AddInfo(FString::Printf(TEXT("snow line at latitude %.0f across one year:\n%s"),
		Latitude, *Table));

	TestTrue(TEXT("the snow line exists somewhere in the year"),
		Lowest < TNumericLimits<double>::Max());
	AddInfo(FString::Printf(
		TEXT("lowest %.0f m at phase %.2f, highest %.0f m at phase %.2f, range %.0f m"),
		Lowest, LowestAt, Highest, HighestAt, Highest - Lowest));

	// A snow line that moves by less than the depth of the band it is drawn as
	// has not moved. Several hundred metres is what a real mid-latitude one
	// does between February and August.
	TestTrue(*FString::Printf(TEXT("the snow line moves across the year (%.0f m)"),
		Highest - Lowest),
		Highest - Lowest > 300.0);

	// And it comes back: a year is a cycle, not a ramp.
	TestEqual(TEXT("the year closes"), SnowLine(Latitude, 0.0), SnowLine(Latitude, 1.0), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSnowNoTiltNoSeasons,
	"Ledger.Snow.APlanetWithNoTiltHasNoSeasons",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSnowNoTiltNoSeasons::RunTest(const FString&)
{
	// **The reason T073 replaced a drawn curve with an insolation anomaly.**
	//
	// The old seasonal term was `swing * sin(latitude) * sin(2 pi phase)`, which
	// gives a planet with no axial tilt a full set of seasons, and gives planets
	// tilted 10 and 40 degrees exactly the same ones. Neither is true, and
	// neither could be detected by any test written against that formula,
	// because the tilt did not appear in it.
	for (double Latitude = -80.0; Latitude <= 80.0; Latitude += 20.0)
	{
		for (double Phase = 0.0; Phase < 1.0; Phase += 0.125)
		{
			TestEqual(
				*FString::Printf(TEXT("no tilt, latitude %.0f, phase %.3f"), Latitude, Phase),
				LedgerClimate::SeasonalOffsetC(SnowAtLatitude(Latitude), Phase, 0.0), 0.0, 1e-9);
		}
	}

	// And more tilt is more season, which is the other half of the claim.
	const double Small = LedgerClimate::SeasonalOffsetC(
		SnowAtLatitude(60.0), NorthernSummer, FMath::DegreesToRadians(5.0));
	const double Large = LedgerClimate::SeasonalOffsetC(
		SnowAtLatitude(60.0), NorthernSummer, FMath::DegreesToRadians(35.0));
	AddInfo(FString::Printf(
		TEXT("at 60 degrees in midsummer: %+.2f C with a 5 degree tilt, %+.2f C with 35"),
		Small, Large));
	TestTrue(*FString::Printf(
		TEXT("a steeper tilt makes a stronger summer (%.2f against %.2f)"), Large, Small),
		Large > Small * 1.5);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
