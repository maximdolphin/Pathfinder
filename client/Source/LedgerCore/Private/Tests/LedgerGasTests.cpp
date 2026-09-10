// Descending into something with no floor. T079.
//
// The acceptance is that the ship is destroyed at the depth the atmosphere model
// PREDICTS -- so the prediction and the descent have to be two things. The
// prediction is a logarithm. The descent is a loop that steps down, asks the
// pressure at each step, and stops when the hull gives. They meet in the middle
// or the model is decoration.

#include "LedgerGas.h"
#include "LedgerSky.h"
#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// What a hull takes before it stops being one. 50 bar.
	///
	/// A submarine's crush depth is a few hundred metres of water, which is
	/// about 50 bar, and a spacecraft hull is built to hold one atmosphere IN
	/// rather than fifty out. Fifty is generous to the ship and it is the
	/// number the fixture uses, so the two cannot disagree.
	constexpr double HullLimitPascals = 50.0 * LedgerGas::OneBarPascals;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGasHydrostatic,
	"Ledger.Gas.PressureMatchesTheIntegratedHydrostaticEquation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGasHydrostatic::RunTest(const FString&)
{
	// The closed form against the differential equation it solves. One is an
	// exponential; the other steps dP = rho g dh downwards in metre slices with
	// rho recomputed from the pressure at each slice. They share the physics
	// and nothing else.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const int32 Giant = LedgerBodies::FirstOfKind(System, ELedgerBodyKind::GasGiant);
	TestTrue(TEXT("the system has a gas giant"), System.Bodies.IsValidIndex(Giant));

	const FLedgerBody& Body = System.Bodies[Giant];
	const double Temperature = LedgerSky::EquilibriumTemperatureKelvin(System, Giant, 0.0);
	const double Height = LedgerGas::ScaleHeightMetres(Body, Temperature);

	AddInfo(FString::Printf(
		TEXT("gas giant: %.0f km radius, %.2f m/s^2, %.0f K, scale height %.1f km"),
		Body.RadiusMetres / 1000.0, LedgerGas::GravityAt(Body, 0.0),
		Temperature, Height / 1000.0));

	constexpr double Boltzmann = 1.380649e-23;
	const double Mass = LedgerGas::MolecularMassKg(Body);
	const double Gravity = LedgerGas::GravityAt(Body, 0.0);

	double Pressure = LedgerGas::OneBarPascals;
	constexpr double Step = 5.0;
	double Worst = 0.0;
	FString Table;
	for (double Depth = 0.0; Depth < 150000.0; Depth += Step)
	{
		// rho = P m / (k T), and dP = rho g dh going down.
		const double Density = Pressure * Mass / (Boltzmann * Temperature);
		Pressure += Density * Gravity * Step;

		const double Closed = LedgerGas::PressurePascals(Body, Temperature, Depth + Step);
		const double Drift = FMath::Abs(Pressure - Closed) / Closed;
		Worst = FMath::Max(Worst, Drift);

		if (FMath::IsNearlyZero(FMath::Fmod(Depth + Step, 30000.0), 1e-6))
		{
			Table += FString::Printf(
				TEXT("  %6.0f km down: integrated %.4g Pa, closed form %.4g Pa\n"),
				(Depth + Step) / 1000.0, Pressure, Closed);
		}
	}
	AddInfo(FString::Printf(TEXT("stepping the hydrostatic equation down:\n%s"), *Table));
	AddInfo(FString::Printf(TEXT("worst relative disagreement %.3e"), Worst));

	// Not exact: the integration is first order, so it accumulates about half a
	// step of error per scale height. Five-metre steps against a 30 km scale
	// height is a ten-thousandth.
	TestTrue(*FString::Printf(
		TEXT("the exponential is the solution of the equation (worst %.3e)"), Worst),
		Worst < 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGasCrushDepth,
	"Ledger.Gas.TheHullFailsWhereThePredictionSaysItWill",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGasCrushDepth::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const int32 Giant = LedgerBodies::FirstOfKind(System, ELedgerBodyKind::GasGiant);
	const FLedgerBody& Body = System.Bodies[Giant];
	const double Temperature = LedgerSky::EquilibriumTemperatureKelvin(System, Giant, 0.0);

	// **The prediction, before anything descends.** A logarithm.
	const double Predicted =
		LedgerGas::DepthForPressure(Body, Temperature, HullLimitPascals);

	// **The descent.** A metre at a time until the hull gives.
	double Failed = -1.0;
	for (double Depth = 0.0; Depth < 1.0e6; Depth += 1.0)
	{
		if (LedgerGas::PressurePascals(Body, Temperature, Depth) >= HullLimitPascals)
		{
			Failed = Depth;
			break;
		}
	}

	AddInfo(FString::Printf(
		TEXT("predicted crush depth %.1f km; the descent failed at %.1f km"),
		Predicted / 1000.0, Failed / 1000.0));
	TestTrue(TEXT("the descent ended"), Failed > 0.0);
	TestTrue(*FString::Printf(
		TEXT("the hull fails within a metre of the prediction (%.3f m apart)"),
		FMath::Abs(Failed - Predicted)),
		FMath::Abs(Failed - Predicted) < 1.0);

	// And the pressure genuinely rises on the way down, which is the other half
	// of the acceptance and is not implied by where it ends.
	double Previous = 0.0;
	FString Profile;
	for (const double Depth : { 0.0, 10000.0, 25000.0, 50000.0, 100000.0, Predicted })
	{
		const double Here = LedgerGas::PressurePascals(Body, Temperature, Depth);
		Profile += FString::Printf(TEXT("  %6.1f km: %8.2f bar\n"),
			Depth / 1000.0, Here / LedgerGas::OneBarPascals);
		TestTrue(*FString::Printf(TEXT("pressure rises by %.1f km"), Depth / 1000.0),
			Here > Previous);
		Previous = Here;
	}
	AddInfo(FString::Printf(TEXT("down the well:\n%s"), *Profile));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGasAgainstJupiter,
	"Ledger.Gas.TheModelAgreesWithJupiter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGasAgainstJupiter::RunTest(const FString&)
{
	// The one place this can be checked against a body somebody has measured.
	//
	// **The EQUATORIAL radius, and that is not a detail.** Jupiter's quoted
	// surface gravity of 24.79 m/s^2 is GM over the equatorial radius of
	// 71,492 km. Using the volumetric mean radius of 69,911 km -- which is the
	// figure the generator uses, and reasonably -- gives 25.92, and this test
	// failed against 24.79 until it was asked with the radius that number was
	// quoted at. Two right answers to two different questions.
	FLedgerBody Jupiter;
	Jupiter.Kind = ELedgerBodyKind::GasGiant;
	Jupiter.MassKg = 1.898e27;
	Jupiter.RadiusMetres = 7.1492e7;

	const double Gravity = LedgerGas::GravityAt(Jupiter, 0.0);
	const double Height = LedgerGas::ScaleHeightMetres(Jupiter, 165.0);
	AddInfo(FString::Printf(
		TEXT("Jupiter: gravity %.2f m/s^2 (measured 24.79), scale height %.1f km "
			 "(measured about 27)"), Gravity, Height / 1000.0));

	TestEqual(TEXT("Jupiter's gravity"), Gravity, 24.79, 0.1);

	// **About a tenth low, and the reason is known.** This model carries one
	// mean molecular mass of 2.3 for every giant; Jupiter's is nearer 2.22, and
	// its temperature is not constant with depth the way an isothermal model
	// assumes. Both push the same way. 24 km against a measured 27 is close
	// enough for a body a ship descends into and not close enough to call the
	// model finished, so the bound is quoted with the reason rather than opened
	// until it passes quietly.
	AddInfo(FString::Printf(
		TEXT("that is %.0f%% below the measured 27 km, from carrying one mean "
			 "molecular mass for every giant and an isothermal profile"),
		(1.0 - Height / 27000.0) * 100.0));
	TestTrue(*FString::Printf(TEXT("Jupiter's scale height (%.1f km)"), Height / 1000.0),
		Height > 21000.0 && Height < 30000.0);

	// And the branch that matters: the same body made of nitrogen instead of
	// hydrogen would have an atmosphere a fourteenth as deep.
	FLedgerBody AsRock = Jupiter;
	AsRock.Kind = ELedgerBodyKind::Planet;
	const double RockHeight = LedgerGas::ScaleHeightMetres(AsRock, 165.0);
	AddInfo(FString::Printf(
		TEXT("the same body breathing nitrogen: %.1f km, a factor of %.1f"),
		RockHeight / 1000.0, Height / RockHeight));
	TestEqual(TEXT("hydrogen against nitrogen is the mass ratio"),
		Height / RockHeight, 28.0 / 2.3, 0.01);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
