#include "LedgerPadCheck.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerTerrainDelta.h"
#include "LedgerTerrainSample.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/// The pad. Forty metres of flat with twenty of blend, which is a landing
	/// pad rather than a crater.
	constexpr double PadRadiusMetres = 40.0;
	constexpr double PadFalloffMetres = 20.0;

	/// Long enough for the collision ring to fill, twice: once before the edit
	/// and once after it, because levelling drops every patch.
	constexpr double SettleSeconds = 12.0;

	/// How flat is flat. The mesh resolves 4.8 m at the finest LOD and the pad
	/// is a plane, so the only error is the interpolation of a plane, which is
	/// none -- this is a tolerance for the sampling, not for the levelling.
	constexpr double FlatToleranceMetres = 0.05;
}

bool ULedgerPadCheck::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerPadCheck::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("flattenpad"));

	// Start from a planet nobody has dug, so a re-run measures the same thing
	// the first run did rather than the sum of every run before it.
	if (bRunning)
	{
		IFileManager::Get().Delete(*FLedgerTerrainDelta::DefaultPath(), false, true, true);
	}
}

void ULedgerPadCheck::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr
		? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	// The scripted flight is running too and will keep moving the ship. Park it
	// over the site every frame; see LedgerTerrainQueryCheck for the run where
	// forgetting this traced into empty sky a thousand times.
	const FVector3d Site = Builder->GetSiteDirection().GetSafeNormal();
	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetActorLocation(FVector(FVector3d(Planet->GetActorLocation())
			+ Site * (Planet->SurfaceRadiusAt(Site) + 4000.0)));
	}

	Waited += DeltaSeconds;
	if (Waited < SettleSeconds)
	{
		return;
	}

	if (!bLevelled)
	{
		// The pad is put at the ground's own height at the centre, so it is a
		// levelling rather than a lift: the interesting question is whether the
		// slope around it goes away, not whether the API can move ground.
		FLedgerTerrainSample Before;
		if (!Planet->SampleTerrain(FVector3d(Planet->GetActorLocation())
			+ Site * Planet->SurfaceRadiusAt(Site), Before))
		{
			// Not settled after all.
			return;
		}

		Relief = Roughness(*Planet, Site);
		Target = Before.AltitudeMetres;
		Planet->LevelPad(Site, PadRadiusMetres, PadFalloffMetres, Target);
		bLevelled = true;
		Waited = 0.0;
		return;
	}

	bRunning = false;
	const bool bHolds = Report();
	UE_LOG(LogLedger, Log, TEXT("flatten pad: %s"),
		bHolds ? TEXT("VERDICT PASS") : TEXT("VERDICT FAIL"));
	FPlatformMisc::RequestExit(false);
}

double ULedgerPadCheck::Roughness(const ALedgerPlanet& Planet, const FVector3d& Site) const
{
	// Peak-to-trough over the pad, from the sampling API. Measured before the
	// edit so the report can say what was levelled rather than only that
	// something now is.
	double Lowest = TNumericLimits<double>::Max();
	double Highest = -TNumericLimits<double>::Max();
	Sweep(Planet, Site, [&Lowest, &Highest](const FLedgerTerrainSample& Sample)
	{
		Lowest = FMath::Min(Lowest, Sample.AltitudeMetres);
		Highest = FMath::Max(Highest, Sample.AltitudeMetres);
	});
	return Highest > Lowest ? Highest - Lowest : 0.0;
}

void ULedgerPadCheck::Sweep(const ALedgerPlanet& Planet, const FVector3d& Site,
	TFunctionRef<void(const FLedgerTerrainSample&)> Visit) const
{
	FVector3d East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Site);
	if (East.IsNearlyZero())
	{
		East = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), Site);
	}
	East.Normalize();
	const FVector3d North = FVector3d::CrossProduct(Site, East).GetSafeNormal();

	// A grid over the flat part only, inside the falloff.
	constexpr int32 Across = 21;
	const double Half = PadRadiusMetres * 0.8;
	for (int32 Y = 0; Y < Across; ++Y)
	{
		for (int32 X = 0; X < Across; ++X)
		{
			const double OffsetEast = (X / (Across - 1.0) * 2.0 - 1.0) * Half;
			const double OffsetNorth = (Y / (Across - 1.0) * 2.0 - 1.0) * Half;
			if (FVector2d(OffsetEast, OffsetNorth).Length() > Half)
			{
				continue;
			}

			const FVector3d Direction = (Site
				+ East * (OffsetEast * 100.0 / Planet.Radius)
				+ North * (OffsetNorth * 100.0 / Planet.Radius)).GetSafeNormal();

			FLedgerTerrainSample Sample;
			if (Planet.SampleTerrain(FVector3d(Planet.GetActorLocation())
				+ Direction * Planet.SurfaceRadiusAt(Direction), Sample))
			{
				Visit(Sample);
			}
		}
	}
}

