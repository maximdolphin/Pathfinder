#include "LedgerPlaytest.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerSettlement.h"
#include "LedgerShip.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/// One leg of the flight: how long, the six axes, and whether to photograph it.
	struct FPlaytestLeg
	{
		const TCHAR* Name;
		double Seconds;
		float Throttle, Strafe, Lift, Pitch, Yaw, Roll;
		bool bCapture;
	};

	// Off the pad, out over the town and back, down to a hover: every axis, in
	// about the order a player tries them -- and, like a player, putting back
	// what it took. Pitch and roll go one way and then the other by the same
	// amount; letting go is the brake, and S backs up. The first version asked
	// for 137 degrees of pitch in one leg, looped the ship, and then scored the
	// dive it left behind.
	const FPlaytestLeg Legs[] = {
		{ TEXT("hover"),       6.0,  0.0f, 0.0f,  0.0f,  0.0f, 0.0f,  0.0f, true },
		{ TEXT("climb"),      25.0,  0.0f, 0.0f,  1.0f,  0.0f, 0.0f,  0.0f, false },
		{ TEXT("forward"),    10.0,  0.5f, 0.0f,  0.0f,  0.0f, 0.0f,  0.0f, true },
		{ TEXT("turn"),        8.0,  0.5f, 0.0f,  0.0f,  0.0f, 0.5f,  0.0f, false },
		{ TEXT("cruise"),     10.0,  1.0f, 0.0f,  0.0f,  0.0f, 0.0f,  0.0f, true },
		{ TEXT("roll-right"),  3.0,  0.3f, 0.0f,  0.0f,  0.0f, 0.0f,  0.5f, false },
		{ TEXT("roll-left"),   3.0,  0.3f, 0.0f,  0.0f,  0.0f, 0.0f, -0.5f, false },
		{ TEXT("strafe"),      5.0,  0.0f, 0.6f,  0.0f,  0.0f, 0.0f,  0.0f, false },
		{ TEXT("pitch-down"),  3.0,  0.3f, 0.0f,  0.0f, -0.3f, 0.0f,  0.0f, true },
		{ TEXT("pitch-up"),    3.0,  0.3f, 0.0f,  0.0f,  0.3f, 0.0f,  0.0f, false },
		{ TEXT("let-go"),     10.0,  0.0f, 0.0f,  0.0f,  0.0f, 0.0f,  0.0f, true },
		{ TEXT("reverse"),     4.0, -1.0f, 0.0f,  0.0f,  0.0f, 0.0f,  0.0f, true },
		{ TEXT("descend"),     8.0,  0.0f, 0.0f, -0.5f,  0.0f, 0.0f,  0.0f, true },
		{ TEXT("hover-end"),   8.0,  0.0f, 0.0f,  0.0f,  0.0f, 0.0f,  0.0f, true },
	};

	/// Long enough for the world to build and the ground under the town to arrive.
	constexpr double SettleSeconds = 8.0;
	constexpr double SampleSeconds = 1.0;

	// What a player would call the ship falling apart.
	constexpr double MaxSpinDegPerSecond = 180.0;
	constexpr double MaxLowSpeed = 400.0;
	constexpr double LowMetres = 2000.0;
}

bool ULedgerPlaytest::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerPlaytest::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerPlaytest, STATGROUP_Tickables);
}

void ULedgerPlaytest::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("playtest"));
	Lines.Add(TEXT("Playtest: the ship flown through the six axes a player has (M5P)."));
	Lines.Add(TEXT(""));
}

