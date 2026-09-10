#include "LedgerHydrologyReport.h"

#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "LedgerHydrology.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "LedgerMath.h"

bool ULedgerHydrologyReport::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerHydrologyReport::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!FParse::Param(FCommandLine::Get(), TEXT("hydrology")))
	{
		return;
	}

	const bool bHolds = WriteReport();
	UE_LOG(LogLedger, Log, TEXT("hydrology: %s"),
		bHolds ? TEXT("VERDICT PASS") : TEXT("VERDICT FAIL"));
	FPlatformMisc::RequestExit(false);
}

bool ULedgerHydrologyReport::WriteReport()
{
	ULedgerWorldBuilder* Builder = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Planet == nullptr)
	{
		UE_LOG(LogLedger, Error, TEXT("hydrology: no planet"));
		return false;
	}

	const FLedgerTerrainParams Params = Planet->TerrainParams();
	const int32 Resolution = LedgerHydrology::DefaultResolution;

	const double Started = FPlatformTime::Seconds();
	const FLedgerFlowField Field = LedgerHydrology::BuildFlowField(Params, Resolution);
	const double BuildSeconds = FPlatformTime::Seconds() - Started;

	// A cube face spans a quarter of the great circle, so a cell is that over
	// the resolution. Everything below is reported in kilometres because a
	// drainage basin in centimetres is not a number anybody can read.
	const double RadiusKm = Params.Radius / 100000.0;
	const double CellKm = (LedgerTwoPi * RadiusKm / 4.0) / Resolution;
	const double CellAreaKm2 = CellKm * CellKm;

	FString Body;
	Body += TEXT("River networks by flow accumulation (T055).\n\n");
	Body += FString::Printf(
		TEXT("lattice %d per cube face, %d cells, %.1f km apart, built in %.1f s\n\n"),
		Resolution, Field.Num(), CellKm, BuildSeconds);

	// ---- what the terminals are -------------------------------------------
	int32 SeaCells = 0;
	int32 LandCells = 0;
	int32 Basins = 0;
	double LandToSea = 0.0;
	double LandToBasin = 0.0;
	for (int32 Index = 0; Index < Field.Num(); ++Index)
	{
		if (Field.Terminal[Index] == ELedgerFlowTerminal::Sea)
		{
			++SeaCells;
			// Everything arriving here came overland, less this cell itself.
			LandToSea += Field.Flow[Index] - 1.0;
			continue;
		}
		++LandCells;
		if (Field.Terminal[Index] == ELedgerFlowTerminal::Basin)
		{
			++Basins;
			LandToBasin += Field.Flow[Index];
		}
	}

	Body += FString::Printf(TEXT("land %d cells (%.1f%%), sea %d\n"),
		LandCells, 100.0 * LandCells / Field.Num(), SeaCells);
	Body += FString::Printf(
		TEXT("of the land, %.1f%% drains to the sea and %.1f%% into %d closed basins\n"),
		100.0 * LandToSea / FMath::Max(1, LandCells),
		100.0 * LandToBasin / FMath::Max(1, LandCells),
		Basins);
	Body += TEXT("a basin is a cell with no lower neighbour; on a noise height field\n"
		"every dimple is one, so the count is a statement about the terrain.\n\n");

	// ---- the biggest rivers ------------------------------------------------
	//
	// A river mouth is a *land* cell that drains into the sea: the last cell
	// the water is still on land. Reporting sea cells instead would count the
	// same estuary once per adjacent sea cell.
	TArray<int32> Mouths;
	for (int32 Index = 0; Index < Field.Num(); ++Index)
	{
		const int32 Below = Field.Downstream[Index];
		if (Below != INDEX_NONE && Field.Terminal[Below] == ELedgerFlowTerminal::Sea)
		{
			Mouths.Add(Index);
		}
	}
	Mouths.Sort([&Field](int32 A, int32 B) { return Field.Flow[A] > Field.Flow[B]; });

	Body += FString::Printf(TEXT("%d river mouths. The twelve largest:\n"), Mouths.Num());
	Body += TEXT("      lat     lon    basin km2   cells   length km\n");

	int32 LongestSteps = 0;
	for (int32 Rank = 0; Rank < FMath::Min(12, Mouths.Num()); ++Rank)
	{
		const int32 Mouth = Mouths[Rank];
		const FVector3d Point = Field.Centre(Mouth);
		const double Latitude = FMath::RadiansToDegrees(
			FMath::Asin(FMath::Clamp(Point.Z, -1.0, 1.0)));
		const double Longitude = FMath::RadiansToDegrees(FMath::Atan2(Point.Y, Point.X));

		// Length along the longest tributary: walk up from the mouth, always
		// taking the feeder with the most flow.
		int32 Length = 0;
		int32 Current = Mouth;
		for (int32 Step = 0; Step < Field.Num(); ++Step)
		{
			int32 Biggest = INDEX_NONE;
			float BiggestFlow = 0.0f;

			// The feeders of a cell are among its own eight neighbours, so this
			// is a local search rather than a scan of the lattice -- and the
			// neighbours are taken in cube space, because going through the
			// sphere lands somewhere else entirely (LedgerHydrology.h).
			const int32 PerFace = Resolution * Resolution;
			const int32 Face = Current / PerFace;
			const int32 Within = Current - Face * PerFace;
			for (int32 OffsetV = -1; OffsetV <= 1; ++OffsetV)
			{
				for (int32 OffsetU = -1; OffsetU <= 1; ++OffsetU)
				{
					if (OffsetU == 0 && OffsetV == 0)
					{
						continue;
					}
					const int32 Other = LedgerHydrology::CellIndex(
						static_cast<ELedgerCubeFace>(Face),
						Within % Resolution + OffsetU, Within / Resolution + OffsetV,
						Resolution);
					if (Field.Downstream[Other] == Current && Field.Flow[Other] > BiggestFlow)
					{
						BiggestFlow = Field.Flow[Other];
						Biggest = Other;
					}
				}
			}
			if (Biggest == INDEX_NONE)
			{
				break;
			}
			Current = Biggest;
			++Length;
		}
		LongestSteps = FMath::Max(LongestSteps, Length);

		Body += FString::Printf(TEXT("  %7.2f %7.2f %11.0f %7.0f %11.0f\n"),
			Latitude, Longitude, Field.Flow[Mouth] * CellAreaKm2,
			Field.Flow[Mouth], Length * CellKm);
	}

	// ---- the acceptance ----------------------------------------------------
	int32 Uphill = 0;
	int32 Unterminated = 0;
	for (int32 Index = 0; Index < Field.Num(); ++Index)
	{
		const int32 Below = Field.Downstream[Index];
		if (Below != INDEX_NONE && !(Field.Height[Below] < Field.Height[Index]))
		{
			++Uphill;
		}
		int32 Steps = 0;
		if (LedgerHydrology::TraceToTerminal(Field, Index, Field.Num(), Steps) == INDEX_NONE)
		{
			++Unterminated;
		}
	}

	const uint64 Hash = LedgerHydrology::NetworkHash(Field);
	const uint64 Again = LedgerHydrology::NetworkHash(
		LedgerHydrology::BuildFlowField(Params, Resolution));

	Body += FString::Printf(TEXT("\ncells flowing uphill: %d\n"), Uphill);
	Body += FString::Printf(TEXT("cells not reaching a terminal: %d\n"), Unterminated);
	Body += FString::Printf(TEXT("network hash %llu, rebuilt %llu, %s\n"),
		Hash, Again, Hash == Again ? TEXT("same") : TEXT("DIFFERENT"));
	Body += FString::Printf(TEXT("longest river traced: %.0f km\n"), LongestSteps * CellKm);

	const bool bHolds = Uphill == 0 && Unterminated == 0 && Hash == Again && Mouths.Num() > 0;
	Body += FString::Printf(TEXT("\nVERDICT: %s\n"), bHolds ? TEXT("PASS") : TEXT("FAIL"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("hydrology.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("hydrology -> %s"), *Path);
	return bHolds;
}
