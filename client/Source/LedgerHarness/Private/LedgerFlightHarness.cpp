#include "LedgerFlightHarness.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerLog.h"
#include "LedgerPerf.h"
#include "LedgerPlanet.h"
#include "LedgerSettlement.h"
#include "LedgerShip.h"
#include "LedgerTerrainMath.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

bool ULedgerFlightHarness::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

ULedgerWorldBuilder* ULedgerFlightHarness::Builder() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
}

ALedgerPlanet* ULedgerFlightHarness::Planet() const
{
	ULedgerWorldBuilder* World = Builder();
	return World != nullptr ? World->GetPlanet() : nullptr;
}

ALedgerSettlement* ULedgerFlightHarness::Town() const
{
	ULedgerWorldBuilder* World = Builder();
	return World != nullptr ? World->GetSettlement() : nullptr;
}

FVector3d ULedgerFlightHarness::Site() const
{
	ULedgerWorldBuilder* World = Builder();
	return World != nullptr ? World->GetSiteDirection() : FVector3d::UnitZ();
}

FVector3d ULedgerFlightHarness::Sun() const
{
	ULedgerWorldBuilder* World = Builder();
	return World != nullptr ? World->GetSunFacing() : FVector3d::UnitZ();
}

ALedgerShip* ULedgerFlightHarness::GetShip() const
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	return Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;
}

void ULedgerFlightHarness::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Whichever fixture is running owns the ship. Two of them placing the same
	// pawn is two fixtures measuring neither.
	for (const TCHAR* Fixture : { TEXT("transect"), TEXT("surfacestudy"), TEXT("turntable") })
	{
		if (FParse::Param(FCommandLine::Get(), Fixture))
		{
			UE_LOG(LogLedger, Log, TEXT("flight harness standing down: -%s"), Fixture);
			return;
		}
	}

	// A scripted reentry: settle in orbit, fly down to the town, and capture
	// along the way. One continuous world, one continuous camera, no seam.
	//
	// **Each step must finish before the next one takes the ship.** A step that
	// frames a shot and schedules the capture a few seconds later is still
	// running during those seconds, and the ridge sweep was once inserted at
	// exactly the moment the town capture was due — so both files came back
	// showing the sweep, and the town went unphotographed for two milestones
	// without anyone noticing, because the image that was checked was the one
	// that was right.
	FTimerManager& Timers = InWorld.GetTimerManager();
	Timers.SetTimer(OrbitCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::CaptureOrbit), 14.0f, false);
	Timers.SetTimer(DescendTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::BeginDescent), 18.0f, false);
	Timers.SetTimer(EntryCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::CaptureEntry), 42.0f, false);
	Timers.SetTimer(SurfaceCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::CaptureSurface), 68.0f, false);
	Timers.SetTimer(TownCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::FrameTown), 78.0f, false);
	Timers.SetTimer(SweepTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::BeginRidgeSweep), 88.0f, false);
	Timers.SetTimer(CoastCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::FrameCoast), 124.0f, false);
	Timers.SetTimer(UnderwaterTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::FrameUnderwater), 132.0f, false);
	Timers.SetTimer(AscentTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::BeginAscent), 140.0f, false);
	Timers.SetTimer(SpaceCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::FrameSpace), 154.0f, false);
	Timers.SetTimer(PerformanceTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::WritePerformanceReport), 160.0f, false);

	UE_LOG(LogLedger, Log, TEXT("flight harness armed: 11 steps over 160 s"));
}

// ---------------------------------------------------------------- captures

