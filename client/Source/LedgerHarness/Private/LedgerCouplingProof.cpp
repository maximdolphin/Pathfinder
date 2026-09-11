#include "LedgerCouplingProof.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerShipAudio.h"
#include "LedgerShipDamageView.h"
#include "LedgerShipSystemsComponent.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr double ProofSettleSeconds = 15.0;
	constexpr float ProofDilation = 20.0f;
	/// Game seconds the damaged ship runs before the workshop, at most, and
	/// after it.
	constexpr double ProofDamagedSeconds = 2400.0;
	constexpr double ProofRepairedSeconds = 900.0;
}

bool ULedgerCouplingProof::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerCouplingProof::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerCouplingProof, STATGROUP_Tickables);
}

void ULedgerCouplingProof::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("couplingproof"));
	Lines.Add(TEXT("Playable proof: shoot out a power coupling (T131)."));
	Lines.Add(TEXT(""));
}

void ULedgerCouplingProof::Tick(float DeltaSeconds)
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
	if (Planet == nullptr || Systems == nullptr || !Systems->IsConfigured())
	{
		return;
	}
	const FLedgerShipDefinition& Definition = Systems->GetDefinition();
	FLedgerShipState& State = Systems->EditState();
	const ULedgerShipAudio* Audio = World->GetSubsystem<ULedgerShipAudio>();
	const ULedgerShipDamageView* Damage = World->GetSubsystem<ULedgerShipDamageView>();
	const int32 Coupling = Definition.FindComponent(TEXT("coupling_aft"));
	const int32 Reactor = Definition.FindComponent(TEXT("reactor"));
	Clock += DeltaSeconds;
	GameClock += DeltaSeconds;

	// The ship on the ground a kilometre from the town, the camera off its
	// quarter, where the aft coupling and the engines are in view.
	const FVector3d Site = Builder->GetSiteDirection().GetSafeNormal();
	const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Site).GetSafeNormal();
	const FVector3d Up = (Site + East * (100000.0 / Planet->Radius)).GetSafeNormal();
	const FVector3d North = FVector3d::CrossProduct(Up, East);
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const FVector Where = FVector(Centre + Up * (Planet->SurfaceRadiusAt(Up) + 250.0));
	Ship->SetFlightEnabled(false);
	Ship->SetActorLocation(Where);
	Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(North), FVector(Up)).Rotator());
	Ship->SetActorHiddenInGame(false);
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Where, FRotator::ZeroRotator, SpawnParams);
		Camera->GetCameraComponent()->SetFieldOfView(60.0f);
		Controller->SetViewTarget(Camera);
	}
	const FVector Eye = Where + FVector(-North * 1800.0 + East * 1300.0 + Up * 700.0);
	Camera->SetActorLocationAndRotation(Eye, FRotationMatrix::MakeFromXZ(Where - Eye, FVector(Up)).Rotator());

	auto Shot = [this](const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), FString::Printf(TEXT("coupling-%d-%s.png"), Frame++, Name))), false, false);
	};
	auto Powered = [&Definition, &State]()
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < Definition.Components.Num(); ++Index)
		{
			Count += State.Components[Index].bPowered && Definition.Components[Index].Param(TEXT("drawKw")) > 0.0 ? 1 : 0;
		}
		return Count;
	};

	if (Stage == 0)
	{
		// Shields up: the stance a ship is in when it is being shot at, and a
		// load on the reactor for the heat to come from.
		State.Components[Definition.FindComponent(TEXT("shield"))].bOn = true;
		if (Clock < ProofSettleSeconds)
		{
			return;
		}
		PoweredAtStart = Powered();
		ReactorAtHit = State.Components[Reactor].TemperatureK;
		Shot(TEXT("intact"));
		Lines.Add(FString::Printf(TEXT("before: thrust %.2f main and %.2f manoeuvring, reactor %.0f K, %d loads powered, scorch %.2f"),
			Systems->MainThrustShare(), Systems->ManoeuvringThrustShare(), ReactorAtHit, PoweredAtStart, Damage != nullptr ? Damage->Scorch() : 0.0));

		// The shot.
		LedgerShipSystems::Damage(Definition, State, Coupling, 1.0);
		Lines.Add(FString::Printf(TEXT("a shot destroys %s; downstream of it: %s"), *Definition.Components[Coupling].Id,
			*FString::JoinBy(LedgerShipSystems::DownstreamOf(Definition, Coupling, true), TEXT(", "), [&Definition](int32 Index) { return Definition.Components[Index].Id; })));
		UGameplayStatics::SetGlobalTimeDilation(World, ProofDilation);
		HitAt = GameClock;
		Stage = 1;
		return;
	}

	// ---- every game second or so: what has power, how hot, what it sounds like
	SinceSample += DeltaSeconds;
	const bool bSample = SinceSample >= 20.0;
	if (bSample)
	{
		SinceSample = 0.0;
	}
	for (int32 Index = 0; Index < Definition.Components.Num(); ++Index)
	{
		const FString& Id = Definition.Components[Index].Id;
		if (Definition.Components[Index].Param(TEXT("drawKw")) > 0.0 && !State.Components[Index].bPowered && State.Components[Index].bOn
			&& !Unpowered.Contains(Id))
		{
			Unpowered.Add(Id);
			LostOrder.Add(FString::Printf(TEXT("%s at +%.0f s"), *Id, GameClock - HitAt));
		}
	}
	ReactorPeak = FMath::Max(ReactorPeak, State.Components[Reactor].TemperatureK);
	bSawFire |= Damage != nullptr && Damage->FiresShown() > 0;
	bHeardAlarm |= Audio != nullptr && !Audio->Sounding().IsEmpty();
	// The first look after the systems have had a second: not a window,
	// because the frame after the shot can be a hitch that is several game
	// seconds long at this dilation, and a window it steps over reads nothing.
	if (Stage == 1 && !bTookAfterHit && GameClock - HitAt > 1.0)
	{
		bTookAfterHit = true;
		MainAfterHit = Systems->MainThrustShare();
		SideAfterHit = Systems->ManoeuvringThrustShare();
	}
	if (bSample)
	{
		Lines.Add(FString::Printf(TEXT("  +%5.0f s  thrust %.2f/%.2f  reactor %5.1f K  %2d powered  alarm: %s"),
			GameClock - HitAt, Systems->MainThrustShare(), Systems->ManoeuvringThrustShare(), State.Components[Reactor].TemperatureK,
			Powered(), Audio != nullptr && !Audio->Sounding().IsEmpty() ? *Audio->Sounding() : TEXT("-")));
	}

	if (Stage == 1)
	{
		if (Frame == 1 && GameClock - HitAt > 40.0)
		{
			Shot(TEXT("hit"));
		}
		if (Frame == 2 && State.Components[Reactor].TemperatureK > 400.0)
		{
			Shot(TEXT("hot"));
		}
		// The workshop, once the shedding has run its course or the time is up.
		if (GameClock - HitAt >= ProofDamagedSeconds)
		{
			// A workshop mends what the loss did as well as what was shot: the
			// shield cooked once the radiator pump went dark, and that is the
			// consequence the proof is about, not something to leave broken.
			double Parts = 500.0;
			for (int32 Index = 0; Index < Definition.Components.Num(); ++Index)
			{
				if (State.Components[Index].Condition >= 1.0)
				{
					continue;
				}
				const FLedgerRepair Repair = LedgerShipSystems::Repair(Definition, State, Index, true, &Parts);
				Lines.Add(FString::Printf(TEXT("the workshop mends %s for %.1f kg of parts; condition %.2f to %.2f"),
					*Definition.Components[Index].Id, Repair.PartsKg, Repair.ConditionBefore, Repair.ConditionAfter));
			}
			RepairedAt = GameClock;
			Unpowered.Reset();
			Stage = 2;
		}
		return;
	}

	if (GameClock - RepairedAt < ProofRepairedSeconds)
	{
		return;
	}
	ReactorAtEnd = State.Components[Reactor].TemperatureK;
	MainAtEnd = Systems->MainThrustShare();
	SideAtEnd = Systems->ManoeuvringThrustShare();
	PoweredAtEnd = Powered();
	Shot(TEXT("repaired"));
	UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
	bRunning = false;
	Finish();
}

