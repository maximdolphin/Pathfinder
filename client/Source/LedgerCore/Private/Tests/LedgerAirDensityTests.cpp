// Density with height, and the pressure the air puts on what moves through it.
// T102.
//
// Two tests. The first checks the density against the barometric law it is
// meant to be, at the one atmosphere everybody can look up: Earth's sea-level
// air is 1.225 kg/m3 and the density at the top of Everest is a little over a
// third of it. The second checks that "above the atmosphere" is a place and not
// a limit -- the profile stops at twelve scale heights, so past that the answer
// is exactly zero rather than a millionth, and the wind can be as fast as it
// likes without being heard.

#include "LedgerAir.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAirDensityWithHeight,
	"Ledger.Air.DensityFollowsTheBarometricLaw",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAirDensityWithHeight::RunTest(const FString&)
{
	const FLedgerAirProfile Earth = LedgerAir::Describe(
		ELedgerAir::NitrogenOxygen, 101325.0, 288.0, 9.807, 6.371e6);

	const double Sea = LedgerAir::DensityAt(Earth, 0.0);
	AddInfo(FString::Printf(
		TEXT("sea level %.3f kg/m3 against a measured 1.225"), Sea));
	TestTrue(TEXT("sea-level density is within 5% of 1.225 kg/m3"),
		FMath::Abs(Sea - 1.225) < 0.062);

	// e-folding is the definition of a scale height, so this is checking the
	// arithmetic rather than the physics -- but it is the arithmetic that a
	// caller open-coding exp(-h/H) gets wrong by writing H in kilometres.
	const double OneScaleHeight = LedgerAir::DensityAt(Earth, Earth.ScaleHeightMetres);
	TestTrue(TEXT("one scale height up is 1/e of the surface"),
		FMath::Abs(OneScaleHeight / Sea - FMath::Exp(-1.0)) < 1e-9);

	// 8,849 m. The published density there is about 0.45 kg/m3; an isothermal
	// profile overstates it slightly because the real air up there is colder,
	// which is a known and stated property of this model rather than a bug.
	const double Everest = LedgerAir::DensityAt(Earth, 8849.0);
	AddInfo(FString::Printf(
		TEXT("Everest %.3f kg/m3 against a measured 0.45"), Everest));
	TestTrue(TEXT("the summit of Everest is between a third and a half of sea level"),
		Everest / Sea > 0.33 && Everest / Sea < 0.50);

	// Dynamic pressure is the density times half the speed squared, and the
	// thinner air is the whole reason the same wind is quieter up there.
	const double Below = LedgerAir::DynamicPressure(Earth, 0.0, 30.0);
	const double Above = LedgerAir::DynamicPressure(Earth, 8849.0, 30.0);
	AddInfo(FString::Printf(
		TEXT("30 m/s is %.0f Pa at sea level and %.0f Pa on the summit"),
		Below, Above));
	TestTrue(TEXT("half rho v squared, at sea level"),
		FMath::Abs(Below - 0.5 * Sea * 900.0) < 1e-9);
	TestTrue(TEXT("the same wind is weaker in thinner air"), Above < Below * 0.55);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAirVacuumIsAPlace,
	"Ledger.Air.AboveTheAtmosphereIsExactlyZero",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAirVacuumIsAPlace::RunTest(const FString&)
{
	const FLedgerAirProfile Earth = LedgerAir::Describe(
		ELedgerAir::NitrogenOxygen, 101325.0, 288.0, 9.807, 6.371e6);

	TestTrue(TEXT("the top is twelve scale heights up"),
		FMath::Abs(Earth.TopMetres - 12.0 * Earth.ScaleHeightMetres) < 1.0);

	const double JustBelow = LedgerAir::DensityAt(Earth, Earth.TopMetres - 1.0);
	const double AtTheTop = LedgerAir::DensityAt(Earth, Earth.TopMetres);
	AddInfo(FString::Printf(
		TEXT("a metre below the top %.3e kg/m3, at it %.3e"), JustBelow, AtTheTop));
	TestTrue(TEXT("there is still a trace a metre below the top"), JustBelow > 0.0);
	TestTrue(TEXT("at and above the top there is nothing at all"),
		AtTheTop == 0.0
		&& LedgerAir::DensityAt(Earth, Earth.TopMetres * 10.0) == 0.0);

	// A gale in vacuum is silent because there is nothing for it to push, which
	// is the half of T102's acceptance that cannot be got right by fading
	// something out with altitude.
	TestTrue(TEXT("a hundred metres a second above the air is zero pressure"),
		LedgerAir::DynamicPressure(Earth, Earth.TopMetres, 100.0) == 0.0);

	// An airless body has no density anywhere, not even at the datum.
	const FLedgerAirProfile None = LedgerAir::Describe(
		ELedgerAir::None, 0.0, 120.0, 1.28, 1.976e6);
	TestTrue(TEXT("an airless world is vacuum at the ground"),
		LedgerAir::DensityAt(None, 0.0) == 0.0);

	return true;
}

#endif