void ULedgerFlightHarness::Capture(const TCHAR* Name)
{
	// Queued, not taken. The shot fires once the terrain has finished streaming.
	//
	// Captures used to fire on the timer that scheduled them, which made every
	// image a photograph of whatever had happened to load by that instant. Two
	// builds running at different frame rates reach the same game time having
	// streamed different amounts of terrain, so their captures differ for a
	// reason that has nothing to do with what is being compared -- and the
	// packaged build failed four of eight comparisons against the editor on
	// exactly the four phases where the ship moves fastest.
	//
	// Waiting for a settled world makes the image a function of where the
	// camera is rather than of how fast the machine is.
	PendingCapture = Name;
	SettleWaited = 0.0;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SettleTimer,
			FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::TakeWhenSettled),
			SettlePollSeconds, true);
	}
}

void ULedgerFlightHarness::TakeWhenSettled()
{
	if (PendingCapture == nullptr)
	{
		return;
	}

	SettleWaited += SettlePollSeconds;

	const ALedgerPlanet* Body = Planet();
	const bool bSettled = Body != nullptr
		&& Body->GetStats().PendingBuilds == 0
		&& Body->GetStats().JobsInFlight == 0;

	// Bounded. A world that never settles must still produce an image, or a
	// streaming regression turns into a missing file and reads as a harness
	// fault rather than the thing it is.
	if (!bSettled && SettleWaited < SettleLimitSeconds)
	{
		return;
	}

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			FString(PendingCapture)));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogLedger, Log, TEXT("capture -> %s (settled after %.1f s%s)"),
		*Path, SettleWaited, bSettled ? TEXT("") : TEXT(", TIMED OUT"));

	PendingCapture = nullptr;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SettleTimer);
	}
}

void ULedgerFlightHarness::CaptureOrbit()
{
	MarkPhase(TEXT("orbit"));
	if (UWorld* World = GetWorld())
	{
		if (GEngine != nullptr)
		{
			GEngine->Exec(World, TEXT("Ledger.Terrain.Stats"));
		}
	}
	Capture(TEXT("terrain-orbit.png"));
}

void ULedgerFlightHarness::CaptureEntry()
{
	MarkPhase(TEXT("atmospheric entry"));
	Capture(TEXT("terrain-entry.png"));
}

void ULedgerFlightHarness::CaptureSurface()
{
	MarkPhase(TEXT("surface"));

	// A per-pass GPU breakdown at the worst point in the flight. The surface is
	// where frame time is a hundred milliseconds and the game thread is twenty,
	// so the answer is a render pass and guessing which one is not a method.
	if (UWorld* World = GetWorld())
	{
		if (GEngine != nullptr)
		{
			GEngine->Exec(World, TEXT("ProfileGPU"));
		}
	}
	if (UWorld* World = GetWorld())
	{
		if (GEngine != nullptr)
		{
			GEngine->Exec(World, TEXT("Ledger.Terrain.Stats"));
		}
	}
	Capture(TEXT("terrain-surface.png"));
}

void ULedgerFlightHarness::CaptureTown()
{
	Capture(TEXT("terrain-town.png"));
}

// ---------------------------------------------------------------- descent

void ULedgerFlightHarness::BeginDescent()
{
	MarkPhase(TEXT("descent"));
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	if (World == nullptr || Ship == nullptr || Planet() == nullptr)
	{
		return;
	}

	// The ship stops integrating while the script owns its transform. Two
	// systems writing the same position produces a fight nobody wins.
	Ship->SetFlightEnabled(false);
	Ship->SetVelocity(FVector::ZeroVector);

	const FVector3d Current = FVector3d(Ship->GetActorLocation() - Planet()->GetActorLocation());
	DescentStartAltitude = Current.Length();
	// Just above the pad, so the handover leaves the player where the town is.
	DescentEndAltitude = Planet()->SurfaceRadiusAt(Site()) + 26000.0;
	DescentElapsed = 0.0;

	World->GetTimerManager().SetTimer(
		DescentStepTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::StepDescent),
		1.0f / 60.0f,
		true);

	UE_LOG(LogLedger, Log, TEXT("reentry: %.0f km -> %.2f km over %.0f s"),
		DescentStartAltitude / 100000.0, DescentEndAltitude / 100000.0, DescentDuration);
}

