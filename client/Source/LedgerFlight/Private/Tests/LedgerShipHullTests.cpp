// A breach vents the compartment behind it and not the rest of the ship. T115.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipBreach,
	"Ledger.Ships.ABreachVentsTheCompartmentBehindItAndNotTheRest",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipBreach::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}
	const int32 Engineering = Ship.Compartments.IndexOfByPredicate([](const FLedgerCompartment& C) { return C.Id == TEXT("engineering"); });
	const int32 Cockpit = Ship.Compartments.IndexOfByPredicate([](const FLedgerCompartment& C) { return C.Id == TEXT("cockpit"); });
	if (!TestTrue(TEXT("it has compartments"), Engineering != INDEX_NONE && Cockpit != INDEX_NONE))
	{
		return false;
	}

	// A glancing hit the armour stops: no hole anywhere.
	FLedgerShipState State = FLedgerShipState::For(Ship);
	const FLedgerHullHit Glance = LedgerShipSystems::HitHull(Ship, State, FVector3d(-3.0, 0.5, 0.0), 2.0e4);
	TestTrue(TEXT("a hit the armour stops makes no hole"), !Glance.bPenetrated);

	// A megajoule into the engine room, in vacuum.
	const FLedgerHullHit Hit = LedgerShipSystems::HitHull(Ship, State, FVector3d(-3.0, 0.5, 0.0), 1.0e6);
	TestTrue(TEXT("a megajoule holes the engine room"), Hit.bPenetrated && Hit.Compartment == Engineering);
	const double Tau = LedgerShipSystems::VentSeconds(Ship.Compartments[Engineering].VolumeM3(), Hit.HoleM2, 293.0);

	double At5 = 0.0;
	for (int32 Step = 1; Step <= 600; ++Step)
	{
		LedgerShipSystems::StepAtmosphere(Ship, State, 0.0, 0.1);
		if (Step == 50)
		{
			At5 = State.Compartments[Engineering].PressurePa;
		}
	}
	const double Predicted5 = 101325.0 * FMath::Exp(-5.0 / Tau);
	AddInfo(FString::Printf(TEXT("a %.4f m2 hole in %.1f m3: time constant %.1f s; %.0f Pa at 5 s against %.0f predicted, %.0f Pa at a minute"),
		Hit.HoleM2, Ship.Compartments[Engineering].VolumeM3(), Tau, At5, Predicted5, State.Compartments[Engineering].PressurePa));
	TestTrue(TEXT("the engine room vents on the schedule the model predicts"), FMath::Abs(At5 - Predicted5) < 0.01 * Predicted5);
	TestTrue(TEXT("and is near vacuum after a minute"), State.Compartments[Engineering].PressurePa < 0.05 * 101325.0);
	bool bRestHeld = true;
	for (int32 Index = 0; Index < State.Compartments.Num(); ++Index)
	{
		bRestHeld &= Index == Engineering || State.Compartments[Index].PressurePa == 101325.0;
	}
	TestTrue(TEXT("and every other compartment keeps its air"), bRestHeld);
	TestTrue(TEXT("the hull as a structure has taken the hit"), State.HullIntegrity < 1.0 && State.HullIntegrity > FLedgerShipState::StructuralFailure);
	return true;
}

#endif
