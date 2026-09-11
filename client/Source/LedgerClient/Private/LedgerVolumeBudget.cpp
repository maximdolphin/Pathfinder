#include "LedgerVolumeBudget.h"

#include "DynamicRHI.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LedgerAtmosphere.h"
#include "LedgerLog.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"

namespace
{
	struct FVolumeLevel
	{
		float CloudViewSamples;
		float CloudShadowSamples;
		int32 FogGridPixels;
		float DropScale;
	};

	/// Full detail first. Each step takes roughly a third off the volumetric
	/// passes: fewer cloud samples, bigger froxels, fewer drops.
	const FVolumeLevel VolumeLevels[ULedgerVolumeBudget::LevelCount] =
	{
		{ 1.2f, 1.0f,  8, 1.00f },
		{ 0.8f, 0.7f, 12, 0.70f },
		{ 0.5f, 0.5f, 16, 0.50f },
		{ 0.3f, 0.3f, 24, 0.35f },
	};

	/// Seconds between steps, so one slow frame is not a level.
	constexpr double VolumeStepSeconds = 1.0;

	/// Room to climb back: a level up only when the frame is this far under.
	constexpr double VolumeHeadroom = 0.75;

	void VolumeSet(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}
}

bool ULedgerVolumeBudget::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerVolumeBudget::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerVolumeBudget, STATGROUP_Tickables);
}

void ULedgerVolumeBudget::Apply(int32 InLevel)
{
	InLevel = FMath::Clamp(InLevel, 0, LevelCount - 1);
	const UWorld* World = GetWorld();
	const ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerAtmosphere* Atmosphere = Builder != nullptr ? Builder->GetAtmosphere() : nullptr;
	if (Atmosphere == nullptr)
	{
		return;
	}
	const FVolumeLevel& Chosen = VolumeLevels[InLevel];
	Atmosphere->SetCloudQuality(Chosen.CloudViewSamples, Chosen.CloudShadowSamples);
	VolumeSet(TEXT("r.VolumetricFog.GridPixelSize"), static_cast<float>(Chosen.FogGridPixels));
	VolumeSet(TEXT("Ledger.Precip.DropScale"), Chosen.DropScale);
	if (InLevel != CurrentLevel)
	{
		UE_LOG(LogLedger, Log, TEXT("volume budget: level %d (clouds %.1f/%.1f samples, fog %d px, drops %.0f%%) at %.1f ms GPU against %.1f"),
			InLevel, Chosen.CloudViewSamples, Chosen.CloudShadowSamples, Chosen.FogGridPixels,
			Chosen.DropScale * 100.0f, Smoothed, Target);
	}
	CurrentLevel = InLevel;
}

void ULedgerVolumeBudget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}
	if (!bTargetRead)
	{
		bTargetRead = true;
		FParse::Value(FCommandLine::Get(), TEXT("gpubudgetms="), Target);
	}

	// The engine's GPU frame counter, smoothed over a few dozen frames. It lags
	// a frame or two by construction, which a one-second step does not mind.
	const double GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	Smoothed = Smoothed <= 0.0 ? GpuMs : FMath::Lerp(Smoothed, GpuMs, 0.1);

	if (Held >= 0)
	{
		if (Held != CurrentLevel)
		{
			Apply(Held);
		}
		return;
	}
	if (CurrentLevel < 0)
	{
		Apply(0);
		return;
	}
	SinceChange += DeltaSeconds;
	if (SinceChange < VolumeStepSeconds)
	{
		return;
	}
	SinceChange = 0.0;
	if (Smoothed > Target && CurrentLevel < LevelCount - 1)
	{
		Apply(CurrentLevel + 1);
	}
	else if (Smoothed < Target * VolumeHeadroom && CurrentLevel > 0)
	{
		Apply(CurrentLevel - 1);
	}
}