void ULedgerFlightHarness::StepDescent()
{
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet() == nullptr)
	{
		return;
	}

	DescentElapsed += 1.0 / 60.0;
	const double RawAlpha = FMath::Clamp(DescentElapsed / DescentDuration, 0.0, 1.0);
	const double Alpha = RawAlpha * RawAlpha * (3.0 - 2.0 * RawAlpha);

	// Interpolate the *logarithm* of the altitude, not the altitude. Linear
	// interpolation from 6,000 km down to a few hundred metres spends almost the
	// whole descent in empty space and then crosses the entire atmosphere in the
	// last half second. Log space gives a constant relative rate — the altitude
	// halves every so many seconds — which is what an approach looks like and
	// what gives the LOD time to refine.
	const double Altitude = FMath::Exp(FMath::Lerp(
		FMath::Loge(DescentStartAltitude), FMath::Loge(DescentEndAltitude), Alpha));

	// Slew from wherever the ship started to directly above the town, so the
	// approach converges on the site rather than dropping onto it from orbit.
	const FVector3d StartDirection = Sun();
	const FVector3d Direction =
		FMath::Lerp(StartDirection, Site(), FMath::Min(Alpha * 1.5, 1.0)).GetSafeNormal();

	const FVector Location = Planet()->GetActorLocation() + FVector(Direction * Altitude);
	Ship->SetActorLocation(Location);

	// Pitch from straight down at the start to a shallow approach at the end, so
	// the horizon rises into frame the way it does on a real descent.
	const FVector Up(Direction);
	FVector Forward = FVector::CrossProduct(Up, FVector(0.0, 1.0, 0.0)).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::CrossProduct(Up, FVector(1.0, 0.0, 0.0)).GetSafeNormal();
	}
	const FVector Look = FMath::Lerp(FVector(-Up), Forward - Up * 0.30, Alpha).GetSafeNormal();

	// `MakeFromXZ`, not `Look.Rotation()`. A rotation built from a direction
	// alone has no opinion about roll and resolves it against *world* Z — which
	// on a sphere is only vertical at one point, so the horizon came out tilted
	// everywhere else. Passing the local up as the Z reference makes it level.
	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Look, Up).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	if (RawAlpha >= 1.0)
	{
		World->GetTimerManager().ClearTimer(DescentStepTimer);

		// Hand the ship back. From here the player flies it, with gravity.
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetFlightEnabled(true);

		UE_LOG(LogLedger, Log, TEXT("reentry complete — control handed over at %.0f m"),
			Ship->AltitudeMetres());
	}
}

void ULedgerFlightHarness::FrameTown()
{
	MarkPhase(TEXT("town"));
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet() == nullptr || Town() == nullptr)
	{
		return;
	}

	const FVector Pad = Town()->GetPadLocation();
	const FVector3d Up = Site();

	// Put the sun behind the camera. The sun is nearly overhead at this site, so
	// what matters is its *horizontal* component — the direction the shadows
	// fall. Standing on that side means looking at lit faces instead of
	// silhouettes, which is what the first town capture came back as.
	FVector3d SunHorizontal = Sun() - Up * FVector3d::DotProduct(Sun(), Up);
	if (SunHorizontal.IsNearlyZero())
	{
		SunHorizontal = FVector3d::CrossProduct(Up, FVector3d::UpVector).GetSafeNormal();
	}
	SunHorizontal.Normalize();

	// Back off along the sun direction and climb, so the town is below and lit.
	const FVector Vantage = Pad + FVector(SunHorizontal) * 26000.0 + FVector(Up) * 9000.0;
	const FVector Look = (Pad - Vantage).GetSafeNormal();

	Ship->SetFlightEnabled(false);
	Ship->SetVelocity(FVector::ZeroVector);
	Ship->SetActorLocation(Vantage);

	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Look, FVector(Up)).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	UE_LOG(LogLedger, Log, TEXT("town framed from %.0f m up, sun behind camera"),
		Ship->AltitudeMetres());

	// Capture a beat later, once the sky light and exposure have settled.
	FTimerHandle Shot;
	World->GetTimerManager().SetTimer(
		Shot,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::CaptureTown),
		4.0f,
		false);
}

