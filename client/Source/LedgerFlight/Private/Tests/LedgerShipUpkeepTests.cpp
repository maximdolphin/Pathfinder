// Wear, repair and the air inside. T116, T117 and T118.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool UpkeepCourier(FAutomationTestBase& Test, FLedgerShipDefinition& Ship)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipWear,
	"Ledger.Ships.ANeglectedThrusterLosesOutputAndWarnsBeforeItFails",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipWear::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the courier loads"), UpkeepCourier(*this, Ship)))
	{
		return false;
	}
	const int32 Engine = Ship.FindComponent(TEXT("main_engine"));
	FLedgerShipState State = FLedgerShipState::For(Ship);

	// A hundred hours of running, a minute at a time.
	double WarnedAt = -1.0;
	double FailedAt = -1.0;
	double At100 = 0.0;
	for (int32 Minute = 1; Minute <= 60 * 1000 && FailedAt < 0.0; ++Minute)
	{
		LedgerShipSystems::SolvePower(Ship, State);
		LedgerShipSystems::Wear(Ship, State, 60.0);
		// Everything else is serviced; only the engine is neglected. Left to
		// wear too, the reactor runs down first and sheds the engine, which then
		// stops wearing -- true, and not what this is about.
		for (int32 Other = 0; Other < State.Components.Num(); ++Other)
		{
			if (Other != Engine)
			{
				State.Components[Other].Condition = 1.0;
			}
		}
		const double Hours = Minute / 60.0;
		if (WarnedAt < 0.0 && State.Components[Engine].NeedsService())
		{
			WarnedAt = Hours;
		}
		if (State.Components[Engine].Condition <= 0.0)
		{
			FailedAt = Hours;
		}
		if (Minute == 6000)
		{
			At100 = LedgerShipSystems::ThrustShare(Ship, State, Engine);
		}
	}
	AddInfo(FString::Printf(TEXT("the main engine gives %.0f%% of its thrust after a hundred hours, asks for service at %.0f h, fails at %.0f h"),
		At100 * 100.0, WarnedAt, FailedAt));
	TestTrue(TEXT("a hundred hours costs it measurable thrust"), At100 < 0.9 && At100 > 0.0);
	TestTrue(TEXT("and it warns well before it fails"), WarnedAt > 0.0 && FailedAt > WarnedAt + 50.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipRepair,
	"Ledger.Ships.RepairsReachAStatedConditionAndSpendParts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipRepair::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the courier loads"), UpkeepCourier(*this, Ship)))
	{
		return false;
	}
	FLedgerShipState State = FLedgerShipState::For(Ship);
	const int32 Engine = Ship.FindComponent(TEXT("main_engine"));
	const int32 Coupling = Ship.FindComponent(TEXT("coupling_aft"));
	State.Components[Engine].Condition = 0.4;
	State.Components[Coupling].Condition = 0.0;
	const double SparesBefore = State.SparesKg;

	const FLedgerRepair Field = LedgerShipSystems::Repair(Ship, State, Engine, false);
	AddInfo(FString::Printf(TEXT("field repair: %.2f to %.2f for %.1f kg of spares (%.1f left)"),
		Field.ConditionBefore, Field.ConditionAfter, Field.PartsKg, State.SparesKg));
	TestTrue(TEXT("a field repair reaches its stated condition"), Field.bDone && FMath::IsNearlyEqual(Field.ConditionAfter, LedgerShipSystems::FieldCeiling));
	TestTrue(TEXT("and spends the spares aboard"), FMath::IsNearlyEqual(SparesBefore - State.SparesKg, Field.PartsKg) && Field.PartsKg > 0.0);

	const FLedgerRepair Refused = LedgerShipSystems::Repair(Ship, State, Coupling, false);
	AddInfo(FString::Printf(TEXT("field repair of the destroyed coupling: %s"), *Refused.Why));
	TestTrue(TEXT("a destroyed coupling cannot be field repaired"), !Refused.bDone && State.Components[Coupling].Condition == 0.0);

	double Workshop = 500.0;
	const FLedgerRepair Mended = LedgerShipSystems::Repair(Ship, State, Engine, true, &Workshop);
	const FLedgerRepair Replaced = LedgerShipSystems::Repair(Ship, State, Coupling, true, &Workshop);
	AddInfo(FString::Printf(TEXT("workshop: engine %.2f to %.2f for %.1f kg; coupling replaced for %.1f kg; %.1f kg left in stock"),
		Mended.ConditionBefore, Mended.ConditionAfter, Mended.PartsKg, Replaced.PartsKg, Workshop));
	TestTrue(TEXT("a workshop makes it whole"), Mended.bDone && Mended.ConditionAfter == 1.0);
	TestTrue(TEXT("and replaces what was destroyed"), Replaced.bDone && State.Components[Coupling].Condition == 1.0);
	TestTrue(TEXT("from its own stock"), FMath::IsNearlyEqual(500.0 - Workshop, Mended.PartsKg + Replaced.PartsKg));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipAir,
	"Ledger.Ships.CutLifeSupportInVacuumAndTheAirRunsDownOnSchedule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipAir::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	if (!TestTrue(TEXT("the courier loads"), UpkeepCourier(*this, Ship)) || Ship.Compartments.Num() == 0)
	{
		return false;
	}
	const int32 LifeSupport = Ship.FindComponent(TEXT("life_support"));

	// Kept: a day in vacuum with it running, and no warnings.
	FLedgerShipState Kept = FLedgerShipState::For(Ship);
	ELedgerAirWarning Worst = ELedgerAirWarning::None;
	for (int32 Minute = 0; Minute < 24 * 60; ++Minute)
	{
		LedgerShipSystems::SolvePower(Ship, Kept);
		Worst |= LedgerShipSystems::StepLifeSupport(Ship, Kept, 0.0, 60.0);
	}
	AddInfo(FString::Printf(TEXT("a day in vacuum with life support: cockpit %.0f Pa, oxygen %.0f Pa, carbon dioxide %.0f Pa, %.2f kg of reserve used"),
		Kept.Compartments[0].PressurePa, Kept.Compartments[0].OxygenPa, Kept.Compartments[0].CarbonDioxidePa,
		FLedgerShipState::For(Ship).OxygenReserveKg - Kept.OxygenReserveKg));
	TestTrue(TEXT("running, it keeps the air without a warning"), Worst == ELedgerAirWarning::None);

	// Cut: the prediction first. Oxygen in the cockpit falls by leakage as
	// exp(-t / tau) and by breathing at a steady r pascals a second, so
	// P(t) = (P0 + r tau) exp(-t / tau) - r tau, and it reaches the warning at
	// t = tau ln((P0 + r tau) / (Pwarn + r tau)).
	const double Volume = Ship.Compartments[0].VolumeM3();
	const double R = Ship.Hull.Crew * LedgerShipSystems::OxygenKgPerPersonDay / 86400.0 / 0.032 * 8.314462618 * 293.0 / Volume;
	const double Tau = LedgerShipSystems::LeakHours * 3600.0;
	const double Predicted = Tau * FMath::Loge((21200.0 + R * Tau) / (LedgerShipSystems::LowOxygenPa + R * Tau));

	FLedgerShipState Cut = FLedgerShipState::For(Ship);
	Cut.Components[LifeSupport].bOn = false;
	double WarnedAt = -1.0;
	for (int32 Second = 1; Second <= 3 * 86400 && WarnedAt < 0.0; Second += 10)
	{
		LedgerShipSystems::SolvePower(Ship, Cut);
		const ELedgerAirWarning Warnings = LedgerShipSystems::StepLifeSupport(Ship, Cut, 0.0, 10.0);
		if (EnumHasAnyFlags(Warnings, ELedgerAirWarning::LowOxygen))
		{
			WarnedAt = Second + 9.0;
		}
	}
	AddInfo(FString::Printf(TEXT("life support cut in vacuum: the low-oxygen warning at %.2f h against %.2f h predicted; carbon dioxide %.0f Pa by then"),
		WarnedAt / 3600.0, Predicted / 3600.0, Cut.Compartments[0].CarbonDioxidePa));
	TestTrue(TEXT("cut, the air runs down and it warns"), WarnedAt > 0.0);
	TestTrue(TEXT("on the schedule the model predicts, within one per cent"), FMath::Abs(WarnedAt - Predicted) < 0.01 * Predicted + 20.0);
	return true;
}

#endif