void ULedgerPlaytest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	APlayerController* Controller = World->GetFirstPlayerController();
	ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;
	ALedgerSettlement* Town = Builder != nullptr ? Builder->GetSettlement() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Ship == nullptr || Town == nullptr || Planet == nullptr)
	{
		return;
	}
	Clock += DeltaSeconds;

	if (!bPlaced)
	{
		if (Clock < SettleSeconds)
		{
			return;
		}
		// Where -play puts it: forty metres over the pad, level, coupled, the
		// sun behind, so this flies the start a player gets.
		const FVector3d Up = Builder->GetSiteDirection().GetSafeNormal();
		const FVector3d Sun = Builder->GetSunFacing();
		FVector3d SunHorizontal = Sun - Up * FVector3d::DotProduct(Sun, Up);
		if (!SunHorizontal.Normalize())
		{
			SunHorizontal = FVector3d::CrossProduct(Up, FVector3d::UnitX()).GetSafeNormal();
		}
		Ship->SetFlightEnabled(true);
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetActorLocation(Town->GetPadLocation() + FVector(Up) * 4000.0);
		Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(-SunHorizontal), FVector(Up)).Rotator());
		Ship->SetFlightMode(ELedgerFlightMode::Coupled);
		LastAttitude = Ship->GetActorQuat();
		bSkipSpin = true;
		bPlaced = true;
		Leg = 0;
		LegClock = 0.0;
		Lines.Add(TEXT("  time  leg          height m  speed m/s  ahead  spin deg/s  hull  frame ms   fog   ice  nose  nozzles"));
		UE_LOG(LogLedger, Log, TEXT("playtest: the ship is over the pad, flying %d legs"), static_cast<int32>(UE_ARRAY_COUNT(Legs)));
		return;
	}

	// ---- what a player would notice -------------------------------------------
	const double FrameMs = DeltaSeconds * 1000.0;
	++Frames;
	FramesOver += FrameMs > 16.7 ? 1 : 0;
	FramesOver33 += FrameMs > 33.0 ? 1 : 0;
	Hitches += FrameMs > 250.0 && Clock > SettleSeconds + 60.0 ? 1 : 0;
	WorstFrameMs = FMath::Max(WorstFrameMs, FrameMs);

	const FQuat Attitude = Ship->GetActorQuat();
	const double Spin = bSkipSpin ? 0.0 : FMath::RadiansToDegrees(LastAttitude.AngularDistance(Attitude)) / DeltaSeconds;
	bSkipSpin = false;
	LastAttitude = Attitude;
	const FVector Where = Ship->GetActorLocation();
	const double Speed = Ship->GetVelocity().Size() / 100.0;
	const double Height = Ship->AltitudeMetres();
	const double Hull = Ship->HullIntegrity();
	WorstSpin = FMath::Max(WorstSpin, Spin);
	WorstSpeed = FMath::Max(WorstSpeed, Speed);
	LowestHeight = FMath::Min(LowestHeight, Height);
	LowestHull = FMath::Min(LowestHull, Hull);

	const FPlaytestLeg& Now = Legs[Leg];
	if (Where.ContainsNaN() || !FMath::IsFinite(Speed) || !FMath::IsFinite(Height))
	{
		Divergence = FString::Printf(TEXT("NaN in the ship's state during %s"), Now.Name);
	}
	else if (Spin > MaxSpinDegPerSecond)
	{
		Divergence = FString::Printf(TEXT("spin %.0f deg/s during %s at %.0f m"), Spin, Now.Name, Height);
	}
	else if (Speed > MaxLowSpeed && Height < LowMetres)
	{
		Divergence = FString::Printf(TEXT("%.0f m/s at %.0f m during %s"), Speed, Height, Now.Name);
	}
	else if ((FVector3d(Where) - FVector3d(Planet->GetActorLocation())).Length() > Planet->Radius * 2.0)
	{
		Divergence = FString::Printf(TEXT("left the planet during %s"), Now.Name);
	}
	// Meeting the ground faster than a hard landing is a crash, whatever the hull
	// says: the third playtest dived into it at 78 m/s and was scored a pass.
	else if (Height < 3.0 && LastSpeed > 15.0)
	{
		Divergence = FString::Printf(TEXT("hit the ground at %.0f m/s during %s"), LastSpeed, Now.Name);
	}
	LastSpeed = Speed;

	SinceSample += DeltaSeconds;
	if (SinceSample >= SampleSeconds || !Divergence.IsEmpty())
	{
		SinceSample = 0.0;
		// The nose above the local horizon, and whether the nozzles made what the
		// assist asked -- the two things that say why a hands-off ship drifts.
		const FVector3d LocalUp = (FVector3d(Where) - FVector3d(Planet->GetActorLocation())).GetSafeNormal();
		const double NoseUp = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(
			FVector3d::DotProduct(FVector3d(Ship->GetActorForwardVector()), LocalUp), -1.0, 1.0)));
		// And how much of the speed is along the nose: negative is sliding backwards.
		const double Ahead = FVector::DotProduct(Ship->GetVelocity(), Ship->GetActorForwardVector()) / 100.0;
		Lines.Add(FString::Printf(TEXT("  %5.1f %-12s %9.1f %10.1f %8.1f %11.1f %5.2f %9.1f %5.2f %5.2f %6.1f %s"),
			Clock - SettleSeconds, Now.Name, Height, Speed, Ahead, Spin, Hull, FrameMs,
			Ship->CanopyState().Fog, Ship->CanopyState().Ice, NoseUp,
			Ship->Allocation().bMet ? TEXT("met") : TEXT("SHORT")));
	}
	if (!Divergence.IsEmpty())
	{
		Finish(Divergence);
		return;
	}

	// ---- the stick ------------------------------------------------------------
	// Through the ship's auto inputs, which add to the stick in the same place
	// the keys arrive. Not the stick itself: the key bindings write every axis
	// every frame, zero when nothing is pressed, and the first playtest's
	// commands were overwritten before the ship read them -- it sat on the pad.
	// Except that, like a player, it pulls up -- lets go of the throttle and
	// lifts -- when the ground is closing and eight seconds away. The seventh
	// playtest flew level at 300 m/s into rising ground a player would have seen.
	const double Closing = (LastHeight - Height) / DeltaSeconds;
	LastHeight = Height;
	const bool bPullUp = Closing > 5.0 && Height < Closing * 8.0;
	PulledSeconds += bPullUp ? DeltaSeconds : 0.0;
	Ship->SetAutoThrottle(bPullUp ? 0.0f : Now.Throttle);
	Ship->SetAutoStrafe(Now.Strafe);
	Ship->SetAutoLift(bPullUp ? 1.0f : Now.Lift);
	Ship->SetAutoTurn(FVector3f(Now.Pitch, Now.Yaw, Now.Roll));
	LegClock += DeltaSeconds;
	if (Now.bCapture && !bCaptured && LegClock > Now.Seconds * 0.5)
	{
		bCaptured = true;
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			FString::Printf(TEXT("playtest-%02d-%s.png"), Leg + 1, Now.Name))), false, false);
	}
	if (LegClock >= Now.Seconds)
	{
		++Leg;
		LegClock = 0.0;
		bCaptured = false;
		if (Leg >= static_cast<int32>(UE_ARRAY_COUNT(Legs)))
		{
			Finish(TEXT(""));
		}
	}
}