bool ULedgerPadCheck::Report()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder->GetPlanet();
	const FVector3d Site = Builder->GetSiteDirection().GetSafeNormal();

	FString Body;
	Body += TEXT("Levelling a pad, and what it costs elsewhere (T062).\n\n");
	Body += FString::Printf(
		TEXT("pad: %.0f m flat with %.0f m of blend, brought to %.2f m\n"),
		PadRadiusMetres, PadFalloffMetres, Target);
	Body += FString::Printf(
		TEXT("the ground it replaced varied by %.2f m across the pad\n\n"), Relief);

	// ---- is it flat now? ---------------------------------------------------
	int32 Sampled = 0;
	double Worst = 0.0;
	Sweep(*Planet, Site, [this, &Sampled, &Worst](const FLedgerTerrainSample& Sample)
	{
		++Sampled;
		Worst = FMath::Max(Worst, FMath::Abs(Sample.AltitudeMetres - Target));
	});

	Body += FString::Printf(
		TEXT("after levelling: %d samples over the pad, worst %.4f m from the target\n"),
		Sampled, Worst);

	// ---- and after a reload? -----------------------------------------------
	//
	// The file, read back into a fresh delta, evaluated directly. Not the same
	// as restarting the process, and it is the half of the round trip that can
	// actually go wrong: the mesh is already known to follow the field.
	FString Saved;
	const bool bRead = FFileHelper::LoadFileToString(Saved, *FLedgerTerrainDelta::DefaultPath());
	TSharedRef<const FLedgerTerrainDelta> Reloaded =
		FLedgerTerrainDelta::FromJson(bRead ? Saved : FString());

	FLedgerTerrainParams Bare = Planet->TerrainParams();
	Bare.Delta.Reset();
	FLedgerTerrainParams Restored = Planet->TerrainParams();
	Restored.Delta = Reloaded;

	double WorstReloaded = 0.0;
	int32 Checked = 0;
	{
		FVector3d East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Site).GetSafeNormal();
		const FVector3d North = FVector3d::CrossProduct(Site, East).GetSafeNormal();
		for (int32 Step = -10; Step <= 10; ++Step)
		{
			const double Offset = Step / 10.0 * PadRadiusMetres * 0.8;
			const FVector3d Direction =
				(Site + East * (Offset * 100.0 / Planet->Radius)).GetSafeNormal();
			const double Altitude =
				LedgerTerrain::Elevation(Direction, Restored) / 100.0;
			WorstReloaded = FMath::Max(WorstReloaded, FMath::Abs(Altitude - Target));
			++Checked;
		}
	}

	Body += FString::Printf(
		TEXT("reloaded from %s: %d edits, %d samples along the pad, "
		     "worst %.4f m from the target\n\n"),
		bRead ? TEXT("disk") : TEXT("NOTHING"), Reloaded->Num(), Checked, WorstReloaded);

	// ---- what it costs where nothing was modified --------------------------
	//
	// The same sweep of the height field, far from the pad, with and without
	// the delta. Far, because near it the delta is doing real work and the
	// question is what it costs where it is not.
	const FVector3d Elsewhere = FVector3d(-Site.Y, Site.Z, -Site.X).GetSafeNormal();
	constexpr int32 Samples = 200000;

	double Sink = 0.0;
	const double BareStarted = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const FVector3d Point =
			(Elsewhere + FVector3d(Index * 1e-9, Index * 2e-9, 0.0)).GetSafeNormal();
		Sink += LedgerTerrain::Elevation(Point, Bare);
	}
	const double BareMs = (FPlatformTime::Seconds() - BareStarted) * 1000.0;

	const double DeltaStarted = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const FVector3d Point =
			(Elsewhere + FVector3d(Index * 1e-9, Index * 2e-9, 0.0)).GetSafeNormal();
		Sink += LedgerTerrain::Elevation(Point, Restored);
	}
	const double DeltaMs = (FPlatformTime::Seconds() - DeltaStarted) * 1000.0;

	Body += FString::Printf(
		TEXT("%d height samples away from the pad: %.1f ms without the delta, "
		     "%.1f ms with it (%+.1f%%)\n"),
		Samples, BareMs, DeltaMs,
		BareMs > 0.0 ? 100.0 * (DeltaMs - BareMs) / BareMs : 0.0);
	Body += FString::Printf(TEXT("(checksum %.0f, so neither loop was optimised away)\n"),
		FMath::Abs(Sink));

	const bool bFlat = Sampled > 100 && Worst < FlatToleranceMetres;
	const bool bPersisted = bRead && Reloaded->Num() == 1
		&& WorstReloaded < FlatToleranceMetres;
	const bool bCheap = BareMs > 0.0 && (DeltaMs - BareMs) / BareMs < 0.05;

	Body += FString::Printf(TEXT("\nflat: %s   survives a reload: %s   "
		"costs nothing elsewhere: %s\n"),
		bFlat ? TEXT("yes") : TEXT("NO"),
		bPersisted ? TEXT("yes") : TEXT("NO"),
		bCheap ? TEXT("yes") : TEXT("NO"));
	Body += FString::Printf(TEXT("\nVERDICT: %s\n"),
		(bFlat && bPersisted && bCheap) ? TEXT("PASS") : TEXT("FAIL"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("flatten-pad.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("flatten pad -> %s"), *Path);
	return bFlat && bPersisted && bCheap;
}

TStatId ULedgerPadCheck::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerPadCheck, STATGROUP_Tickables);
}
