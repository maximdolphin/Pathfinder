// A year in under a minute. T085.
//
// The acceptance has a wall-clock half and a correctness half, and the second is
// the one that matters: a year that runs fast and arrives somewhere else is not
// a year that ran fast.

#include "LedgerClock.h"
#include "LedgerBody.h"
#include "LedgerEphemeris.h"
#include "LedgerSky.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerClockYear,
	"Ledger.Clock.AYearRunsInUnderAMinuteAndLandsWhereItShould",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerClockYear::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const double Year = LedgerEphemeris::PeriodSeconds(
		System.Bodies[0].MassKg, System.Bodies[1].Orbit.SemiMajorAxisMetres);

	// Stepped an hour of simulated time at a time, which is 8,760 steps for a
	// year and fine enough that nothing seasonal is stepped over.
	FLedgerClock Clock;
	Clock.Rate = Year / 30.0;            // a year in thirty real seconds
	Clock.MaxStepSeconds = 3600.0;

	TArray<FLedgerState> Sampled;
	int32 Steps = 0;
	double Hottest = -1000.0;

	const double Started = FPlatformTime::Seconds();
	Clock.Advance(30.0, [&](double At)
	{
		++Steps;
		// Everything a year is inspected FOR: where the bodies are, and what
		// the sky over the home world is doing.
		LedgerEphemeris::StatesAt(System, At, Sampled);
		Hottest = FMath::Max(Hottest,
			LedgerSky::SolarDeclination(System, 1, At));
	});
	const double Elapsed = FPlatformTime::Seconds() - Started;

	AddInfo(FString::Printf(
		TEXT("a year of %.0f s stepped in %d hourly steps, sampling the ephemeris and "
			 "the sky at each: %.3f real seconds"),
		Year, Steps, Elapsed));
	AddInfo(FString::Printf(
		TEXT("that is %.0f simulated seconds per real second, or a year every %.2f "
			 "seconds if nothing else were running"),
		Year / Elapsed, Elapsed));

	TestTrue(*FString::Printf(TEXT("a year runs in under a minute (%.3f s)"), Elapsed),
		Elapsed < 60.0);

	// **And it ends where the analytic model says.** The clock arrived by
	// eight thousand steps; the model is asked once. If those disagree, the
	// acceleration has changed the answer, which is the only way this feature
	// can be wrong.
	TArray<FLedgerState> Stepped;
	LedgerEphemeris::StatesAt(System, Clock.SecondsFromEpoch, Stepped);

	TArray<FLedgerState> Direct;
	LedgerEphemeris::StatesAt(System, Year, Direct);

	AddInfo(FString::Printf(
		TEXT("the clock stopped at %.6f s against a year of %.6f s, a difference of "
			 "%.3e s"),
		Clock.SecondsFromEpoch, Year, FMath::Abs(Clock.SecondsFromEpoch - Year)));

	double Worst = 0.0;
	for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
	{
		Worst = FMath::Max(Worst,
			(Stepped[Index].PositionMetres - Direct[Index].PositionMetres).Length());
	}
	AddInfo(FString::Printf(
		TEXT("every body is within %.6f m of where asking directly puts it"), Worst));

	TestTrue(*FString::Printf(TEXT("the clock lands on the year (%.3e s out)"),
		FMath::Abs(Clock.SecondsFromEpoch - Year)),
		FMath::Abs(Clock.SecondsFromEpoch - Year) < 1e-6);
	TestTrue(*FString::Printf(
		TEXT("and every body is where the model says (%.6f m)"), Worst),
		Worst < 0.001);

	// The year really was a year: the sun's declination has to have swung
	// through its full range, or the clock advanced without the sky noticing.
	AddInfo(FString::Printf(
		TEXT("the sun reached %.2f degrees of declination against an axial tilt of "
			 "%.2f"),
		FMath::RadiansToDegrees(Hottest),
		FMath::RadiansToDegrees(System.Bodies[1].AxialTiltRadians)));
	TestTrue(TEXT("a whole year passed, not merely a lot of steps"),
		FMath::RadiansToDegrees(Hottest)
			> FMath::RadiansToDegrees(System.Bodies[1].AxialTiltRadians) * 0.9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerClockCap,
	"Ledger.Clock.TheStepCapTurnsABigJumpIntoManySmallOnes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerClockCap::RunTest(const FString&)
{
	// **The cap is a correctness knob, not a performance one.** At a useful
	// rate one frame of real time is weeks of simulated time, and anything that
	// integrates rather than solving would be handed a step it cannot survive.
	FLedgerClock Clock;
	Clock.Rate = 525600.0;               // a year a minute
	Clock.MaxStepSeconds = 3600.0;

	int32 Steps = 0;
	double Largest = 0.0;
	double Previous = Clock.SecondsFromEpoch;
	Clock.Advance(1.0 / 60.0, [&](double At)
	{
		++Steps;
		Largest = FMath::Max(Largest, At - Previous);
		Previous = At;
	});

	AddInfo(FString::Printf(
		TEXT("one sixtieth of a second at 525600x is %.0f simulated seconds, taken "
			 "in %d steps of at most %.0f"),
		Clock.SecondsFromEpoch, Steps, Largest));

	TestTrue(TEXT("a frame at that rate is more than one step"), Steps > 1);
	TestTrue(*FString::Printf(TEXT("and no step exceeds the cap (%.0f s)"), Largest),
		Largest <= Clock.MaxStepSeconds + 1e-9);
	TestEqual(TEXT("the clock still arrives at the right time"),
		Clock.SecondsFromEpoch, 525600.0 / 60.0, 1e-9);

	// And the steps add up to exactly the interval, with nothing lost to the
	// last partial one.
	FLedgerClock Uncapped;
	Uncapped.Rate = 525600.0;
	Uncapped.MaxStepSeconds = 0.0;
	int32 Once = 0;
	Uncapped.Advance(1.0 / 60.0, [&Once](double) { ++Once; });
	AddInfo(FString::Printf(
		TEXT("with no cap the same interval is %d step, arriving at %.9f s"),
		Once, Uncapped.SecondsFromEpoch));
	TestEqual(TEXT("capped and uncapped arrive at the same time"),
		Clock.SecondsFromEpoch, Uncapped.SecondsFromEpoch, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerClockSkip,
	"Ledger.Clock.SkippingAYearCostsNothingBecauseTheEphemerisIsAFunction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerClockSkip::RunTest(const FString&)
{
	// **This is the dividend T070 paid.** An integrated solar system has to be
	// stepped from the epoch to the moment you care about, so knowing where a
	// moon was last Tuesday costs a simulated week. An analytic one does not,
	// and the difference is visible in a stopwatch.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const double Year = LedgerEphemeris::PeriodSeconds(
		System.Bodies[0].MassKg, System.Bodies[1].Orbit.SemiMajorAxisMetres);

	FLedgerClock Clock;
	const double Started = FPlatformTime::Seconds();
	Clock.Skip(Year * 100.0);
	TArray<FLedgerState> States;
	LedgerEphemeris::StatesAt(System, Clock.SecondsFromEpoch, States);
	const double Elapsed = FPlatformTime::Seconds() - Started;

	AddInfo(FString::Printf(
		TEXT("a century skipped and the whole system placed: %.6f real seconds"),
		Elapsed));
	TestTrue(*FString::Printf(TEXT("a century costs nothing (%.6f s)"), Elapsed),
		Elapsed < 0.05);

	// And it is the right century: stepping there hourly must agree.
	FLedgerClock Walked;
	Walked.Rate = 1.0;
	Walked.MaxStepSeconds = Year;
	Walked.Advance(Year * 100.0, [](double) {});
	TArray<FLedgerState> Stepped;
	LedgerEphemeris::StatesAt(System, Walked.SecondsFromEpoch, Stepped);

	double Worst = 0.0;
	for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
	{
		Worst = FMath::Max(Worst,
			(States[Index].PositionMetres - Stepped[Index].PositionMetres).Length());
	}
	AddInfo(FString::Printf(
		TEXT("skipping a century and walking one land within %.6f m of each other"),
		Worst));
	TestTrue(*FString::Printf(TEXT("skipping and walking agree (%.6f m)"), Worst),
		Worst < 0.001);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
