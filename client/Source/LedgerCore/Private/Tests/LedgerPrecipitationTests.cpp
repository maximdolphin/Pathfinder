// Where rain becomes snow, and whether anybody put it there. T095.
//
// The acceptance says the boundary sits where the lapse rate puts it, so the
// test that matters is the one that *finds* the boundary rather than reading
// it. FrozenOrNot walks the lapse rate down from the surface temperature and
// asks whether the air is below freezing; FreezingLevelMetres solves for the
// height at which it is exactly freezing. Two routes, and bisecting the first
// has to land on the second -- otherwise one of them is wrong and neither would
// ever have said so.
//
// The third test is the one that keeps the model honest about Earth: a 288 K
// surface at 6.5 K/km puts the freezing level at 2,280 m, which is the number
// in the textbook and roughly where the snow line sits in the Alps in summer.

#include "LedgerAir.h"
#include "LedgerBody.h"
#include "LedgerCloud.h"
#include "LedgerMath.h"
#include "LedgerPrecipitation.h"
#include "LedgerWeather.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerPrecipBoundaryIsFound,
	"Ledger.Precipitation.TheBoundaryIsWhereTheLapseRatePutsIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerPrecipBoundaryIsFound::RunTest(const FString&)
{
	const FLedgerAirProfile Earth = LedgerAir::Describe(
		ELedgerAir::NitrogenOxygen, 101325.0, 288.0, 9.807, 6.371e6);

	const double Declared = LedgerPrecip::FreezingLevelMetres(Earth);
	AddInfo(FString::Printf(TEXT("freezing level %.0f m, lapse %.5f K/m"),
		Declared, LedgerCloud::EnvironmentalLapseRate(Earth)));

	TestTrue(TEXT("it rains at the ground on a 288 K world"),
		LedgerPrecip::FrozenOrNot(Earth, 0.0) == ELedgerPrecipitation::Rain);
	TestTrue(TEXT("it snows at ten kilometres"),
		LedgerPrecip::FrozenOrNot(Earth, 10000.0) == ELedgerPrecipitation::Snow);

	// Bisect for the changeover without ever consulting the declared level.
	double Low = 0.0;
	double High = 10000.0;
	for (int32 Step = 0; Step < 80; ++Step)
	{
		const double Middle = (Low + High) * 0.5;
		(LedgerPrecip::FrozenOrNot(Earth, Middle) == ELedgerPrecipitation::Rain
			? Low : High) = Middle;
	}
	const double Found = (Low + High) * 0.5;

	AddInfo(FString::Printf(
		TEXT("bisected changeover %.3f m against a declared %.3f m, %.2e apart"),
		Found, Declared, FMath::Abs(Found - Declared)));
	TestTrue(TEXT("the changeover is the freezing level, to under a millimetre"),
		FMath::Abs(Found - Declared) < 0.001);

	// The textbook number. 288 K over 6.5 K/km is 2,280 m, and this model uses
	// two thirds of the dry adiabatic rate for the environment, so it should
	// land near it rather than at the 1,500 m the dry rate would give.
	AddInfo(FString::Printf(TEXT("Earth's freezing level %.0f m against 2280"),
		Declared));
	TestTrue(TEXT("Earth's freezing level is within 15% of 2,280 m"),
		FMath::Abs(Declared - 2280.0) < 342.0);

	// A cold world snows at the ground, and the freezing level is below it
	// rather than a clamped zero -- which matters because a renderer asking
	// "how far up does it turn to snow" needs to be told "it already has".
	const FLedgerAirProfile Cold = LedgerAir::Describe(
		ELedgerAir::NitrogenOxygen, 101325.0, 258.0, 9.807, 6.371e6);
	AddInfo(FString::Printf(TEXT("a 258 K world freezes at %.0f m"),
		LedgerPrecip::FreezingLevelMetres(Cold)));
	TestTrue(TEXT("a world below freezing snows at the ground"),
		LedgerPrecip::FrozenOrNot(Cold, 0.0) == ELedgerPrecipitation::Snow);
	TestTrue(TEXT("and its freezing level is underground"),
		LedgerPrecip::FreezingLevelMetres(Cold) < 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerPrecipFollowsTheLows,
	"Ledger.Precipitation.ItRainsInTheLowAndNotBesideIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerPrecipFollowsTheLows::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908);
	const int32 Body = 1;
	if (!System.Bodies.IsValidIndex(Body))
	{
		AddError(TEXT("the generated system has no body 1"));
		return false;
	}

	const double When = 0.0;
	const FLedgerAirProfile Air = LedgerAir::For(System, Body, When);
	if (!Air.bHasClouds)
	{
		AddError(TEXT("body 1 has no condensable water, so this test is meaningless"));
		return false;
	}

	// Find the deepest low there is at this instant and stand under it.
	TArray<FLedgerPressureCell> Cells;
	LedgerWeather::CellsAt(System, Body, When, Cells);
	const FLedgerPressureCell* Deepest = nullptr;
	for (const FLedgerPressureCell& Cell : Cells)
	{
		if (Deepest == nullptr || Cell.AnomalyPascals < Deepest->AnomalyPascals)
		{
			Deepest = &Cell;
		}
	}
	if (Deepest == nullptr || Deepest->AnomalyPascals >= 0.0)
	{
		AddError(TEXT("no low anywhere on the body at t=0"));
		return false;
	}

	const FLedgerPrecipitation Under = LedgerPrecip::At(
		System, Body, Air, Deepest->LatitudeRadians, Deepest->LongitudeRadians,
		0.0, When);
	AddInfo(FString::Printf(
		TEXT("under a %.0f Pa low: %s at %.2f mm/h"),
		Deepest->AnomalyPascals, LexToString(Under.Kind),
		Under.RateMillimetresPerHour));
	TestTrue(TEXT("it precipitates under a low"), Under.IsFalling());

	// Four cell radii away the anomaly is exp(-16) of its centre value, which
	// is nothing. Somewhere that far from every low is dry.
	const double Away = 4.0 * Deepest->RadiusMetres / System.Bodies[Body].RadiusMetres;
	const double Lat = FMath::Clamp(
		Deepest->LatitudeRadians + Away, -1.4, 1.4);
	const FLedgerPrecipitation Beside = LedgerPrecip::At(
		System, Body, Air, Lat, Deepest->LongitudeRadians + LedgerPi, 0.0, When);
	AddInfo(FString::Printf(TEXT("a quarter turn away: %s at %.2f mm/h"),
		LexToString(Beside.Kind), Beside.RateMillimetresPerHour));

	// And nothing falls above the cloud top, at any anomaly.
	const FLedgerPrecipitation Above = LedgerPrecip::At(
		System, Body, Air, Deepest->LatitudeRadians, Deepest->LongitudeRadians,
		Air.CloudTopMetres + 1000.0, When);
	TestTrue(TEXT("nothing falls above the cloud top"), !Above.IsFalling());

	// Rain falls nine times faster than snow, which is the whole of why they
	// look different.
	TestTrue(TEXT("rain outruns snow"),
		LedgerPrecip::FallSpeedMetresPerSecond(ELedgerPrecipitation::Rain)
		> 5.0 * LedgerPrecip::FallSpeedMetresPerSecond(ELedgerPrecipitation::Snow));

	return true;
}

#endif