void ULedgerCouplingProof::Finish()
{
	const bool bDead = MainAfterHit == 0.0 && SideAfterHit == 0.0;
	const bool bHot = ReactorPeak > ReactorAtHit + 30.0;
	const bool bOrdered = LostOrder.Num() >= 3;
	const bool bBack = MainAtEnd > 0.99 && SideAtEnd > 0.99 && ReactorAtEnd < ReactorPeak && PoweredAtEnd >= PoweredAtStart;
	Lines.Add(FString::Printf(TEXT("lost power, in order: %s"), *FString::Join(LostOrder, TEXT("; "))));
	Lines.Add(FString::Printf(TEXT("reactor %.0f K at the shot, %.0f K at worst, %.0f K after the repair; fire shown %d, alarm heard %d"),
		ReactorAtHit, ReactorPeak, ReactorAtEnd, bSawFire ? 1 : 0, bHeardAlarm ? 1 : 0));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("the thrusters on that bus go dead: %s"), bDead ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("heat climbs: %s"), bHot ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("systems degrade in graph order: %s"), bOrdered ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("repair brings the ship back: %s"), bBack ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bDead && bHot && bOrdered && bBack ? TEXT("PASS") : TEXT("FAIL")));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("coupling proof: %s"), *Line);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("coupling.txt"))));
	FPlatformMisc::RequestExit(false);
}
