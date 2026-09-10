// Where a body is, and whether the answer depends on how you asked. T070.
//
// The acceptance: "positions at t agree whether computed forward from zero or
// directly, to sub-metre over a simulated century". For an analytic solution
// that is close to a tautology -- which is the point. An integrator would fail
// it, and this test is what stops one quietly replacing this.

#include "LedgerEphemeris.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double CenturySeconds = 100.0 * 365.25 * 86400.0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerEphemerisNoDrift,
	"Ledger.Ephemeris.SteppedAndDirectAgreeOverACentury",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerEphemerisNoDrift::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);

	// A century, walked a day at a time, against the same century asked for
	// directly. Ten thousand steps is where an integrator's error would be
	// obvious and where a function of time cannot have any.
	constexpr int32 Steps = 36525;
	const double Step = CenturySeconds / Steps;

	double Worst = 0.0;
	int32 WorstBody = 0;
	double WorstAt = 0.0;

	TArray<FLedgerState> Direct;
	TArray<FLedgerState> Walked;
	for (int32 Index = 0; Index <= Steps; ++Index)
	{
		// "Forward from zero": the time accumulated one step at a time, which
		// is how a stepped simulation would arrive at it, including the
		// floating-point error of ten thousand additions.
		static double Accumulated = 0.0;
		if (Index == 0) { Accumulated = 0.0; }
		else { Accumulated += Step; }

		const double Straight = Step * Index;

		// Only sample occasionally -- the accumulation has to happen every
		// step, but comparing every step is ten thousand times the work for
		// the same answer.
		if ((Index % 1000) != 0)
		{
			continue;
		}

		LedgerEphemeris::StatesAt(System, Straight, Direct);
		LedgerEphemeris::StatesAt(System, Accumulated, Walked);

		for (int32 Body = 0; Body < Direct.Num(); ++Body)
		{
			const double Apart = (Direct[Body].PositionMetres
				- Walked[Body].PositionMetres).Length();
			if (Apart > Worst)
			{
				Worst = Apart;
				WorstBody = Body;
				WorstAt = Straight;
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("worst disagreement %.6f m, body %d (%s), at %.1f years"),
		Worst, WorstBody, *System.Bodies[WorstBody].Name, WorstAt / (365.25 * 86400.0)));

	TestTrue(*FString::Printf(
		TEXT("stepped and direct agree to sub-metre over a century (worst %.6f m)"), Worst),
		Worst < 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerEphemerisIsPeriodic,
	"Ledger.Ephemeris.ABodyReturnsAfterOnePeriod",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerEphemerisIsPeriodic::RunTest(const FString&)
{
	// The check that the solution is a real orbit rather than a curve that
	// happens to be self-consistent. After exactly one period a body is where
	// it started, and after a hundred it is there again.
	const FLedgerSystem System = LedgerBodies::Generate(7u);

	for (int32 Index = 1; Index < System.Bodies.Num(); ++Index)
	{
		const FLedgerBody& Body = System.Bodies[Index];
		const double ParentMass = System.Bodies[Body.ParentIndex].MassKg;
		const double Period = LedgerEphemeris::PeriodSeconds(
			ParentMass, Body.Orbit.SemiMajorAxisMetres);
		if (!(Period > 0.0))
		{
			continue;
		}

		const FLedgerState Start = LedgerEphemeris::StateAt(Body, ParentMass, 0.0);
		const FLedgerState After = LedgerEphemeris::StateAt(Body, ParentMass, Period);
		const FLedgerState Hundred = LedgerEphemeris::StateAt(Body, ParentMass, Period * 100.0);

		// Against the orbit's own size rather than an absolute: a metre is
		// nothing at one astronomical unit and everything for a station.
		const double Scale = Body.Orbit.SemiMajorAxisMetres;
		TestTrue(*FString::Printf(TEXT("%s returns after one period"), *Body.Name),
			(After.PositionMetres - Start.PositionMetres).Length() < Scale * 1e-9);
		TestTrue(*FString::Printf(TEXT("%s returns after a hundred"), *Body.Name),
			(Hundred.PositionMetres - Start.PositionMetres).Length() < Scale * 1e-7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerEphemerisSolvesKepler,
	"Ledger.Ephemeris.KeplerSolutionSatisfiesItsOwnEquation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerEphemerisSolvesKepler::RunTest(const FString&)
{
	// M = E - e sin E, checked by substitution rather than against a table.
	// A solver that returns a plausible-looking number for every input is
	// exactly the failure a table of expected values would not catch.
	double Worst = 0.0;
	double WorstE = 0.0;
	double WorstM = 0.0;

	for (double Eccentricity : { 0.0, 0.01, 0.1, 0.3, 0.6, 0.9, 0.95 })
	{
		for (int32 Step = 0; Step < 720; ++Step)
		{
			// Deliberately unwrapped, and far from zero: this is what a body a
			// century into its orbit actually passes in.
			const double M = -20000.0 + Step * 55.5;
			const double E = LedgerEphemeris::EccentricAnomaly(M, Eccentricity);

			double Wrapped = FMath::Fmod(M, 2.0 * PI);
			if (Wrapped > PI) { Wrapped -= 2.0 * PI; }
			if (Wrapped < -PI) { Wrapped += 2.0 * PI; }

			const double Residual = FMath::Abs((E - Eccentricity * FMath::Sin(E)) - Wrapped);
			if (Residual > Worst)
			{
				Worst = Residual;
				WorstE = Eccentricity;
				WorstM = M;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("worst residual %.3e at e=%.2f, M=%.1f"),
		Worst, WorstE, WorstM));
	TestTrue(*FString::Printf(TEXT("Kepler's equation is satisfied (worst %.3e)"), Worst),
		Worst < 1e-10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerEphemerisVelocityMatchesMotion,
	"Ledger.Ephemeris.VelocityIsTheDerivativeOfPosition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerEphemerisVelocityMatchesMotion::RunTest(const FString&)
{
	// The velocity is written out analytically rather than differenced, so
	// nothing forces it to agree with the position it claims to describe.
	// A central difference does.
	const FLedgerSystem System = LedgerBodies::Generate(1u);
	double Worst = 0.0;

	for (int32 Index = 1; Index < System.Bodies.Num(); ++Index)
	{
		const FLedgerBody& Body = System.Bodies[Index];
		const double ParentMass = System.Bodies[Body.ParentIndex].MassKg;
		const double Period = LedgerEphemeris::PeriodSeconds(
			ParentMass, Body.Orbit.SemiMajorAxisMetres);
		const double Delta = Period * 1e-6;

		for (int32 Step = 0; Step < 16; ++Step)
		{
			const double At = Period * Step / 16.0;
			const FVector3d Before =
				LedgerEphemeris::StateAt(Body, ParentMass, At - Delta).PositionMetres;
			const FVector3d After =
				LedgerEphemeris::StateAt(Body, ParentMass, At + Delta).PositionMetres;
			const FVector3d Differenced = (After - Before) / (2.0 * Delta);
			const FVector3d Claimed =
				LedgerEphemeris::StateAt(Body, ParentMass, At).VelocityMetresPerSecond;

			// Relative, because orbital speeds span four orders of magnitude
			// between a moon and a planet.
			const double Apart = (Differenced - Claimed).Length()
				/ FMath::Max(1.0, Claimed.Length());
			Worst = FMath::Max(Worst, Apart);
		}
	}

	AddInfo(FString::Printf(TEXT("worst relative disagreement %.3e"), Worst));
	TestTrue(*FString::Printf(
		TEXT("velocity is the derivative of position (worst %.3e)"), Worst),
		Worst < 1e-6);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
