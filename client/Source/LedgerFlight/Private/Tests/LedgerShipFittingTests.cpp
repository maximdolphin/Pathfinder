// Mounts, holds and gear. T121, T122 and T123.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool FittingLoad(FAutomationTestBase& Test, const TCHAR* Name, FLedgerShipDefinition& Ship)
	{
		TArray<FString> Errors;
		const bool bLoaded = LedgerShips::Load(LedgerShips::DefaultDirectory(), Name, Ship, Errors);
		for (const FString& Error : Errors)
		{
			Test.AddError(Error);
		}
		return bLoaded;
	}

	FLedgerComponent FittingItem(const TCHAR* Id, int32 MountClass, double DrawKw)
	{
		FLedgerComponent Item;
		Item.Id = Id;
		Item.Type = TEXT("Mounted");
		Item.MassKg = 180.0;
		Item.Params.Add(TEXT("mountClass"), MountClass);
		Item.Params.Add(TEXT("drawKw"), DrawKw);
		Item.Params.Add(TEXT("priority"), 1.8);
		FLedgerPort Power;
		Power.Name = TEXT("power");
		Power.Kind = ELedgerPortKind::Power;
		Power.bRequired = true;
		Item.Ports.Add(Power);
		return Item;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipMounts,
	"Ledger.Ships.AMountTakesItsClassAndItsDrawShowsInTheTotals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipMounts::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the courier loads"), FittingLoad(*this, TEXT("courier"), Ship)) || Ship.Hardpoints.Num() < 2)
	{
		AddError(TEXT("the courier needs two hardpoints"));
		return false;
	}
	FLedgerShipState Bare = FLedgerShipState::For(Ship);
	const FLedgerPowerReport Before = LedgerShipSystems::SolvePower(Ship, Bare);

	FString Why;
	TestFalse(TEXT("a medium mining laser is refused by the small nose mount"),
		LedgerShips::Mount(Ship, FittingItem(TEXT("mining_laser"), 2, 60.0), TEXT("hp_nose"), Why));
	AddInfo(FString::Printf(TEXT("the nose mount: %s"), *Why));
	TestTrue(TEXT("and taken by the medium dorsal one"),
		LedgerShips::Mount(Ship, FittingItem(TEXT("mining_laser"), 2, 60.0), TEXT("hp_dorsal"), Why));
	TestFalse(TEXT("a second thing on an occupied mount is refused"),
		LedgerShips::Mount(Ship, FittingItem(TEXT("scanner"), 2, 10.0), TEXT("hp_dorsal"), Why));
	AddInfo(FString::Printf(TEXT("the dorsal mount again: %s"), *Why));

	FLedgerShipState Fitted = FLedgerShipState::For(Ship);
	const FLedgerPowerReport After = LedgerShipSystems::SolvePower(Ship, Fitted);
	LedgerShipSystems::StepThermal(Ship, Fitted, 1.0);
	const int32 Laser = Ship.FindComponent(TEXT("mining_laser"));
	AddInfo(FString::Printf(TEXT("demand %.0f kW bare, %.0f kW with the laser; the laser makes %.1f kW of heat"),
		Before.DemandKw, After.DemandKw, Fitted.Components[Laser].HeatKw));
	TestEqual(TEXT("its draw appears in the ship's power"), After.DemandKw - Before.DemandKw, 60.0);
	TestTrue(TEXT("and its heat in the ship's heat"), Fitted.Components[Laser].HeatKw > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipHold,
	"Ledger.Ships.AHoldFillsByVolumeOrMassWhicheverBindsFirst",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipHold::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the hauler loads"), FittingLoad(*this, TEXT("hauler"), Ship)))
	{
		return false;
	}
	const int32 Hold = Ship.FindComponent(TEXT("cargo_hold"));
	const double MaxKg = Ship.Components[Hold].Param(TEXT("maxKg"));
	const double Volume = Ship.Components[Hold].Param(TEXT("volumeM3"));

	// Lead: a cubic metre weighs eleven tonnes, so mass binds.
	FLedgerShipState Dense = FLedgerShipState::For(Ship);
	int32 Crates = 0;
	FString Why;
	while (LedgerShipSystems::Stow(Ship, Dense, Hold, { TEXT("lead"), 11000.0, 1.0 }, &Why))
	{
		++Crates;
	}
	AddInfo(FString::Printf(TEXT("lead: %d crates, %.0f of %.0f kg and %.0f of %.0f m3, then '%s'"),
		Crates, Dense.Components[Hold].ContentsKg, MaxKg, LedgerShipSystems::HoldVolumeUsedM3(Dense, Hold), Volume, *Why));
	TestTrue(TEXT("dense cargo is stopped by mass"), Why == TEXT("too heavy") && Crates == static_cast<int32>(MaxKg / 11000.0));

	// Foam: fifty kilograms a cubic metre, so volume binds.
	FLedgerShipState Light = FLedgerShipState::For(Ship);
	Crates = 0;
	while (LedgerShipSystems::Stow(Ship, Light, Hold, { TEXT("foam"), 50.0, 1.0 }, &Why))
	{
		++Crates;
	}
	AddInfo(FString::Printf(TEXT("foam: %d crates, %.0f kg and %.0f m3, then '%s'"),
		Crates, Light.Components[Hold].ContentsKg, LedgerShipSystems::HoldVolumeUsedM3(Light, Hold), *Why));
	TestTrue(TEXT("light cargo is stopped by volume"), Why == TEXT("no room") && Crates == static_cast<int32>(Volume));

	// And what is stowed weighs the ship down where the hold is.
	TestTrue(TEXT("stowed cargo is in the ship's mass"),
		LedgerShipSystems::MassProperties(Ship, Dense).MassKg - LedgerShipSystems::MassProperties(Ship, FLedgerShipState::For(Ship)).MassKg
			== Dense.Components[Hold].ContentsKg);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipGear,
	"Ledger.Ships.GearCutMidCycleStaysHalfDownUntilPowerReturns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipGear::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the courier loads"), FittingLoad(*this, TEXT("courier"), Ship)))
	{
		return false;
	}
	const int32 Gear = Ship.FindComponent(TEXT("landing_gear"));
	const int32 Reactor = Ship.FindComponent(TEXT("reactor"));
	if (!TestTrue(TEXT("it has landing gear"), Gear != INDEX_NONE))
	{
		return false;
	}
	FLedgerShipState State = FLedgerShipState::For(Ship);
	auto Run = [&Ship, &State](double Seconds)
	{
		for (double Elapsed = 0.0; Elapsed < Seconds - 1.0e-9; Elapsed += 0.1)
		{
			LedgerShipSystems::SolvePower(Ship, State);
			LedgerShipSystems::StepActuators(Ship, State, 0.1);
		}
	};

	const double Idle = LedgerShipSystems::DrawKw(Ship, State, Gear);
	State.Components[Gear].DeployTarget = 1.0;
	const double Moving = LedgerShipSystems::DrawKw(Ship, State, Gear);
	Run(Ship.Components[Gear].Param(TEXT("travelSeconds")) * 0.5);
	const double Half = State.Components[Gear].Deployment;

	State.Components[Reactor].bOn = false;
	Run(10.0);
	const double Stuck = State.Components[Gear].Deployment;

	State.Components[Reactor].bOn = true;
	Run(Ship.Components[Gear].Param(TEXT("travelSeconds")));
	AddInfo(FString::Printf(TEXT("gear draws %.0f kW idle and %.0f kW moving; %.2f down at half travel, %.2f after ten seconds without power, %.2f when it returns"),
		Idle, Moving, Half, Stuck, State.Components[Gear].Deployment));
	TestTrue(TEXT("it draws only while it moves"), Idle == 0.0 && Moving > 0.0);
	TestTrue(TEXT("cut mid-cycle it stays where it was"), FMath::Abs(Half - 0.5) < 0.05 && Stuck == Half);
	TestTrue(TEXT("and finishes when power returns"), State.Components[Gear].Deployment == 1.0);
	return true;
}

#endif
