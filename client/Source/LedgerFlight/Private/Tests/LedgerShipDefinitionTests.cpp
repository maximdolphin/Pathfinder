// Ships as data, and the ways a ship file can be wrong. T108 and T109.

#include "LedgerShipDefinition.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipsLoad,
	"Ledger.Ships.EveryShippedShipLoadsAndItsGraphIsDiscoverable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipsLoad::RunTest(const FString&)
{
	for (const TCHAR* Name : { TEXT("courier"), TEXT("hauler") })
	{
		FLedgerShipDefinition Ship;
		TArray<FString> Errors;
		const bool bLoaded = LedgerShips::Load(LedgerShips::DefaultDirectory(), Name, Ship, Errors);
		for (const FString& Error : Errors)
		{
			AddError(Error);
		}
		TestTrue(FString::Printf(TEXT("%s loads"), Name), bLoaded);
		if (!bLoaded)
		{
			continue;
		}
		AddInfo(FString::Printf(TEXT("%s: %d components, %d connections, %.1f t, %.0f m/s2 main thrust"),
			Name, Ship.Components.Num(), Ship.Connections.Num(), Ship.MassKg() / 1000.0, Ship.Flight.MainThrust));

		// Discoverable at runtime: what the bus feeds, and what that feeds, found
		// from the graph alone.
		const int32 Bus = Ship.FindComponent(TEXT("bus"));
		bool bFeedsEngine = false;
		TArray<int32> Open = { Bus };
		TSet<int32> Seen;
		while (Bus != INDEX_NONE && Open.Num() > 0)
		{
			const int32 At = Open.Pop();
			for (int32 Port = 0; Port < Ship.Components[At].Ports.Num(); ++Port)
			{
				if (!Ship.Components[At].Ports[Port].bOutput)
				{
					continue;
				}
				for (const TPair<int32, int32>& Link : Ship.ConnectedTo(At, Port))
				{
					bFeedsEngine |= Ship.Components[Link.Key].Id == TEXT("main_engine");
					if (!Seen.Contains(Link.Key) && Ship.Components[Link.Key].Type == TEXT("Coupling"))
					{
						Seen.Add(Link.Key);
						Open.Add(Link.Key);
					}
				}
			}
		}
		TestTrue(TEXT("the bus is found to feed the main engine, through its coupling"), bFeedsEngine);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipsRefuse,
	"Ledger.Ships.AnUnsatisfiableConnectionIsALoadErrorNotACrash",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipsRefuse::RunTest(const FString&)
{
	// The smallest ship that works, and then one mistake at a time.
	const FString Good = TEXT(R"({
		"name": "probe",
		"components": [
			{ "id": "cell", "type": "PowerPlant", "ports": [ { "name": "power", "kind": "power", "dir": "out" } ] },
			{ "id": "lamp", "type": "Light", "ports": [
				{ "name": "power", "kind": "power", "dir": "in", "required": true },
				{ "name": "heat", "kind": "coolant", "dir": "in" } ] }
		],
		"connections": [ ["cell.power", "lamp.power"] ]
	})");
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	TestTrue(TEXT("the smallest ship loads"), LedgerShips::Parse(Good, Ship, Errors));

	struct FBroken
	{
		const TCHAR* What;
		FString From;
		FString To;
		const TCHAR* Says;
	};
	const FBroken Broken[] =
	{
		{ TEXT("a port that is not there"), TEXT("[\"cell.power\", \"lamp.power\"]"), TEXT("[\"cell.power\", \"lamp.socket\"]"), TEXT("no port") },
		{ TEXT("a component that is not there"), TEXT("[\"cell.power\", \"lamp.power\"]"), TEXT("[\"battery.power\", \"lamp.power\"]"), TEXT("no component") },
		{ TEXT("power into coolant"), TEXT("[\"cell.power\", \"lamp.power\"]"), TEXT("[\"cell.power\", \"lamp.power\"], [\"cell.power\", \"lamp.heat\"]"), TEXT("cannot feed") },
		{ TEXT("an input wired as the source"), TEXT("[\"cell.power\", \"lamp.power\"]"), TEXT("[\"lamp.power\", \"cell.power\"]"), TEXT("output to an input") },
		{ TEXT("a required input left open"), TEXT("\"connections\": [ [\"cell.power\", \"lamp.power\"] ]"), TEXT("\"connections\": []"), TEXT("not connected") },
		{ TEXT("an unknown port kind"), TEXT("\"kind\": \"coolant\""), TEXT("\"kind\": \"steam\""), TEXT("not a port kind") },
		{ TEXT("not JSON at all"), TEXT("{"), TEXT("<"), TEXT("not a JSON object") },
	};
	for (const FBroken& Case : Broken)
	{
		FString Json = Good;
		Json.ReplaceInline(*Case.From, *Case.To);
		FLedgerShipDefinition Out;
		TArray<FString> Said;
		const bool bLoaded = LedgerShips::Parse(Json, Out, Said);
		const bool bExplained = Said.ContainsByPredicate([&Case](const FString& Line) { return Line.Contains(Case.Says); });
		TestTrue(FString::Printf(TEXT("%s is refused, and says why"), Case.What), !bLoaded && bExplained);
		AddInfo(FString::Printf(TEXT("%s: %s"), Case.What, Said.Num() > 0 ? *Said[0] : TEXT("(nothing)")));
	}
	return true;
}

#endif
