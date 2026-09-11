#include "LedgerStallTrial.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerShipSystemsComponent.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr double StallSettleSeconds = 15.0;
	constexpr double StallHeightMetres = 5000.0;
	constexpr double StallSpeedMetres = 130.0;
	/// Turning faster than this, stalled, is a departure.
	constexpr double StallDepartRadians = 0.52;
	/// Seconds of the spin before the recovery, and the most allowed for it.
	constexpr double StallSpinSeconds = 3.0;
	constexpr double StallRecoverySeconds = 40.0;
}

bool ULedgerStallTrial::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerStallTrial::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerStallTrial, STATGROUP_Tickables);
}

void ULedgerStallTrial::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("stalltrial"));
	Lines.Add(TEXT("A deliberate stall, the spin, and the recovery (T136)."));
	Lines.Add(TEXT(""));
}

void ULedgerStallTrial::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;
	ULedgerShipSystemsComponent* Systems = Ship != nullptr ? Ship->GetSystems() : nullptr;
	if (Planet == nullptr || Systems == nullptr || !Systems->IsConfigured() || !Ship->Definition().Aero.bWinged)
	{
		return;
	}
	const FLedgerShipDefinition& Definition = Systems->GetDefinition();
	Clock += DeltaSeconds;
	PhaseClock += DeltaSeconds;

	const FLedgerAeroState& Air = Ship->Aerodynamic();
	const FVector3d Omega = Ship->AngularVelocity();
	const double Rate = Omega.Length();
	const double YawRate = Omega.Z;
	// Roll and yaw together: a departure is the ship turning away, not the
	// pitch-up that took it past the stall.
	const double Lateral = FMath::Sqrt(Omega.X * Omega.X + Omega.Z * Omega.Z);
	const double Sink = -FVector3d::DotProduct(FVector3d(Ship->GetVelocity()), Up) / 100.0;
	const bool bStalled = Air.bStalled;

	if (Stage == 0)
	{
		// Held over the site, level, nose east; the thrusters off, so the
		// wings and the surfaces are all there is.
		Up = Builder->GetSiteDirection().GetSafeNormal();
		const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up).GetSafeNormal();
		const FVector3d Centre = FVector3d(Planet->GetActorLocation());
		Ship->SetFlightEnabled(false);
		Ship->SetFlightMode(ELedgerFlightMode::AssistOff);
		Ship->SetAutoThrottle(0.0f);
		Ship->SetAutoLift(0.0f);
		Ship->SetAutoTurn(FVector3f::ZeroVector);
		Ship->SetActorLocation(FVector(Centre + Up * (Planet->SurfaceRadiusAt(Up) + StallHeightMetres * 100.0)));
		Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(East), FVector(Up)).Rotator());
		Ship->SetVelocity(FVector(East * StallSpeedMetres * 100.0));
		const int32 Rcs = Definition.FindComponent(TEXT("rcs"));
		if (Rcs != INDEX_NONE)
		{
			Systems->EditState().Components[Rcs].bOn = false;
		}
		if (Clock < StallSettleSeconds)
		{
			return;
		}
		StartHeight = Ship->AltitudeMetres();
		Ship->SetFlightEnabled(true);
		Lines.Add(FString::Printf(TEXT("let go at %.0f m above the ground and %.0f m/s, thrusters off; full aft stick and a third of right rudder"),
			StartHeight, StallSpeedMetres));
		Lines.Add(TEXT("    s  phase      height m   m/s   sink   alpha deg   turn deg/s   yaw deg/s"));
		Stage = 1;
		PhaseClock = 0.0;
		Clock = 0.0;
		return;
	}

	SinceLine += DeltaSeconds;
	if (SinceLine >= 1.0)
	{
		SinceLine = 0.0;
		const TCHAR* Phases[] = { TEXT(""), TEXT("pulling"), TEXT("spinning"), TEXT("recovering"), TEXT("pulling out") };
		Lines.Add(FString::Printf(TEXT("%5.0f  %-10s %8.0f  %5.0f  %5.1f  %9.1f  %11.1f  %9.1f%s"), Clock, Phases[FMath::Clamp(Stage, 0, 4)],
			Ship->AltitudeMetres(), Ship->GetVelocity().Size() / 100.0, Sink, FMath::RadiansToDegrees(Air.AngleOfAttack),
			FMath::RadiansToDegrees(Rate), FMath::RadiansToDegrees(YawRate), bStalled ? TEXT("  stalled") : TEXT("")));
	}
	if (Ship->AltitudeMetres() < 50.0)
	{
		Lines.Add(TEXT("it reached the ground"));
		bRunning = false;
		Finish();
		return;
	}

	if (Stage == 1)
	{
		// The deliberate stall: stick hard back, a boot of rudder.
		Ship->SetAutoTurn(FVector3f(1.0f, 0.33f, 0.0f));
		if (bStalled && Lateral > StallDepartRadians && DepartedAt < 0.0)
		{
			DepartedAt = Clock;
			Stage = 2;
			PhaseClock = 0.0;
		}
		else if (PhaseClock > 30.0)
		{
			Lines.Add(TEXT("thirty seconds of full aft stick and it has not departed"));
			bRunning = false;
			Finish();
		}
		return;
	}
	if (Stage == 2)
	{
		// Into the spin, the inputs held.
		Ship->SetAutoTurn(FVector3f(1.0f, 1.0f, 0.0f));
		WorstRate = FMath::Max(WorstRate, Rate);
		if (PhaseClock >= StallSpinSeconds)
		{
			Stage = 3;
			PhaseClock = 0.0;
			LostToRecovery = Ship->AltitudeMetres();
		}
		return;
	}
	if (Stage == 3)
	{
		// The standard recovery: stick forward, rudder against the turn,
		// ailerons neutral -- until the wing flies and the turning stops.
		Ship->SetAutoTurn(FVector3f(-1.0f, YawRate > 0.0 ? -1.0f : 1.0f, 0.0f));
		WorstRate = FMath::Max(WorstRate, Rate);
		if (!bStalled && Rate < 0.1)
		{
			RecoveredAt = Clock;
			Stage = 4;
			PhaseClock = 0.0;
		}
		else if (PhaseClock > StallRecoverySeconds)
		{
			Lines.Add(TEXT("forty seconds of recovery inputs and it is still spinning"));
			bRunning = false;
			Finish();
		}
		return;
	}
	// Wings level first, then out of the dive holding ten degrees of angle of
	// attack -- safely under the stall -- until it stops sinking. A fixed pull
	// re-stalled it, and a pull before the wings were level pulled it down.
	const FVector3d Right = FVector3d(Ship->GetActorRightVector());
	const FVector3d Over = FVector3d(Ship->GetActorUpVector());
	const double Bank = FMath::RadiansToDegrees(FMath::Atan2(-FVector3d::DotProduct(Right, Up), FVector3d::DotProduct(Over, Up)));
	const double Pull = FMath::Abs(Bank) < 45.0
		? FMath::Clamp(0.1 * (10.0 - FMath::RadiansToDegrees(Air.AngleOfAttack)), -0.3, 0.5) : 0.0;
	Ship->SetAutoTurn(FVector3f(static_cast<float>(Pull), 0.0f, static_cast<float>(FMath::Clamp(-Bank / 60.0, -0.5, 0.5))));
	if (Sink < 5.0 || PhaseClock > 60.0)
	{
		LevelAt = Sink < 5.0 ? Clock : -1.0;
		LostToRecovery -= Ship->AltitudeMetres();
		bRunning = false;
		Finish();
	}
}

void ULedgerStallTrial::Finish()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr)
	{
		Ship->SetAutoTurn(FVector3f::ZeroVector);
		Ship->SetFlightMode(ELedgerFlightMode::Decoupled);
	}
	const bool bDeparted = DepartedAt >= 0.0;
	const bool bRecovered = RecoveredAt >= 0.0 && LevelAt >= 0.0;
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("departed at %.1f s, turning at up to %.0f deg/s; recovered at %.1f s and level at %.1f s, %.0f m lost from the recovery to level"),
		DepartedAt, FMath::RadiansToDegrees(WorstRate), RecoveredAt, LevelAt, LostToRecovery));
	Lines.Add(FString::Printf(TEXT("a deliberate stall departs controlled flight: %s"), bDeparted ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("and the standard recovery recovers it: %s"), bRecovered ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bDeparted && bRecovered ? TEXT("PASS") : TEXT("FAIL")));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("stall trial: %s"), *Line);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("stall.txt"))));
	FPlatformMisc::RequestExit(false);
}
