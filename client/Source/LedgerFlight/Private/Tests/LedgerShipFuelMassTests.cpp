// Fuel burnt against thrust given, and cargo on one side. T112 and T113.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool LoadCourier(FAutomationTestBase& Test, FLedgerShipDefinition& Ship)
	{
		TArray<FString> Errors;
		const bool bLoaded = LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors);
		for (const FString& Error : Errors)
		{
			Test.AddError(Error);
		}
		return bLoaded;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipFuel,
	"Ledger.Ships.FuelBurntMatchesThrustGivenAndDryStopsTheEngines",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipFuel::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the courier loads"), LoadCourier(*this, Ship)))
	{
		return false;
	}
	FLedgerShipState State = FLedgerShipState::For(Ship);
	const int32 Engine = Ship.FindComponent(TEXT("main_engine"));
	const double Exhaust = Ship.Components[Engine].Param(TEXT("ispSeconds")) * 9.80665;
	const double Before = LedgerShipSystems::FuelKg(Ship, State, 0);

	// Ten minutes of a flight's worth of throttle: a climb, a cruise, a stop.
	double Impulse = 0.0;
	for (int32 Second = 0; Second < 600; ++Second)
	{
		const double Throttle = Second < 120 ? 0.02 : (Second < 480 ? 0.004 : 0.01);
		const double Thrust = Throttle * Ship.Flight.MainThrust * Ship.MassKg();
		const double Given = LedgerShipSystems::Burn(Ship, State, Engine, Thrust, 1.0);
		Impulse += Thrust * Given;
	}
	const double Burnt = Before - LedgerShipSystems::FuelKg(Ship, State, 0);
	AddInfo(FString::Printf(TEXT("ten minutes: %.2f MN s of impulse, %.3f kg burnt, %.3f expected from the Isp"),
		Impulse / 1.0e6, Burnt, Impulse / Exhaust));
	TestTrue(TEXT("fuel burnt integrates to thrust given"), FMath::Abs(Burnt - Impulse / Exhaust) <= 1.0e-6 * Burnt);
	TestTrue(TEXT("and reactor fuel is a separate thing the engine does not touch"),
		LedgerShipSystems::FuelKg(Ship, State, 1) == Ship.Components[Ship.FindComponent(TEXT("reactor_fuel"))].Param(TEXT("capacityKg")));

	// Full throttle until the tank is dry.
	int32 Seconds = 0;
	double Given = 1.0;
	while (Given > 0.0 && Seconds < 100000)
	{
		Given = LedgerShipSystems::Burn(Ship, State, Engine, Ship.Flight.MainThrust * Ship.MassKg(), 1.0);
		++Seconds;
	}
	AddInfo(FString::Printf(TEXT("full throttle ran dry after %d s"), Seconds));
	TestTrue(TEXT("running dry stops the engine"), Given == 0.0 && LedgerShipSystems::FuelKg(Ship, State, 0) <= 1.0e-9);

	// A reactor out of its own fuel is no longer fed, so it makes nothing.
	FLedgerShipState Cold = FLedgerShipState::For(Ship);
	Cold.Components[Ship.FindComponent(TEXT("reactor_fuel"))].ContentsKg = 0.0;
	TestTrue(TEXT("a reactor with no fuel makes no power"), LedgerShipSystems::SolvePower(Ship, Cold).SupplyKw == 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipMass,
	"Ledger.Ships.CargoOnOneSideHandlesAsPredicted",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipMass::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the hauler loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("hauler"), Ship, Errors)))
	{
		return false;
	}
	FLedgerShipState Even = FLedgerShipState::For(Ship);
	const FLedgerMassProperties Balanced = LedgerShipSystems::MassProperties(Ship, Even);

	// Six tonnes in a hold moved two metres to starboard.
	FLedgerShipDefinition Loaded = Ship;
	const int32 Hold = Loaded.FindComponent(TEXT("cargo_hold"));
	Loaded.Components[Hold].PositionMetres.Y += 2.0;
	FLedgerShipState Cargo = FLedgerShipState::For(Loaded);
	Cargo.Components[Hold].ContentsKg = 6000.0;
	const FLedgerMassProperties Heavy = LedgerShipSystems::MassProperties(Loaded, Cargo);

	// The prediction: the centre of mass moves to starboard; lift through the
	// hull's middle then has a lever arm, and the torque it makes rolls the
	// loaded side down -- a negative roll rate, x forward and z up.
	const double Lift = 1.0e5;
	const FVector3d Arm = FVector3d::ZeroVector - Heavy.CentreMetres;
	const FVector3d Torque = FVector3d::CrossProduct(Arm, FVector3d(0.0, 0.0, Lift));
	const FVector3d Roll = Heavy.Respond(Torque);
	const FVector3d Level = Balanced.Respond(FVector3d::CrossProduct(FVector3d::ZeroVector - Balanced.CentreMetres, FVector3d(0.0, 0.0, Lift)));
	AddInfo(FString::Printf(TEXT("balanced: %.1f t, centre y %+.3f m, roll inertia %.0f kg m2, lift rolls it %+.5f rad/s2"),
		Balanced.MassKg / 1000.0, Balanced.CentreMetres.Y, Balanced.Inertia[0][0], Level.X));
	AddInfo(FString::Printf(TEXT("loaded to starboard: %.1f t, centre y %+.3f m, roll inertia %.0f kg m2, lift rolls it %+.5f rad/s2"),
		Heavy.MassKg / 1000.0, Heavy.CentreMetres.Y, Heavy.Inertia[0][0], Roll.X));
	TestTrue(TEXT("the centre of mass moves towards the cargo"), Heavy.CentreMetres.Y > Balanced.CentreMetres.Y + 0.1);
	TestTrue(TEXT("the ship is harder to roll"), Heavy.Inertia[0][0] > Balanced.Inertia[0][0]);
	TestTrue(TEXT("and lift rolls the loaded side down, where the balanced ship does not roll"),
		Roll.X < 0.0 && FMath::Abs(Roll.X) > 10.0 * FMath::Abs(Level.X));
	return true;
}

#endif
