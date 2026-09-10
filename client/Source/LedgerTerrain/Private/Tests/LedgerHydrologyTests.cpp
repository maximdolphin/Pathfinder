// The three things T055's acceptance asks of a river network.
//
//   Every river reaches the sea or a basin, none flows uphill, and the same
//   seed produces the same network.
//
// All three are properties of the flow field rather than of a picture, so all
// three are tests. They run at a coarse resolution: correctness does not depend
// on how fine the lattice is, and 6 x 48² builds in about a second.

#include "LedgerHydrology.h"

#include "Misc/AutomationTest.h"
#include "LedgerMath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 TestResolution = 48;

	FLedgerTerrainParams ThisPlanet(int32 Seed = 1337)
	{
		FLedgerTerrainParams Params;
		Params.Seed = Seed;
		Params.Radius = 637100000.0;
		Params.MaxElevation = 900000.0;
		Params.SeaLevel = 0.14;
		return Params;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerHydrologyNeverFlowsUphill,
	"Ledger.Hydrology.NoCellFlowsUphill",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerHydrologyNeverFlowsUphill::RunTest(const FString&)
{
	const FLedgerFlowField Field =
		LedgerHydrology::BuildFlowField(ThisPlanet(), TestResolution);

	// Strictly lower, not "no higher". Equal heights would admit a two-cell
	// cycle that passes a <= check and hangs every trace through it.
	for (int32 Index = 0; Index < Field.Num(); ++Index)
	{
		const int32 Below = Field.Downstream[Index];
		if (Below == INDEX_NONE)
		{
			continue;
		}
		if (!(Field.Height[Below] < Field.Height[Index]))
		{
			AddError(FString::Printf(
				TEXT("cell %d at %.1f m drains into %d at %.1f m"),
				Index, Field.Height[Index], Below, Field.Height[Below]));
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerHydrologyReachesTheSeaOrABasin,
	"Ledger.Hydrology.EveryCellReachesTheSeaOrABasin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerHydrologyReachesTheSeaOrABasin::RunTest(const FString&)
{
	const FLedgerFlowField Field =
		LedgerHydrology::BuildFlowField(ThisPlanet(), TestResolution);

	// A cap well above any possible path length, so a failure here is a cycle
	// rather than a long river. The lattice is 6 x 48² = 13,824 cells and no
	// path can visit one twice while strictly descending.
	const int32 MaxSteps = Field.Num();

	int32 ToSea = 0;
	int32 ToBasin = 0;
	for (int32 Index = 0; Index < Field.Num(); ++Index)
	{
		int32 Steps = 0;
		const int32 End = LedgerHydrology::TraceToTerminal(Field, Index, MaxSteps, Steps);
		if (End == INDEX_NONE)
		{
			AddError(FString::Printf(
				TEXT("cell %d did not reach a terminal in %d steps"), Index, MaxSteps));
			return false;
		}

		const ELedgerFlowTerminal Kind = Field.Terminal[End];
		if (Kind == ELedgerFlowTerminal::Sea) { ++ToSea; }
		else if (Kind == ELedgerFlowTerminal::Basin) { ++ToBasin; }
		else
		{
			AddError(FString::Printf(
				TEXT("cell %d ended at %d, which claims to still be flowing"), Index, End));
			return false;
		}
	}

	// Not an assertion about the ratio -- that is the terrain's business and
	// the report says what it is. This only catches the degenerate case where
	// the whole planet is one kind, which would mean the sea test or the basin
	// test is broken rather than that the world is unusual.
	TestTrue(TEXT("some water reaches the sea"), ToSea > 0);
	TestTrue(TEXT("some water ends in a basin"), ToBasin > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerHydrologyIsDeterministic,
	"Ledger.Hydrology.SameSeedSameNetwork",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerHydrologyIsDeterministic::RunTest(const FString&)
{
	const uint64 First = LedgerHydrology::NetworkHash(
		LedgerHydrology::BuildFlowField(ThisPlanet(), TestResolution));
	const uint64 Second = LedgerHydrology::NetworkHash(
		LedgerHydrology::BuildFlowField(ThisPlanet(), TestResolution));

	// The build runs on the task graph, so this is also a test that the
	// parallel decomposition has no ordering in it: a race in the neighbour
	// pass would show up here and nowhere else.
	TestEqual(TEXT("the same planet twice"), First, Second);

	const uint64 Other = LedgerHydrology::NetworkHash(
		LedgerHydrology::BuildFlowField(ThisPlanet(9001), TestResolution));
	TestNotEqual(TEXT("a different seed"), First, Other);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerHydrologyFlowIsConserved,
	"Ledger.Hydrology.FlowIsConserved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerHydrologyFlowIsConserved::RunTest(const FString&)
{
	const FLedgerFlowField Field =
		LedgerHydrology::BuildFlowField(ThisPlanet(), TestResolution);

	// Every cell contributes itself and passes on what it received, so the flow
	// arriving at the terminals is exactly the number of cells. An accumulation
	// pass that visited a cell twice, or in the wrong order, fails this and
	// nothing else notices -- the network would still look plausible and every
	// river would be the wrong size.
	double AtTerminals = 0.0;
	for (int32 Index = 0; Index < Field.Num(); ++Index)
	{
		if (Field.Downstream[Index] == INDEX_NONE)
		{
			AtTerminals += Field.Flow[Index];
		}
	}

	TestEqual(TEXT("flow reaching the terminals is the cell count"),
		AtTerminals, static_cast<double>(Field.Num()), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerHydrologyCellRoundTrips,
	"Ledger.Hydrology.CellRoundTripsThroughItsCentre",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerHydrologyCellRoundTrips::RunTest(const FString&)
{
	// The test the first version of this did not have, and would have failed.
	//
	// Neighbours were found by projecting a face position to the sphere and
	// asking DirectionToFace where it landed -- which skips CubeToSphere's
	// area-evening warp and lands several cells away near a face edge. Every
	// other test passed: the flow graph was consistent, nothing ran uphill,
	// nothing cycled, and none of the links joined cells that touch.
	FLedgerFlowField Field;
	Field.Resolution = TestResolution;

	int32 Checked = 0;
	for (int32 Index = 0; Index < Field.Num(); Index += 7)
	{
		const int32 Back = LedgerHydrology::CellAt(Field.Centre(Index), Field.Resolution);
		if (Back != Index)
		{
			AddError(FString::Printf(
				TEXT("cell %d round-trips through its own centre to %d"), Index, Back));
			return false;
		}
		++Checked;
	}
	TestTrue(TEXT("something was checked"), Checked > 100);

	// And a neighbour is next door: the great-circle distance between a cell
	// and each of its eight neighbours is at most two cell widths.
	const double CellArc = (LedgerPi * 0.5) / TestResolution;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 CellV = 0; CellV < TestResolution; CellV += 5)
		{
			for (int32 CellU = 0; CellU < TestResolution; CellU += 5)
			{
				const int32 Index = LedgerHydrology::CellIndex(
					static_cast<ELedgerCubeFace>(Face), CellU, CellV, TestResolution);
				const FVector3d Here = Field.Centre(Index);

				for (int32 OffsetV = -1; OffsetV <= 1; ++OffsetV)
				{
					for (int32 OffsetU = -1; OffsetU <= 1; ++OffsetU)
					{
						const int32 Other = LedgerHydrology::CellIndex(
							static_cast<ELedgerCubeFace>(Face),
							CellU + OffsetU, CellV + OffsetV, TestResolution);
						const double Angle = FMath::Acos(FMath::Clamp(
							FVector3d::DotProduct(Here, Field.Centre(Other)), -1.0, 1.0));
						if (Angle > CellArc * 2.5)
						{
							AddError(FString::Printf(
								TEXT("face %d cell (%d,%d) neighbour (%+d,%+d) is %.1f cells away"),
								Face, CellU, CellV, OffsetU, OffsetV, Angle / CellArc));
							return false;
						}
					}
				}
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
