// The collision transect: fly low and fast in a straight line, and trace
// downward every single frame.
//
// **A separate mode rather than another step in the scripted flight.** Two
// hundred kilometres at 900 m/s is three and a half minutes, which would more
// than double a run that already takes three. Launch with `-transect` and this
// runs instead of the flight.
//
// The question it answers cannot be answered by looking. Streaming collision
// is cooked near the camera and ahead along the velocity vector, and the
// failure mode is that it does not arrive: a trace that hits nothing, on one
// frame, somewhere in the middle of a long fast run. A ship falls through the
// world for a moment and is back before anybody has finished being surprised.
// Counting the misses is the only honest way to know.

#include "LedgerTransect.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/// How high above the ground the ship is held, in centimetres.
	constexpr double TransectAltitude = 5000.0;

	/// Ground speed, cm/s. 900 m/s is the number in M02's gate.
	constexpr double TransectSpeed = 90000.0;

	/// How far, in centimetres.
	constexpr double TransectDistance = 20000000.0; // 200 km

	/// The trace looks this far down. Generous: it is asking whether *any*
	/// collision exists beneath, not measuring a height.
	constexpr double TraceDepth = 40000.0;
}

bool ULedgerTransect::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerTransect::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!FParse::Param(FCommandLine::Get(), TEXT("transect")))
	{
		return;
	}

	bRunning = true;
	Travelled = 0.0;
	Frames = 0;
	Misses = 0;
	LongestMiss = 0;
	CurrentMiss = 0;

	UE_LOG(LogLedger, Log, TEXT("transect: %.0f km at %.0f m/s, %.0f m up, tracing every frame"),
		TransectDistance / 100000.0, TransectSpeed / 100.0, TransectAltitude / 100.0);
}

void ULedgerTransect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;

	if (Planet == nullptr || Ship == nullptr)
	{
		return;
	}

	// The first frames are startup; the terrain has not streamed anything yet
	// and counting misses there would be measuring the loading screen.
	if (Warmup < 3.0)
	{
		Warmup += DeltaSeconds;
		Ship->SetFlightEnabled(false);
		Place(*Planet, *Ship, *Controller, 0.0);
		return;
	}

	Travelled += TransectSpeed * DeltaSeconds;
	Place(*Planet, *Ship, *Controller, Travelled);

	// The trace: straight down from the ship, asking only whether anything is
	// there. A miss means the patch below has not cooked, and a ship would have
	// nothing to land on or collide with at this moment.
	const FVector From = Ship->GetActorLocation();
	const FVector Down = -(From - Planet->GetActorLocation()).GetSafeNormal();

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LedgerTransect), false, Ship);
	const bool bHit = World->LineTraceSingleByChannel(
		Hit, From, From + Down * TraceDepth, ECC_WorldStatic, Params);

	++Frames;
	if (bHit)
	{
		CurrentMiss = 0;
	}
	else
	{
		++Misses;
		++CurrentMiss;
		LongestMiss = FMath::Max(LongestMiss, CurrentMiss);
	}

	if (Travelled >= TransectDistance)
	{
		Finish();
	}
}

void ULedgerTransect::Place(ALedgerPlanet& Planet, ALedgerShip& Ship,
	APlayerController& Controller, double Along) const
{
	// A great circle from the town site, so the ground under it is the same
	// ground the rest of the flight visits and the run is comparable.
	const ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>();
	const FVector3d Start = Builder != nullptr ? Builder->GetSiteDirection() : FVector3d::UnitZ();
	const FVector3d East = FVector3d::CrossProduct(Start, FVector3d::UpVector).GetSafeNormal();

	const double Angle = Along / Planet.Radius;
	const FVector3d Direction = (Start * FMath::Cos(Angle) + East * FMath::Sin(Angle)).GetSafeNormal();

	const double Ground = Planet.SurfaceRadiusAt(Direction);
	Ship.SetActorLocation(Planet.GetActorLocation() + FVector(Direction * (Ground + TransectAltitude)));

	// Facing along the track, so the collision cook's velocity lead points where
	// the ship is actually going. Getting this wrong would test the wrong thing:
	// a stationary-facing ship cooks a circle rather than a corridor.
	const FVector3d Ahead = (Start * -FMath::Sin(Angle) + East * FMath::Cos(Angle)).GetSafeNormal();
	const FRotator Attitude = FRotationMatrix::MakeFromXZ(FVector(Ahead), FVector(Direction)).Rotator();
	Ship.SetActorRotation(Attitude);
	Controller.SetControlRotation(Attitude);
}

void ULedgerTransect::Finish()
{
	bRunning = false;

	const double MissFraction = Frames > 0 ? 100.0 * Misses / static_cast<double>(Frames) : 0.0;
	const bool bPassed = Misses == 0;

	FString Body;
	Body += TEXT("Collision transect.\n\n");
	Body += FString::Printf(TEXT("  distance      %.0f km\n"), TransectDistance / 100000.0);
	Body += FString::Printf(TEXT("  speed         %.0f m/s\n"), TransectSpeed / 100.0);
	Body += FString::Printf(TEXT("  altitude      %.0f m\n"), TransectAltitude / 100.0);
	Body += FString::Printf(TEXT("  frames        %d\n"), Frames);
	Body += FString::Printf(TEXT("  traces missed %d  (%.3f%%)\n"), Misses, MissFraction);
	Body += FString::Printf(TEXT("  longest gap   %d consecutive frames\n\n"), LongestMiss);
	Body += FString::Printf(TEXT("VERDICT: %s\n"), bPassed
		? TEXT("PASS — collision present under the ship on every frame")
		: TEXT("FAIL — the ship had nothing beneath it at some point"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("transect.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);

	UE_LOG(LogLedger, Log, TEXT("transect -> %s"), *Path);
	UE_LOG(LogLedger, Log, TEXT("  %d frames, %d missed (%.3f%%), longest gap %d, %s"),
		Frames, Misses, MissFraction, LongestMiss, bPassed ? TEXT("PASS") : TEXT("FAIL"));

	if (GEngine != nullptr && GetWorld() != nullptr)
	{
		GEngine->Exec(GetWorld(), TEXT("Ledger.Terrain.Stats"));
	}
	FGenericPlatformMisc::RequestExit(false);
}

TStatId ULedgerTransect::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerTransect, STATGROUP_Tickables);
}