void ULedgerPlaytest::Finish(const FString& Why)
{
	bRunning = false;
	UWorld* World = GetWorld();
	if (APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr)
	{
		if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
		{
			Ship->SetStick(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	const bool bFlew = Why.IsEmpty();
	const bool bFrames = Frames > 0 && FramesOver <= Frames / 1000 && FramesOver33 == 0;
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("legs flown        %d of %d"), FMath::Max(Leg, 0), static_cast<int32>(UE_ARRAY_COUNT(Legs))));
	Lines.Add(FString::Printf(TEXT("worst spin        %.1f deg/s (limit %.0f)"), WorstSpin, MaxSpinDegPerSecond));
	Lines.Add(FString::Printf(TEXT("worst speed       %.1f m/s"), WorstSpeed));
	Lines.Add(FString::Printf(TEXT("lowest height     %.1f m"), LowestHeight));
	Lines.Add(FString::Printf(TEXT("lowest hull       %.2f"), LowestHull));
	Lines.Add(FString::Printf(TEXT("pulled up         %.1f s"), PulledSeconds));
	Lines.Add(FString::Printf(TEXT("frames            %d, %d over 16.7 ms, %d over 33 ms, worst %.1f ms, %d hitches over 250 ms after the first minute"),
		Frames, FramesOver, FramesOver33, WorstFrameMs, Hitches));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("stays in one piece: %s"), bFlew ? TEXT("yes") : *FString::Printf(TEXT("NO -- %s"), *Why)));
	Lines.Add(FString::Printf(TEXT("frames in budget:   %s"), bFrames ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bFlew && Hitches == 0 ? TEXT("PASS") : TEXT("FAIL")));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("playtest: %s"), *Line);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("playtest.txt"))));
	FPlatformMisc::RequestExit(false);
}
