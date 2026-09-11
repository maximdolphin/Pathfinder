#include "LedgerShipTrial.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerShipDefinition.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/// Above the air: what the file says about thrust is read without the
	/// atmosphere taking its share first (at 3 km it took more than half).
	constexpr double TrialHeightMetres = 150000.0;
	constexpr double TrialSettleSeconds = 15.0;
	constexpr double TrialBurnSeconds = 3.0;
	constexpr float TrialThrottle = 0.01f;
	/// A full yaw held this long: eight time constants of the rate hold.
	constexpr double TrialTurnSeconds = 1.0;
	constexpr double TrialRateHoldPerSecond = 8.0;
}

bool ULedgerShipTrial::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerShipTrial::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerShipTrial, STATGROUP_Tickables);
}

void ULedgerShipTrial::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("shiptrial"));
}

void ULedgerShipTrial::Tick(float DeltaSeconds)
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
	if (Planet == nullptr || Ship == nullptr)
	{
		return;
	}
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	Clock += DeltaSeconds;

	if (Stage == 0)
	{
		// Held level at three kilometres, nose along the local east.
		Up = Builder->GetSiteDirection().GetSafeNormal();
		const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up).GetSafeNormal();
		Ship->SetFlightEnabled(false);
		Ship->SetActorLocation(FVector(Centre + Up * (Planet->SurfaceRadiusAt(Up) + TrialHeightMetres * 100.0)));
		Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(East), FVector(Up)).Rotator());
		Ship->SetVelocity(FVector::ZeroVector);
		if (Clock < TrialSettleSeconds)
		{
			return;
		}
		// Let go with the lift trimmed against gravity, so what changes the
		// speed along the nose is the main engine and the air.
		Ship->SetAutoLift(static_cast<float>(Ship->SurfaceGravity / Ship->ManoeuvringThrust));
		Ship->SetAutoThrottle(TrialThrottle);
		Ship->SetFlightEnabled(true);
		StartVelocity = FVector3d(Ship->GetVelocity());
		Stage = 1;
		Clock = 0.0;
		return;
	}
	const FLedgerShipDefinition& Definition = Ship->Definition();
	if (Stage == 1)
	{
		if (Clock < TrialBurnSeconds)
		{
			return;
		}
		Ship->SetAutoThrottle(0.0f);
		const FVector3d Forward = FVector3d(Ship->GetActorForwardVector());
		Gained = FVector3d::DotProduct(FVector3d(Ship->GetVelocity()) - StartVelocity, Forward) / 100.0;
		Expected = Definition.Flight.MainThrust * TrialThrottle * Clock;
		Burned = Clock;

		// Then a full yaw from rest: T132 and T133, the turn made by the
		// nozzles the allocator picks, on the inertia the ship is built with.
		StartForward = Forward;
		StartVelocity = FVector3d(Ship->GetVelocity());
		Ship->SetAutoTurn(FVector3f(0.0f, 1.0f, 0.0f));
		Stage = 2;
		Clock = 0.0;
		return;
	}
	if (Clock < TrialTurnSeconds)
	{
		return;
	}
	bRunning = false;
	Ship->SetAutoTurn(FVector3f::ZeroVector);
	const FVector3d Omega = Ship->AngularVelocity();
	const double YawWanted = FMath::DegreesToRadians(Definition.Flight.YawRate);
	const double Turned = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(StartForward, FVector3d(Ship->GetActorForwardVector())), -1.0, 1.0));
	// The rate closes on what was asked as 1 - exp(-k t), so the angle is the
	// rate times t less what the closing took: w (t - (1 - exp(-k t)) / k).
	const double TurnExpected = YawWanted * (Clock - (1.0 - FMath::Exp(-TrialRateHoldPerSecond * Clock)) / TrialRateHoldPerSecond);
	const double Drift = (FVector3d(Ship->GetVelocity()) - StartVelocity).Length() / 100.0;
	int32 Firing = 0;
	for (const double Thrust : Ship->Allocation().ThrustNewtons)
	{
		Firing += Thrust > 0.0 ? 1 : 0;
	}

	const FVector Scale = Ship->GetActorScale3D();
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("A ship from a data file, flown (T108): %s."), *Definition.Name));
	Lines.Add(FString::Printf(TEXT("from the file: %d components, %d connections, %.1f t, %.1f m long, %.0f m/s2 main thrust"),
		Definition.Components.Num(), Definition.Connections.Num(), Definition.MassKg() / 1000.0,
		Definition.Hull.LengthMetres, Definition.Flight.MainThrust));
	Lines.Add(FString::Printf(TEXT("the actor: hull scaled %.2f x %.2f x %.2f, main thrust %.0f m/s2"),
		Scale.X, Scale.Y, Scale.Z, Ship->MainThrust / 100.0));
	Lines.Add(FString::Printf(TEXT("a hundredth of the main engine for %.2f s: %.1f m/s gained along the nose against %.1f m/s of thrust (the air takes the rest)"),
		Burned, Gained, Expected));
	Lines.Add(FString::Printf(TEXT("a full yaw for %.2f s: %.1f deg/s reached against %.1f asked (%.3f of it about the yaw axis), %.1f deg turned against %.1f, %.2f m/s of drift, %d of %d nozzles firing at the end"),
		Clock, FMath::RadiansToDegrees(Omega.Length()), Definition.Flight.YawRate, Omega.Length() > 0.0 ? FMath::Abs(Omega.Z) / Omega.Length() : 0.0,
		FMath::RadiansToDegrees(Turned), FMath::RadiansToDegrees(TurnExpected), Drift, Firing, Definition.Nozzles.Num()));
	const bool bFromFile = Definition.Components.Num() > 0 && FMath::IsNearlyEqual(Ship->MainThrust / 100.0, Definition.Flight.MainThrust, 1.0);
	const bool bFlies = FMath::Abs(Gained - Expected) <= 0.05 * Expected;
	const bool bTurns = FMath::Abs(Omega.Length() - YawWanted) <= 0.05 * YawWanted && FMath::Abs(Omega.Z) > 0.99 * Omega.Length()
		&& FMath::Abs(Turned - TurnExpected) <= 0.05 * TurnExpected && Drift < 1.0;
	Lines.Add(FString::Printf(TEXT("built from its file: %s"), bFromFile ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("and it flies on its own numbers: %s"), bFlies ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("and turns on its nozzles and its inertia: %s"), bTurns ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bFromFile && bFlies && bTurns ? TEXT("PASS") : TEXT("FAIL")));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("ship trial: %s"), *Line);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			FString::Printf(TEXT("ship-%s.txt"), *Definition.Name))));
	FPlatformMisc::RequestExit(false);
}
