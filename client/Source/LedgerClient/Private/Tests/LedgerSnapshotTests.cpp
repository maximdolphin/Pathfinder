// Tier 6 client tests. Design §11.
//
// These run in the editor's automation runner, and headless via
// `UnrealEditor-Cmd.exe <project> -ExecCmds="Automation RunTests Ledger"`.
// They exercise the wire boundary only — no world, no rendering — because the
// boundary is where a typed contract either holds or quietly does not.

#include "LedgerSnapshot.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* MinimalSnapshot = TEXT(R"JSON(
{
  "schema": 1,
  "seed": 7,
  "tick": 1200,
  "eventCount": 999,
  "regions": [ { "id": 0, "name": "Corvid Station" }, { "id": 1, "name": "Auger Flats" } ],
  "corps": [
    { "id": 0, "name": "Aurel Holdings", "cash": "1.50", "cashRaw": 1500000, "lanes": 2, "home": 0 },
    { "id": 1, "name": "Meridian Lien Group", "cash": "-0.25", "cashRaw": -250000, "lanes": 1, "home": 1 }
  ],
  "laneOwners": [0, 0, 1],
  "contracts": [
    { "id": 0, "archetype": "Bounty", "poster": "Aurel Holdings", "target": "Ilse Vance",
      "targetId": 4, "posted": 900, "tensionIncurred": 120, "resolved": false }
  ],
  "investigation": { "contract": 0, "target": "Ilse Vance", "inquiries": 3, "searchVolume": [0, 1] }
}
)JSON");

}

// ApplicationContextMask rather than EditorContext: these touch nothing but the
// parser, so they run wherever the code runs - including a game build, which is
// the only target this machine can currently link (the editor target needs a
// .NET Framework SDK that is not installed). Written out at each site because
// the flag operators are not constexpr in 5.8.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLedgerSnapshotParsesTest,
	"Ledger.Snapshot.ParsesAWellFormedDocument",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSnapshotParsesTest::RunTest(const FString&)
{
	FLedgerSnapshot Snapshot;
	FString Error;
	TestTrue(TEXT("parses"), ParseLedgerSnapshot(MinimalSnapshot, Snapshot, Error));
	TestEqual(TEXT("tick"), Snapshot.Tick, static_cast<int64>(1200));
	TestEqual(TEXT("corps"), Snapshot.Corps.Num(), 2);
	TestEqual(TEXT("lane owners"), Snapshot.LaneOwners.Num(), 3);
	TestEqual(TEXT("region lookup"), Snapshot.RegionName(1), FString(TEXT("Auger Flats")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLedgerFixedPointTest,
	"Ledger.Snapshot.KeepsFixedPointExact",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFixedPointTest::RunTest(const FString&)
{
	// §5.2: authoritative values cross as scaled integers. A negative balance is
	// meaningful state — it is what triggers a distressed sale — so it must
	// survive the boundary intact rather than being clamped or rounded away.
	FLedgerSnapshot Snapshot;
	FString Error;
	TestTrue(TEXT("parses"), ParseLedgerSnapshot(MinimalSnapshot, Snapshot, Error));
	TestEqual(TEXT("raw cash preserved"), Snapshot.Corps[1].CashRaw, static_cast<int64>(-250000));
	TestTrue(TEXT("negative survives"), Snapshot.Corps[1].CashDisplay() < 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLedgerUnknownArchetypeTest,
	"Ledger.Snapshot.RefusesAnUnknownArchetype",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerUnknownArchetypeTest::RunTest(const FString&)
{
	// §8.2: an unrecognised classification is a parse failure, never a silent
	// default. A board showing "Unknown" is worse than one that refuses to
	// load — the first is wrong, the second is obvious.
	const FString Bad = FString(MinimalSnapshot).Replace(TEXT("\"Bounty\""), TEXT("\"Extortion\""));
	FLedgerSnapshot Snapshot;
	FString Error;
	TestFalse(TEXT("refuses"), ParseLedgerSnapshot(Bad, Snapshot, Error));
	TestTrue(TEXT("names the offender"), Error.Contains(TEXT("Extortion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLedgerSchemaMismatchTest,
	"Ledger.Snapshot.RefusesAFutureSchema",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSchemaMismatchTest::RunTest(const FString&)
{
	const FString Future = FString(MinimalSnapshot).Replace(TEXT("\"schema\": 1"), TEXT("\"schema\": 99"));
	FLedgerSnapshot Snapshot;
	FString Error;
	TestFalse(TEXT("refuses"), ParseLedgerSnapshot(Future, Snapshot, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLedgerConservationTest,
	"Ledger.Snapshot.DetectsBrokenTerritoryConservation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerConservationTest::RunTest(const FString&)
{
	FLedgerSnapshot Snapshot;
	FString Error;
	TestTrue(TEXT("parses"), ParseLedgerSnapshot(MinimalSnapshot, Snapshot, Error));

	FString ConservationError;
	TestTrue(TEXT("well-formed snapshot conserves"), Snapshot.ValidateConservation(ConservationError));

	// Lose a lane in transit; the client must notice.
	Snapshot.LaneOwners.Pop();
	TestFalse(TEXT("a dropped lane is caught"), Snapshot.ValidateConservation(ConservationError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLedgerTensionTest,
	"Ledger.Snapshot.FlagsManufacturedTension",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerTensionTest::RunTest(const FString&)
{
	// §6.6 / §12: the client can check for itself that a contract is consequent
	// rather than templated, because the sim ships both ticks.
	FLedgerSnapshot Snapshot;
	FString Error;
	TestTrue(TEXT("parses"), ParseLedgerSnapshot(MinimalSnapshot, Snapshot, Error));
	TestTrue(TEXT("consequent"), Snapshot.Contracts[0].ReferencesPreExistingTension());

	Snapshot.Contracts[0].TensionIncurredTick = Snapshot.Contracts[0].PostedTick;
	TestFalse(TEXT("manufactured"), Snapshot.Contracts[0].ReferencesPreExistingTension());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
