// Gauges that cannot lie, a refit shown before it is made, and a ship that
// reloads exactly as it was. T125, T128 and T129.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool RecordLoad(FAutomationTestBase& Test, FLedgerShipDefinition& Ship)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipGauges,
	"Ledger.Ships.EveryGaugeTracesToAReadingAndADeadSensorBlanksIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipGauges::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the courier loads"), RecordLoad(*this, Ship)) || Ship.Instruments.Num() == 0)
	{
		AddError(TEXT("the courier needs instruments"));
		return false;
	}
	FLedgerShipState State = FLedgerShipState::For(Ship);
	LedgerShipSystems::SolvePower(Ship, State);
	LedgerShipSystems::StepThermal(Ship, State, 30.0);

	int32 Traced = 0;
	for (int32 Index = 0; Index < Ship.Instruments.Num(); ++Index)
	{
		const FLedgerGaugeReading Reading = LedgerShipSystems::ReadGauge(Ship, State, Index);
		TestTrue(FString::Printf(TEXT("%s reads"), *Ship.Instruments[Index].Id), Reading.bAvailable);
		Traced += Reading.bAvailable ? 1 : 0;
	}
	const int32 ReactorGauge = Ship.Instruments.IndexOfByPredicate([](const FLedgerInstrument& I) { return I.Id == TEXT("reactor_temperature"); });
	const int32 FuelGauge = Ship.Instruments.IndexOfByPredicate([](const FLedgerInstrument& I) { return I.Id == TEXT("propellant"); });
	TestEqual(TEXT("the reactor gauge is the reactor's own temperature"),
		LedgerShipSystems::ReadGauge(Ship, State, ReactorGauge).Value, State.Components[Ship.FindComponent(TEXT("reactor"))].TemperatureK);

	// The reactor's own thermocouple destroyed: its gauge goes blank, the rest read on.
	State.Components[Ship.FindComponent(TEXT("reactor_sensor"))].Condition = 0.0;
	LedgerShipSystems::SolvePower(Ship, State);
	const FLedgerGaugeReading Blank = LedgerShipSystems::ReadGauge(Ship, State, ReactorGauge);
	AddInfo(FString::Printf(TEXT("%d gauges traced; with the reactor sensor gone the reactor gauge says: %s"), Traced, *Blank.Why));
	TestTrue(TEXT("a failed sensor makes its gauge unavailable rather than wrong"), !Blank.bAvailable && Blank.Value == 0.0);
	TestTrue(TEXT("and leaves the others reading"), LedgerShipSystems::ReadGauge(Ship, State, FuelGauge).bAvailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipOutfit,
	"Ledger.Ships.AnOversizedPlantShowsItsMassHeatAndHandlingBeforeFitting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipOutfit::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the courier loads"), RecordLoad(*this, Ship)))
	{
		return false;
	}
	const FLedgerComponent Old = Ship.Components[Ship.FindComponent(TEXT("reactor"))];
	FLedgerComponent Big = Old;
	Big.Id = TEXT("reactor_large");
	Big.MassKg = 1600.0;
	Big.Params.Add(TEXT("outputKw"), 700.0);
	Big.Params.Add(TEXT("slotClass"), 2.0);
	FLedgerComponent Huge = Big;
	Huge.Params.Add(TEXT("slotClass"), 3.0);

	const FLedgerOutfitPreview TooBig = LedgerShips::PreviewSwap(Ship, TEXT("reactor"), Huge);
	AddInfo(FString::Printf(TEXT("a class 3 plant: %s"), *TooBig.Why));
	TestFalse(TEXT("a plant too big for the slot is refused"), TooBig.bFits);

	const FLedgerOutfitPreview Preview = LedgerShips::PreviewSwap(Ship, TEXT("reactor"), Big);
	AddInfo(FString::Printf(TEXT("the 700 kW plant: %+.0f kg, %+.0f kW of heat flat out, %+.0f kW of margin; main thrust %.0f to %.0f m/s2; roll inertia %.0f to %.0f kg m2"),
		Preview.MassDeltaKg, Preview.HeatDeltaKw, Preview.PowerMarginDeltaKw,
		Preview.AccelerationBefore, Preview.AccelerationAfter, Preview.RollInertiaBefore, Preview.RollInertiaAfter));
	TestTrue(TEXT("it fits"), Preview.bFits);
	TestTrue(TEXT("and the preview shows the mass it adds"), FMath::IsNearlyEqual(Preview.MassDeltaKg, Big.MassKg - Old.MassKg));
	TestTrue(TEXT("the heat it makes"), Preview.HeatDeltaKw > 0.0);
	TestTrue(TEXT("and the handling it costs"), Preview.AccelerationAfter < Preview.AccelerationBefore);
	TestTrue(TEXT("while the ship itself is untouched until it is fitted"),
		Ship.Components[Ship.FindComponent(TEXT("reactor"))].MassKg == Old.MassKg);
	FLedgerShipState Fitted = FLedgerShipState::For(Preview.After);
	TestEqual(TEXT("fitted, it makes 700 kW"), LedgerShipSystems::SolvePower(Preview.After, Fitted).SupplyKw, 700.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipPersist,
	"Ledger.Ships.ADamagedHalfFuelledRefittedShipReloadsExactly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipPersist::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the courier loads"), RecordLoad(*this, Ship)))
	{
		return false;
	}
	// Oddly configured: a laser on the dorsal mount.
	FLedgerComponent Laser;
	Laser.Id = TEXT("mining_laser");
	Laser.Type = TEXT("Mounted");
	Laser.MassKg = 180.0;
	Laser.Params.Add(TEXT("mountClass"), 2.0);
	Laser.Params.Add(TEXT("drawKw"), 60.0);
	FLedgerPort Power;
	Power.Name = TEXT("power");
	Power.bRequired = true;
	Laser.Ports.Add(Power);
	FString Why;
	TestTrue(TEXT("the laser mounts"), LedgerShips::Mount(Ship, Laser, TEXT("hp_dorsal"), Why));

	// Damaged and half fuelled, and run for a while.
	FLedgerShipState State = FLedgerShipState::For(Ship);
	State.Components[Ship.FindComponent(TEXT("tank"))].ContentsKg *= 0.5;
	LedgerShipSystems::Damage(Ship, State, Ship.FindComponent(TEXT("main_engine")), 0.37);
	LedgerShipSystems::HitHull(Ship, State, FVector3d(-3.0, 0.0, 0.0), 3.0e5);
	State.Components[Ship.FindComponent(TEXT("landing_gear"))].DeployTarget = 1.0;
	for (int32 Second = 0; Second < 30; ++Second)
	{
		LedgerShipSystems::SolvePower(Ship, State);
		LedgerShipSystems::StepThermal(Ship, State, 1.0);
		LedgerShipSystems::StepActuators(Ship, State, 0.1);
		LedgerShipSystems::StepAtmosphere(Ship, State, 0.0, 1.0);
		LedgerShipSystems::Wear(Ship, State, 1.0);
	}

	// Save both halves, and load them back into nothing.
	const FString Configuration = LedgerShips::ToJson(Ship);
	const FString Condition = LedgerShipSystems::SaveState(Ship, State);
	FLedgerShipDefinition Reloaded;
	TArray<FString> Errors;
	TestTrue(TEXT("the configuration reloads"), LedgerShips::Parse(Configuration, Reloaded, Errors));
	FLedgerShipState Restored;
	TestTrue(TEXT("the condition reloads"), LedgerShipSystems::LoadState(Reloaded, Condition, Restored, Errors));
	for (const FString& Error : Errors)
	{
		AddError(Error);
	}

	// Exactly: the same text out of the reloaded ship as went in.
	TestEqual(TEXT("the configuration survives exactly"), LedgerShips::ToJson(Reloaded), Configuration);
	TestEqual(TEXT("and so does every number of its condition"), LedgerShipSystems::SaveState(Reloaded, Restored), Condition);
	AddInfo(FString::Printf(TEXT("saved %d characters of configuration and %d of condition; engine at %.17g, tank at %.17g kg, engineering at %.17g Pa"),
		Configuration.Len(), Condition.Len(), Restored.Components[Reloaded.FindComponent(TEXT("main_engine"))].Condition,
		Restored.Components[Reloaded.FindComponent(TEXT("tank"))].ContentsKg, Restored.Compartments.Last().PressurePa));
	return true;
}

#endif
