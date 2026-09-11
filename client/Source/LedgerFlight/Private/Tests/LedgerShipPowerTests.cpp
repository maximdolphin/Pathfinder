// Power: enough of it, too little of it, and none of it. T110.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipPowerSheds,
	"Ledger.Ships.AnOverloadedBusShedsTheLowestPriorityFirst",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipPowerSheds::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}
	auto Names = [&Ship](const TArray<int32>& Indices)
	{
		TArray<FString> Out;
		for (const int32 Index : Indices)
		{
			Out.Add(Ship.Components[Index].Id);
		}
		return FString::Join(Out, TEXT(", "));
	};
	const int32 Reactor = Ship.FindComponent(TEXT("reactor"));
	const int32 Radiator = Ship.FindComponent(TEXT("radiator"));

	// Enough: everything powered.
	FLedgerShipState State = FLedgerShipState::For(Ship);
	FLedgerPowerReport Report = LedgerShipSystems::SolvePower(Ship, State);
	AddInfo(FString::Printf(TEXT("whole: %.0f kW made, %.0f asked, %.0f delivered"), Report.SupplyKw, Report.DemandKw, Report.DeliveredKw));
	TestTrue(TEXT("a whole courier powers everything"), Report.Shed.Num() == 0 && Report.DeliveredKw == Report.DemandKw);

	// Too little: the reactor at a quarter makes 100 kW for 215 asked. The
	// configuration's order is computer 0, life support 1, main engine 2, RCS
	// 3, so the RCS goes, then the main engine -- and not the RCS alone, though
	// it would fit, because the order is the order.
	State = FLedgerShipState::For(Ship);
	State.Components[Reactor].Condition = 0.25;
	Report = LedgerShipSystems::SolvePower(Ship, State);
	AddInfo(FString::Printf(TEXT("a quarter reactor: %.0f kW made, %.0f asked; shed %s"), Report.SupplyKw, Report.DemandKw, *Names(Report.Shed)));
	TestEqual(TEXT("the lowest priorities drop, in the order configured"), Names(Report.Shed), FString(TEXT("rcs, main_engine")));
	TestTrue(TEXT("and life support and the computer stay up"),
		State.Components[Ship.FindComponent(TEXT("life_support"))].bPowered && State.Components[Ship.FindComponent(TEXT("computer"))].bPowered);

	// None: the radiator gone, so the reactor's coolant is cut and it makes nothing.
	State = FLedgerShipState::For(Ship);
	State.Components[Radiator].Condition = 0.0;
	Report = LedgerShipSystems::SolvePower(Ship, State);
	AddInfo(FString::Printf(TEXT("no radiator: %.0f kW made; reactor fed %d"), Report.SupplyKw,
		LedgerShipSystems::IsFed(Ship, State, Reactor) ? 1 : 0));
	TestTrue(TEXT("a reactor with its coolant cut makes nothing"), Report.SupplyKw == 0.0 && !State.Components[Reactor].bPowered);
	TestTrue(TEXT("and nothing downstream is powered"), Report.DeliveredKw == 0.0);
	return true;
}

#endif
