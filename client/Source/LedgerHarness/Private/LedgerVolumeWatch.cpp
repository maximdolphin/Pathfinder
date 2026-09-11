#include "LedgerVolumeWatch.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "DynamicRHI.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerSky.h"
#include "LedgerVolumeBudget.h"
#include "LedgerWeather.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* VolumePhases[] =
	{
		TEXT("volumetrics off"), TEXT("level 0 (full)"), TEXT("level 1"), TEXT("level 2"), TEXT("level 3"),
		TEXT("budget, 4 ms allowed"), TEXT("budget, squeezed"),
	};

	/// Settle, then sample: long enough for the cloud history and the froxel
	/// grid to reconverge after a change, and for a median to mean something.
	constexpr double VolumeSettleSeconds = 6.0;
	constexpr double VolumeSampleSeconds = 6.0;
	constexpr double VolumeFirstSettleSeconds = 25.0;

	constexpr double VolumeAllowanceMs = 4.0;

	double VolumeMedian(TArray<double> Values)
	{
		if (Values.Num() == 0)
		{
			return 0.0;
		}
		Values.Sort();
		return Values[Values.Num() / 2];
	}

	void VolumeSet(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}
}

bool ULedgerVolumeWatch::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerVolumeWatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerVolumeWatch, STATGROUP_Tickables);
}

void ULedgerVolumeWatch::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("volumebudget"));
	if (!bRunning)
	{
		return;
	}
	ULedgerWorldBuilder* Builder = InWorld.GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr)
	{
		bRunning = false;
		return;
	}
	const FLedgerSystem& System = Builder->GetSystem();
	const int32 Home = Builder->GetHomeBodyIndex();

	// The worst weather: the deepest daylit low in ten days.
	double Deepest = 0.0;
	const double Start = Builder->GetWhenSeconds();
	for (double When = Start; When <= Start + 10.0 * 86400.0; When += 3600.0)
	{
		for (int32 Index = 0; Index < LedgerWeather::CellCount(); ++Index)
		{
			const FLedgerPressureCell Cell = LedgerWeather::CellAt(System, Home, Index, When);
			const FVector3d Up(FMath::Cos(Cell.LatitudeRadians) * FMath::Cos(Cell.LongitudeRadians),
				FMath::Cos(Cell.LatitudeRadians) * FMath::Sin(Cell.LongitudeRadians), FMath::Sin(Cell.LatitudeRadians));
			if (Cell.bLow && Cell.AnomalyPascals < Deepest
				&& FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Up, When)) > 25.0)
			{
				Deepest = Cell.AnomalyPascals;
				StormSeconds = When;
				StormUp = Up;
			}
		}
	}
	Lines.Add(TEXT("The volumetric budget in the worst weather there is (T105)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("inside a low %.0f Pa deep at 1.5 km, in its cloud and its rain"), -Deepest));
	Lines.Add(TEXT(""));
	UE_LOG(LogLedger, Log, TEXT("volume watch: %s"), *Lines[2]);
}

void ULedgerVolumeWatch::Tick(float DeltaSeconds)
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
	ULedgerVolumeBudget* Budget = World->GetSubsystem<ULedgerVolumeBudget>();
	if (Planet == nullptr || Controller == nullptr || Budget == nullptr)
	{
		return;
	}
	Builder->SetWhenSeconds(StormSeconds);
	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetActorHiddenInGame(true);
	}
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const FVector Eye = FVector(Centre + StormUp * (Planet->Radius + 150000.0));
	FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), StormUp).GetSafeNormal();
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, FRotator::ZeroRotator, SpawnParams);
		Controller->SetViewTarget(Camera);
		Camera->GetCameraComponent()->SetFieldOfView(80.0f);
	}
	Camera->SetActorLocationAndRotation(Eye, FRotationMatrix::MakeFromXZ(FVector(East + StormUp * 0.1), FVector(StormUp)).Rotator());

	// What this phase is: everything off, a held level, or the budget choosing.
	const bool bOff = Phase == 0;
	VolumeSet(TEXT("r.VolumetricCloud"), bOff ? 0.0f : 1.0f);
	VolumeSet(TEXT("r.VolumetricFog"), bOff ? 0.0f : 1.0f);
	if (bOff)
	{
		Budget->HoldLevel(0);
		VolumeSet(TEXT("Ledger.Precip.DropScale"), 0.0f);
	}
	else if (Phase <= 4)
	{
		Budget->HoldLevel(Phase - 1);
	}
	else
	{
		Budget->HoldLevel(-1);
		// Four milliseconds of volumetrics on top of the frame without them;
		// then half of what full detail was measured to cost.
		const double Full = Medians[1] - Medians[0];
		Budget->SetTargetGpuMs(Medians[0] + (Phase == 5 ? VolumeAllowanceMs : FMath::Max(Full * 0.5, 0.05)));
	}

	Clock += DeltaSeconds;
	const double Settle = Phase == 0 ? VolumeFirstSettleSeconds : (Phase >= 5 ? VolumeSettleSeconds * 2.0 : VolumeSettleSeconds);
	if (Clock < Settle)
	{
		return;
	}
	Samples.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
	if (Clock < Settle + VolumeSampleSeconds)
	{
		return;
	}

	Medians[Phase] = VolumeMedian(Samples);
	if (Phase >= 5)
	{
		Chosen[Phase - 5] = Budget->Level();
	}
	if (Phase == 1)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("volume-full.png"))), false, false);
	}
	const FString Line = FString::Printf(TEXT("%-22s GPU %6.2f ms median over %d frames, volumetrics %+.2f ms%s"),
		VolumePhases[Phase], Medians[Phase], Samples.Num(), Medians[Phase] - Medians[0],
		Phase >= 5 ? *FString::Printf(TEXT(", the budget chose level %d against %.2f ms"), Budget->Level(), Budget->TargetGpuMs()) : TEXT(""));
	UE_LOG(LogLedger, Log, TEXT("volume watch: %s"), *Line);
	Lines.Add(Line);

	Samples.Reset();
	Clock = 0.0;
	++Phase;
	if (Phase >= UE_ARRAY_COUNT(VolumePhases))
	{
		bRunning = false;
		Finish();
	}
}

void ULedgerVolumeWatch::Finish()
{
	const double Budgeted = Medians[5] - Medians[0];
	const double Squeezed = Medians[6] - Medians[0];
	const double Full = Medians[1] - Medians[0];
	const bool bWithin = Budgeted <= VolumeAllowanceMs;
	// Degrades in detail: squeezed below what full detail costs, the budget
	// steps down a level rather than holding full detail over the target.
	const bool bDetail = Full <= 0.1 || Chosen[1] > 0;
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("the worst weather costs %.2f ms at full detail and %.2f ms as budgeted"), Full, Budgeted));
	Lines.Add(FString::Printf(TEXT("within four milliseconds: %s"), bWithin ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("squeezed, it gives up detail (level %d, %.2f ms) rather than the frame: %s"),
		Chosen[1], Squeezed, bDetail ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bWithin && bDetail ? TEXT("PASS") : TEXT("FAIL")));
	for (int32 Index = Lines.Num() - 4; Index < Lines.Num(); ++Index)
	{
		UE_LOG(LogLedger, Log, TEXT("volume watch: %s"), *Lines[Index]);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("volume.txt"))));
	FPlatformMisc::RequestExit(false);
}
