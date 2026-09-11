// A power coupling destroyed, and exactly what is downstream of it lost. T114.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipCoupling,
	"Ledger.Ships.DestroyingACouplingKillsExactlyWhatIsDownstream",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipCoupling::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}
	const int32 Coupling = Ship.FindComponent(TEXT("coupling_aft"));
	if (!TestTrue(TEXT("it has an aft coupling"), Coupling != INDEX_NONE))
	{
		return false;
	}

	FLedgerShipState Before = FLedgerShipState::For(Ship);
	LedgerShipSystems::SolvePower(Ship, Before);

	FLedgerShipState After = FLedgerShipState::For(Ship);
	LedgerShipSystems::Damage(Ship, After, Coupling, 1.0);
	LedgerShipSystems::SolvePower(Ship, After);

	// What the topology says will go: the powered consumers downstream of it.
	// Downstream in power: a coupling carries power, and the coolant loop back
	// through the reactor is not the way its loss travels.
	const TArray<int32> Downstream = LedgerShipSystems::DownstreamOf(Ship, Coupling, true);
	TArray<FString> Predicted;
	TArray<FString> Lost;
	TArray<FString> Kept;
	for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
	{
		if (Index == Coupling || Ship.Components[Index].Param(TEXT("drawKw")) <= 0.0)
		{
			continue;
		}
		if (Downstream.Contains(Index))
		{
			Predicted.Add(Ship.Components[Index].Id);
		}
		if (Before.Components[Index].bPowered && !After.Components[Index].bPowered)
		{
			Lost.Add(Ship.Components[Index].Id);
		}
		else if (After.Components[Index].bPowered)
		{
			Kept.Add(Ship.Components[Index].Id);
		}
	}
	Predicted.Sort();
	Lost.Sort();
	AddInfo(FString::Printf(TEXT("downstream of the coupling: %s; lost: %s; still powered: %s"),
		*FString::Join(Predicted, TEXT(", ")), *FString::Join(Lost, TEXT(", ")), *FString::Join(Kept, TEXT(", "))));
	TestEqual(TEXT("destroying the coupling kills exactly what is downstream of it"), FString::Join(Lost, TEXT(",")), FString::Join(Predicted, TEXT(",")));
	TestTrue(TEXT("and nothing else"), Kept.Contains(TEXT("computer")) && Kept.Contains(TEXT("life_support")));

	// A hit bigger than a component spills along its connections.
	FLedgerShipState Blast = FLedgerShipState::For(Ship);
	const int32 Reactor = Ship.FindComponent(TEXT("reactor"));
	LedgerShipSystems::Damage(Ship, Blast, Reactor, 3.0);
	const double RadiatorLeft = Blast.Components[Ship.FindComponent(TEXT("radiator"))].Condition;
	const double ComputerLeft = Blast.Components[Ship.FindComponent(TEXT("computer"))].Condition;
	AddInfo(FString::Printf(TEXT("a reactor hit three times over: radiator at %.2f, computer at %.2f"), RadiatorLeft, ComputerLeft));
	TestTrue(TEXT("what a destroyed reactor has left over reaches what it is joined to"), RadiatorLeft < 1.0);
	TestTrue(TEXT("and not what it is not"), ComputerLeft == 1.0);
	return true;
}

#endif