void ULedgerFlightHarness::BeginAscent()
{
	MarkPhase(TEXT("ascent"));
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet() == nullptr)
	{
		return;
	}

	// Measure the climb on its own, three seconds in. Everything before it in
	// this sequence teleports the camera, and the ascent itself starts by
	// swinging the boom fifty metres and pitching the nose up — neither of
	// which is a streaming failure, and both of which would otherwise be
	// counted as the climb's worst moment.
	if (UWorld* World = GetWorld())
	{
		FTimerHandle Settle;
		World->GetTimerManager().SetTimer(Settle, FTimerDelegate::CreateLambda([this]
		{
			if (GEngine != nullptr)
			{
				GEngine->Exec(GetWorld(), TEXT("Ledger.Terrain.ResetPeaks"));
			}
		}), 3.0f, false);
	}

	// Back onto the chase boom, which the underwater shot pulled in to the nose.
	Ship->SetCameraBoom(5200.0f, 1500.0f);

	// Nose up, throttle open, and hand it to the flight model. Nothing after
	// this line places the ship — gravity pulls, thrust pushes, drag bleeds, and
	// whether it reaches orbit is a question about the numbers rather than about
	// the animation.
	const FVector3d Radial = FVector3d(Ship->GetActorLocation() - Planet()->GetActorLocation());
	const FVector Up(Radial.GetSafeNormal());
	const FVector Lean = (Up * 0.94f + FVector(Sun()) * 0.34f).GetSafeNormal();

	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Lean, Up).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	Ship->SetFlightEnabled(true);
	Ship->SetAutoThrottle(1.0f);

	UE_LOG(LogLedger, Log, TEXT("ascent: throttle open at %.0f m"), Ship->AltitudeMetres());
}

void ULedgerFlightHarness::FrameSpace()
{
	MarkPhase(TEXT("space"));
	if (UWorld* Stats = GetWorld())
	{
		if (GEngine != nullptr)
		{
			// The climb retraces the descent, so the terrain stats logged here
			// are the cache's report card: every patch on the way up was
			// generated on the way down.
			GEngine->Exec(Stats, TEXT("Ledger.Terrain.Stats"));
		}
	}

	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet() == nullptr)
	{
		return;
	}

	// Cut the throttle and turn to look back down. The ship keeps its velocity —
	// it is coasting, not parked — so this is a manoeuvre rather than a stop.
	Ship->SetAutoThrottle(0.0f);

	const FVector Down = (Planet()->GetActorLocation() - Ship->GetActorLocation()).GetSafeNormal();
	const FVector Up = -Down;
	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Down, Up).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	UE_LOG(LogLedger, Log, TEXT("looking back from %.0f km, %.0f m/s"),
		Ship->AltitudeMetres() / 1000.0, Ship->GetVelocity().Size() / 100.0);

	FTimerHandle Shot;
	World->GetTimerManager().SetTimer(
		Shot,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::Capture, TEXT("terrain-space.png")),
		3.0f,
		false);
}

void ULedgerFlightHarness::MarkPhase(const TCHAR* Name)
{
	if (UWorld* World = GetWorld())
	{
		if (ULedgerPerfSubsystem* Perf = World->GetSubsystem<ULedgerPerfSubsystem>())
		{
			Perf->BeginPhase(FString(Name));
		}
	}
}

void ULedgerFlightHarness::WritePerformanceReport()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	if (ULedgerPerfSubsystem* Perf = World->GetSubsystem<ULedgerPerfSubsystem>())
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("performance.txt")));
		Perf->WriteReport(Path);
	}
}
