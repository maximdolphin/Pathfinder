#include "LedgerEntryBurn.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	struct FEntryRun
	{
		const TCHAR* What;
		double GammaDegrees;
	};

	const FEntryRun EntryRuns[] =
	{
		{ TEXT("steep"),   -8.0 },
		{ TEXT("shallow"), -1.0 },
	};

	constexpr double EntryStartMetres = 120000.0;
	constexpr double EntrySpeedMetresPerSecond = 7800.0;
	constexpr double EntryEndMetres = 10000.0;
	constexpr double EntryMaxSeconds = 1500.0;
	/// Eight times real time. The flight model steps at a fixed 120 Hz of game
	/// time whatever the dilation, so this is the same entry, sooner.
	constexpr float EntryDilation = 8.0f;
	constexpr double EntrySettleSeconds = 10.0;
}

bool ULedgerEntryBurn::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerEntryBurn::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerEntryBurn, STATGROUP_Tickables);
}

void ULedgerEntryBurn::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("entryburn"));
	Lines.Add(TEXT("A steep entry and a shallow one, flown (T100)."));
	Lines.Add(TEXT(""));
}

void ULedgerEntryBurn::Tick(float DeltaSeconds)
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
	const FEntryRun& Now = EntryRuns[Step];
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());

	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		Controller->SetViewTarget(Camera);
		Camera->GetCameraComponent()->SetFieldOfView(60.0f);
	}

	if (!bStarted)
	{
		bStarted = true;
		bShot = false;
		Clock = 0.0;
		Settle = 0.0;
		const FVector3d Site = Builder->GetSiteDirection().GetSafeNormal();
		Ship->SetFlightEnabled(false);
		Ship->SetActorHiddenInGame(false);
		Ship->SetActorLocation(FVector(Centre + Site * (Planet->Radius + EntryStartMetres * 100.0)));
	}

	// Held at the top until the world has settled, then let go at the angle.
	if (!Ship->IsFlightEnabled())
	{
		Settle += DeltaSeconds;
		if (Settle < EntrySettleSeconds)
		{
			return;
		}
		const FVector3d Up = (FVector3d(Ship->GetActorLocation()) - Centre).GetSafeNormal();
		const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up).GetSafeNormal();
		const double Gamma = FMath::DegreesToRadians(Now.GammaDegrees);
		const FVector3d Heading = East * FMath::Cos(Gamma) + Up * FMath::Sin(Gamma);
		Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(Heading), FVector(Up)).Rotator());
		Ship->SetVelocity(FVector(Heading * EntrySpeedMetresPerSecond * 100.0));
		Ship->RepairHull();
		Ship->ResetEntryHeat();
		Ship->SetFlightEnabled(true);
		UGameplayStatics::SetGlobalTimeDilation(World, EntryDilation);
	}
	Clock += DeltaSeconds;

	// A chase camera off the ship's quarter, so the glow is on the hull.
	const FVector3d Position = FVector3d(Ship->GetActorLocation());
	const FVector3d Up = (Position - Centre).GetSafeNormal();
	const FVector3d Along = FVector3d(Ship->GetVelocity()).GetSafeNormal();
	const FVector3d Side = FVector3d::CrossProduct(Up, Along).GetSafeNormal();
	const FVector3d Eye = Position - Along * 3000.0 + Side * 1500.0 + Up * 800.0;
	Camera->SetActorLocationAndRotation(FVector(Eye),
		FRotationMatrix::MakeFromXZ(FVector(Position - Eye), FVector(Up)).Rotator());

	const FLedgerHeatState& Heat = Ship->EntryHeat();
	const double Km = (Position - Centre).Length() / 100000.0 - Planet->Radius / 100000.0;
	if (Heat.FluxWattsPerM2 >= Heat.PeakFluxWattsPerM2 && Heat.PeakFluxWattsPerM2 > 0.0)
	{
		PeakKm[Step] = Km;
	}
	// At the height of it: the first frame the flux has fallen a tenth off its
	// peak, which is as bright as the glow gets.
	if (!bShot && Heat.PeakFluxWattsPerM2 > 1.0e5 && Heat.FluxWattsPerM2 < 0.9 * Heat.PeakFluxWattsPerM2)
	{
		bShot = true;
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("entry-%s.png"), Now.What))), false, false);
	}

	if (Km > EntryEndMetres / 1000.0 && Clock < EntryMaxSeconds && Ship->HullIntegrity() > 0.0)
	{
		return;
	}

	UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
	Ship->SetFlightEnabled(false);
	Damage[Step] = Heat.Damage;
	const FString Line = FString::Printf(
		TEXT("%-7s %+.0f deg: peak %.2f MW/m2 at %.1f km, skin %.0f K, took in %.0f MJ/m2 and radiated %.0f, hull lost %.2f to heat (hull now %.2f), %.0f s"),
		Now.What, Now.GammaDegrees, Heat.PeakFluxWattsPerM2 / 1.0e6, PeakKm[Step], Heat.PeakKelvin,
		Heat.LoadJoulesPerM2 / 1.0e6, Heat.RadiatedJoulesPerM2 / 1.0e6, Heat.Damage, Ship->HullIntegrity(), Clock);
	UE_LOG(LogLedger, Log, TEXT("entry burn: %s"), *Line);
	Lines.Add(Line);

	++Step;
	bStarted = false;
	if (Step >= UE_ARRAY_COUNT(EntryRuns))
	{
		bRunning = false;
		Finish();
	}
}

void ULedgerEntryBurn::Finish()
{
	const bool bSteepBurns = Damage[0] > 0.0;
	const bool bShallowDoesNot = Damage[1] == 0.0;
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("the steep entry burns: %s"), bSteepBurns ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("the shallow entry does not: %s"), bShallowDoesNot ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bSteepBurns && bShallowDoesNot ? TEXT("PASS") : TEXT("FAIL")));
	for (int32 Index = Lines.Num() - 3; Index < Lines.Num(); ++Index)
	{
		UE_LOG(LogLedger, Log, TEXT("entry burn: %s"), *Lines[Index]);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("entry.txt"))));
	FPlatformMisc::RequestExit(false);
}
