// A cold start in the order the graph implies, and a skipped step. T124.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipColdStart,
	"Ledger.Ships.ColdStartComesUpInDependencyOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipColdStart::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}
	auto Id = [&Ship](int32 Index) { return Ship.Components[Index].Id; };
	const int32 Engine = Ship.FindComponent(TEXT("main_engine"));
	const int32 Reactor = Ship.FindComponent(TEXT("reactor"));
	const int32 Bus = Ship.FindComponent(TEXT("bus"));

	// The engine first, from cold: refused, and told why.
	FLedgerShipState Cold = LedgerShipSystems::Cold(Ship);
	FString Why;
	TestFalse(TEXT("a cold engine will not start before its power"), LedgerShipSystems::TryStart(Ship, Cold, Engine, &Why));
	AddInfo(FString::Printf(TEXT("starting the main engine cold: %s"), *Why));

	// The order, and that it respects every dependency.
	const TArray<int32> Order = LedgerShipSystems::StartupOrder(Ship);
	TArray<FString> Names;
	for (const int32 Index : Order)
	{
		Names.Add(Id(Index));
	}
	AddInfo(FString::Printf(TEXT("cold start: %s"), *FString::Join(Names, TEXT(" > "))));
	TestEqual(TEXT("everything comes up"), Order.Num(), Ship.Components.Num());
	auto Before = [&Order](int32 A, int32 B) { return Order.IndexOfByKey(A) < Order.IndexOfByKey(B); };
	TestTrue(TEXT("the reactor after its fuel and its coolant"),
		Before(Ship.FindComponent(TEXT("reactor_fuel")), Reactor) && Before(Ship.FindComponent(TEXT("radiator")), Reactor));
	TestTrue(TEXT("the bus after the reactor"), Before(Reactor, Bus));
	TestTrue(TEXT("the engine after the bus, its coupling and its tank"),
		Before(Bus, Engine) && Before(Ship.FindComponent(TEXT("coupling_aft")), Engine) && Before(Ship.FindComponent(TEXT("tank")), Engine));

	// Skip the reactor: the bus cannot come up, and nothing after it can.
	FLedgerShipState Skipped = LedgerShipSystems::Cold(Ship);
	int32 Refused = 0;
	for (const int32 Index : Order)
	{
		if (Index == Reactor)
		{
			continue;
		}
		Refused += LedgerShipSystems::TryStart(Ship, Skipped, Index) ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("with the reactor skipped, %d components refuse to start; the bus is on: %d"), Refused, Skipped.Components[Bus].bOn ? 1 : 0));
	TestTrue(TEXT("skipping the reactor stops the bus"), !Skipped.Components[Bus].bOn);
	TestTrue(TEXT("and everything that needs power after it"), !Skipped.Components[Engine].bOn && Refused >= 5);
	return true;
}

#endif
