// The ship rig: every failure mode in M05, reproduced without flying. T130.
//
// One ship off the shelf, one failure at a time, and what the graph is
// supposed to do about it. The dedicated tests beside this one say why each
// behaves as it does; this one is the list, so a change that breaks one
// failure mode is named here by the mode it broke.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FRigCase
	{
		const TCHAR* Mode;
		TFunction<bool(const FLedgerShipDefinition&, FString&)> Run;
	};

	int32 RigFind(const FLedgerShipDefinition& Ship, const TCHAR* Id)
	{
		return Ship.FindComponent(Id);
	}

	void RigRun(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double Seconds, double Step = 1.0)
	{
		for (double Elapsed = 0.0; Elapsed < Seconds; Elapsed += Step)
		{
			LedgerShipSystems::SolvePower(Ship, State);
			LedgerShipSystems::StepThermal(Ship, State, Step);
			LedgerShipSystems::StepActuators(Ship, State, Step);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipRig,
	"Ledger.Ships.Rig.EveryFailureModeReproducesWithoutFlying",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipRig::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}

	const FRigCase Cases[] =
	{
		{ TEXT("overload sheds in priority order"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			State.Components[RigFind(S, TEXT("reactor"))].Condition = 0.25;
			const FLedgerPowerReport Report = LedgerShipSystems::SolvePower(S, State);
			Said = FString::Printf(TEXT("%d shed, first %s"), Report.Shed.Num(), Report.Shed.Num() > 0 ? *S.Components[Report.Shed[0]].Id : TEXT("none"));
			return Report.Shed.Num() == 2 && S.Components[Report.Shed[0]].Id == TEXT("rcs");
		} },
		{ TEXT("lost coolant stops the reactor"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			State.Components[RigFind(S, TEXT("radiator"))].Condition = 0.0;
			const double Made = LedgerShipSystems::SolvePower(S, State).SupplyKw;
			Said = FString::Printf(TEXT("%.0f kW made"), Made);
			return Made == 0.0;
		} },
		{ TEXT("reactor fuel out stops the reactor"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			State.Components[RigFind(S, TEXT("reactor_fuel"))].ContentsKg = 0.0;
			const double Made = LedgerShipSystems::SolvePower(S, State).SupplyKw;
			Said = FString::Printf(TEXT("%.0f kW made"), Made);
			return Made == 0.0;
		} },
		{ TEXT("propellant dry stops the engine"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			State.Components[RigFind(S, TEXT("tank"))].ContentsKg = 0.0;
			const double Given = LedgerShipSystems::Burn(S, State, RigFind(S, TEXT("main_engine")), 1.0e6, 1.0);
			Said = FString::Printf(TEXT("%.2f of the thrust given"), Given);
			return Given == 0.0;
		} },
		{ TEXT("a destroyed coupling kills exactly what is downstream"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			LedgerShipSystems::Damage(S, State, RigFind(S, TEXT("coupling_aft")), 1.0);
			LedgerShipSystems::SolvePower(S, State);
			const bool bEngine = State.Components[RigFind(S, TEXT("main_engine"))].bPowered;
			const bool bLife = State.Components[RigFind(S, TEXT("life_support"))].bPowered;
			Said = FString::Printf(TEXT("engine powered %d, life support powered %d"), bEngine ? 1 : 0, bLife ? 1 : 0);
			return !bEngine && bLife;
		} },
		{ TEXT("radiators in overheat and throttle the reactor"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			State.Components[RigFind(S, TEXT("radiator"))].bDeployed = false;
			RigRun(S, State, 900.0);
			const double K = State.Components[RigFind(S, TEXT("reactor"))].TemperatureK;
			Said = FString::Printf(TEXT("reactor at %.0f K after fifteen minutes"), K);
			return K > FLedgerThermal::ThrottleK;
		} },
		{ TEXT("a breach vents one compartment"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			const FLedgerHullHit Hit = LedgerShipSystems::HitHull(S, State, FVector3d(-3.0, 0.0, 0.0), 1.0e6);
			for (int32 Step = 0; Step < 120; ++Step)
			{
				LedgerShipSystems::StepAtmosphere(S, State, 0.0, 1.0);
			}
			int32 Vented = 0;
			for (const FLedgerCompartmentState& C : State.Compartments)
			{
				Vented += C.PressurePa < 50000.0 ? 1 : 0;
			}
			Said = FString::Printf(TEXT("%d of %d compartments vented"), Vented, State.Compartments.Num());
			return Hit.bPenetrated && Vented == 1;
		} },
		{ TEXT("enough hits fail the hull"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			for (int32 Hit = 0; Hit < 12; ++Hit)
			{
				LedgerShipSystems::HitHull(S, State, FVector3d(0.0, 0.0, 0.0), 8.0e6);
			}
			Said = FString::Printf(TEXT("hull at %.2f"), State.HullIntegrity);
			return State.HullIntegrity < FLedgerShipState::StructuralFailure;
		} },
		{ TEXT("wear asks for service before failing"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			const int32 Engine = RigFind(S, TEXT("main_engine"));
			for (int32 Hour = 0; Hour < 340; ++Hour)
			{
				LedgerShipSystems::SolvePower(S, State);
				LedgerShipSystems::Wear(S, State, 3600.0);
			}
			Said = FString::Printf(TEXT("engine at %.2f, service %d"), State.Components[Engine].Condition, State.Components[Engine].NeedsService() ? 1 : 0);
			return State.Components[Engine].NeedsService();
		} },
		{ TEXT("life support cut in vacuum warns"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			State.Components[RigFind(S, TEXT("life_support"))].bOn = false;
			ELedgerAirWarning Warned = ELedgerAirWarning::None;
			for (int32 Minute = 0; Minute < 12 * 60; ++Minute)
			{
				LedgerShipSystems::SolvePower(S, State);
				Warned |= LedgerShipSystems::StepLifeSupport(S, State, 0.0, 60.0);
			}
			Said = FString::Printf(TEXT("warnings %d"), static_cast<int32>(Warned));
			return EnumHasAnyFlags(Warned, ELedgerAirWarning::LowOxygen);
		} },
		{ TEXT("a dead sensor blanks its gauge"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			State.Components[RigFind(S, TEXT("reactor_sensor"))].Condition = 0.0;
			LedgerShipSystems::SolvePower(S, State);
			const FLedgerGaugeReading Reading = LedgerShipSystems::ReadGauge(S, State, 0);
			Said = Reading.Why;
			return !Reading.bAvailable;
		} },
		{ TEXT("a skipped start step stops the next"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = LedgerShipSystems::Cold(S);
			LedgerShipSystems::TryStart(S, State, RigFind(S, TEXT("reactor_fuel")));
			LedgerShipSystems::TryStart(S, State, RigFind(S, TEXT("radiator")));
			const bool bBus = LedgerShipSystems::TryStart(S, State, RigFind(S, TEXT("bus")), &Said);
			return !bBus;
		} },
		{ TEXT("gear cut mid-cycle freezes"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			const int32 Gear = RigFind(S, TEXT("landing_gear"));
			State.Components[Gear].DeployTarget = 1.0;
			RigRun(S, State, 2.0, 0.1);
			State.Components[RigFind(S, TEXT("reactor"))].bOn = false;
			const double Before = State.Components[Gear].Deployment;
			RigRun(S, State, 5.0, 0.1);
			Said = FString::Printf(TEXT("gear at %.2f, then %.2f"), Before, State.Components[Gear].Deployment);
			return Before > 0.2 && Before < 0.8 && State.Components[Gear].Deployment == Before;
		} },
		{ TEXT("raising shields takes thrust"), [](const FLedgerShipDefinition& S, FString& Said)
		{
			FLedgerShipState State = FLedgerShipState::For(S);
			const double Down = LedgerShipSystems::SolvePower(S, State).ThrustHeadroomKw;
			State.Components[RigFind(S, TEXT("shield"))].bOn = true;
			const double Up = LedgerShipSystems::SolvePower(S, State).ThrustHeadroomKw;
			Said = FString::Printf(TEXT("%.0f kW for thrust, then %.0f"), Down, Up);
			return Up < Down;
		} },
	};

	FString Table = TEXT("\n");
	int32 Passed = 0;
	for (const FRigCase& Case : Cases)
	{
		FString Said;
		const bool bPassed = Case.Run(Ship, Said);
		Passed += bPassed ? 1 : 0;
		Table += FString::Printf(TEXT("  %-52s %s  %s\n"), Case.Mode, bPassed ? TEXT("ok  ") : TEXT("FAIL"), *Said);
		TestTrue(Case.Mode, bPassed);
	}
	AddInfo(FString::Printf(TEXT("the rig: %d of %d failure modes behave%s"), Passed, static_cast<int32>(UE_ARRAY_COUNT(Cases)), *Table));
	return true;
}

#endif
