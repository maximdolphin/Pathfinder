#include "LedgerDescent.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/// Where the descent starts and stops, and how fast. Twenty kilometres is
	/// well inside the band where the streamer has to refine every level on
	/// the way down; a hundred metres is low enough that the finest patches
	/// are the ones in view.
	constexpr double DescentStartMetres = 20000.0;
	constexpr double DescentStopMetres = 100.0;
	constexpr double DescentSpeed = 300.0;   // m/s, the acceptance's number

	/// Settled before the clock starts, so a hole counted on the way down is
	/// the descent's and not the start-up's.
	constexpr double DescentMinSettle = 20.0;
	constexpr double DescentMaxSettle = 180.0;
	constexpr double DescentHold = 5.0;
}

bool ULedgerDescent::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerDescent::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerDescent, STATGROUP_Tickables);
}

void ULedgerDescent::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("descent300"));
	if (!bRunning)
	{
		return;
	}
	if (const ULedgerWorldBuilder* Builder = InWorld.GetSubsystem<ULedgerWorldBuilder>())
	{
		Anchor = Builder->GetSiteDirection().GetSafeNormal();
	}
	AltitudeMetres = DescentStartMetres;
}

void ULedgerDescent::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	const ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}
	const FLedgerTerrainStats& Stats = Planet->GetStats();

	if (!bDescending)
	{
		// Parked at the top until the streamer has nothing left to do.
		Waited += DeltaSeconds;
		const bool bBusy = Stats.JobsInFlight > 0 || Stats.PendingBuilds > 0;
		if (Waited >= DescentMaxSettle || (Waited >= DescentMinSettle && !bBusy))
		{
			bDescending = true;
			UE_LOG(LogLedger, Log,
				TEXT("descent: settled after %.0f s at %.0f m (%d unfilled, %d jobs, %d builds); going down at %.0f m/s"),
				Waited, AltitudeMetres, Stats.UnfilledNodes, Stats.JobsInFlight,
				Stats.PendingBuilds, DescentSpeed);
		}
	}
	else if (AltitudeMetres > DescentStopMetres)
	{
		// **Every frame, whatever the frame time.** A slow frame moves the
		// ship further, which is what a slow frame does to a real descent, and
		// the count after it is the one that matters.
		AltitudeMetres = FMath::Max(DescentStopMetres,
			AltitudeMetres - DescentSpeed * DeltaSeconds);
		DescentSeconds += DeltaSeconds;
		++Frames;
		if (Stats.UnfilledNodes > 0)
		{
			++FramesWithHoles;
		}
		if (Stats.UnfilledNodes > WorstHoles)
		{
			WorstHoles = Stats.UnfilledNodes;
			WorstHolesAtMetres = AltitudeMetres;
		}
	}
	else
	{
		Held += DeltaSeconds;
		if (Held >= DescentHold)
		{
			bRunning = false;
			Report();
			FPlatformMisc::RequestExit(false);
			return;
		}
	}

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Anchor);
	const FVector Eye = FVector(Centre + Anchor * (Ground + AltitudeMetres * 100.0));

	// The ship carries the velocity, because the streamer leads along it: a
	// descent flown as a string of teleports would test nothing but the pool.
	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye);
		Ship->SetVelocity(bDescending && AltitudeMetres > DescentStopMetres
			? FVector(-Anchor * DescentSpeed * 100.0) : FVector::ZeroVector);
	}

	// Looking down and ahead, the way a pilot coming in does.
	FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Anchor);
	if (East.IsNearlyZero())
	{
		East = FVector3d::CrossProduct(FVector3d::UnitX(), Anchor);
	}
	const FVector3d Toward = (East.GetSafeNormal() - Anchor * 0.6).GetSafeNormal();
	const FRotator Look = FRotationMatrix::MakeFromXZ(FVector(Toward), FVector(Anchor)).Rotator();
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, Look, SpawnParams);
		if (Camera != nullptr)
		{
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

void ULedgerDescent::Report()
{
	const bool bHolds = Frames > 0 && WorstHoles == 0;
	FString Body;
	Body += TEXT("A 300 m/s descent, counted for holes every frame (T027).\n\n");
	Body += FString::Printf(TEXT("from %.0f m to %.0f m at %.0f m/s: %.1f s, %d frames\n"),
		DescentStartMetres, DescentStopMetres, DescentSpeed, DescentSeconds, Frames);
	Body += FString::Printf(TEXT("frames with an unfilled node   %d\n"), FramesWithHoles);
	Body += FString::Printf(TEXT("most unfilled in one frame     %d%s\n"), WorstHoles,
		WorstHoles > 0 ? *FString::Printf(TEXT(", at %.0f m"), WorstHolesAtMetres) : TEXT(""));
	Body += FString::Printf(TEXT("\nVERDICT: %s\n"), bHolds ? TEXT("PASS") : TEXT("FAIL"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("descent-300.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("descent: %d frames, %d with holes, worst %d -- VERDICT %s -> %s"),
		Frames, FramesWithHoles, WorstHoles, bHolds ? TEXT("PASS") : TEXT("FAIL"), *Path);
}
