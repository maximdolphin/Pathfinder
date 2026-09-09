// Taking the pictures, and waiting until they are worth taking.
//
// Split out of LedgerFlightHarness.cpp when that file crossed 500 lines. The
// standard asks what two things a file that long is doing, and the answer here
// was legible: one half moves a camera along a script, the other decides when
// the world has settled enough to photograph and then photographs it. The
// second half is the one with the history -- captures used to fire on the timer
// that scheduled them, which made every image a photograph of whatever had
// finished streaming by that instant.

#include "LedgerFlightHarness.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "LedgerLog.h"
#include "LedgerPerf.h"
#include "LedgerPlanet.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

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
	// The screenshot frame and the one after it are not frames the game had to
	// render; see ULedgerPerfSubsystem::SkipFrames.
	if (UWorld* Recording = GetWorld())
	{
		if (ULedgerPerfSubsystem* Perf = Recording->GetSubsystem<ULedgerPerfSubsystem>())
		{
			Perf->SkipFrames(2);
		}
	}

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
