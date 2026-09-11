#include "LedgerShipAudio.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerShip.h"
#include "LedgerShipSystemsComponent.h"

int32 FLedgerShipToneGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const double Step = 1.0 / FMath::Max(SampleRate, 1);
	const float Engine = EngineGain.load(std::memory_order_relaxed);
	const double Pitch = EngineHz.load(std::memory_order_relaxed);
	const float Pump = PumpGain.load(std::memory_order_relaxed);
	const double Alarm = AlarmHz.load(std::memory_order_relaxed);
	const double On = AlarmOn.load(std::memory_order_relaxed);
	const double Period = On + AlarmOff.load(std::memory_order_relaxed);
	const float AlarmLevel = AlarmGain.load(std::memory_order_relaxed);
	for (int32 Sample = 0; Sample < NumSamples; ++Sample)
	{
		// The engine: a saw, which is what a turbine's blade-pass sounds like
		// through a hull.
		EnginePhase = FMath::Fmod(EnginePhase + Pitch * Step, 1.0);
		const float Saw = static_cast<float>(EnginePhase * 2.0 - 1.0);
		// The pumps: noise through a low pass, a hiss that rises with the heat.
		Pumped += 0.15f * (Noise.FRandRange(-1.0f, 1.0f) - Pumped);
		// The alarm: a sine, gated on and off in its own rhythm.
		AlarmClock = Period > 0.0 ? FMath::Fmod(AlarmClock + Step, Period) : 0.0;
		AlarmPhase = FMath::Fmod(AlarmPhase + Alarm * Step, 1.0);
		const float Tone = (Alarm > 0.0 && AlarmClock < On) ? FMath::Sin(static_cast<float>(AlarmPhase * UE_TWO_PI)) : 0.0f;
		OutAudio[Sample] = FMath::Clamp(Engine * Saw * 0.5f + Pump * Pumped + AlarmLevel * Tone, -1.0f, 1.0f);
	}
	return NumSamples;
}

ISoundGeneratorPtr ULedgerShipVoice::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	Generator = MakeShared<FLedgerShipToneGenerator, ESPMode::ThreadSafe>(static_cast<int32>(InParams.SampleRate));
	return Generator;
}

bool ULedgerShipAudio::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerShipAudio::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerShipAudio, STATGROUP_Tickables);
}

void ULedgerShipAudio::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Voice = NewObject<ULedgerShipVoice>(&InWorld);
	Voice->bAlwaysPlay = true;
	Voice->bIsUISound = true;
	Voice->bAllowSpatialization = false;
	Voice->RegisterComponentWithWorld(&InWorld);
	Voice->Start();
}

void ULedgerShipAudio::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	const ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;
	const ULedgerShipSystemsComponent* Systems = Ship != nullptr ? Ship->GetSystems() : nullptr;
	if (Systems == nullptr || !Systems->IsConfigured())
	{
		return;
	}
	const FLedgerShipDefinition& Definition = Systems->GetDefinition();
	const FLedgerShipState& State = Systems->GetState();

	// The engine: pitch from the throttle, loudness from what it is giving.
	const double Throttle = Ship->CurrentThrottle();
	LastEngine = 0.05 + 0.3 * Throttle * Systems->MainThrustShare();
	if (Systems->MainThrustShare() <= 0.0)
	{
		LastEngine = 0.0;
	}

	// The pumps: whining as the hottest plant climbs, while they have power.
	double Hottest = 0.0;
	bool bPumping = false;
	for (int32 Index = 0; Index < Definition.Components.Num(); ++Index)
	{
		if (Definition.Components[Index].Type == TEXT("PowerPlant"))
		{
			Hottest = FMath::Max(Hottest, State.Components[Index].TemperatureK);
		}
		if (Definition.Components[Index].Type == TEXT("Radiator"))
		{
			bPumping |= State.Components[Index].bPowered;
		}
	}
	LastPump = bPumping ? FMath::Clamp((Hottest - 330.0) / 150.0, 0.0, 1.0) * 0.3 : 0.0;

	// The most urgent alarm, in its own voice.
	const TArray<FLedgerAlarm> Alarms = LedgerShipSystems::Alarms(Definition, State, Systems->LastPower(), Systems->LastAirWarnings());
	AlarmSays = Alarms.Num() > 0 ? Alarms[0].Says : FString();
	LastAlarmHz = Alarms.Num() > 0 ? Alarms[0].ToneHz : 0.0;

	if (Voice != nullptr && Voice->Generator.IsValid())
	{
		FLedgerShipToneGenerator& Out = *Voice->Generator;
		Out.EngineHz.store(static_cast<float>(55.0 + 110.0 * Throttle), std::memory_order_relaxed);
		Out.EngineGain.store(static_cast<float>(LastEngine), std::memory_order_relaxed);
		Out.PumpGain.store(static_cast<float>(LastPump), std::memory_order_relaxed);
		Out.AlarmHz.store(static_cast<float>(LastAlarmHz), std::memory_order_relaxed);
		Out.AlarmOn.store(Alarms.Num() > 0 ? static_cast<float>(Alarms[0].OnSeconds) : 0.0f, std::memory_order_relaxed);
		Out.AlarmOff.store(Alarms.Num() > 0 ? static_cast<float>(Alarms[0].OffSeconds) : 1.0f, std::memory_order_relaxed);
		Out.AlarmGain.store(Alarms.Num() > 0 ? 0.35f : 0.0f, std::memory_order_relaxed);
	}
}
