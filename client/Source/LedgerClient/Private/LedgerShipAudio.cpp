#include "LedgerShipAudio.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerShip.h"
#include "LedgerShipSystemsComponent.h"

int32 FLedgerShipToneGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const float Rate = static_cast<float>(FMath::Max(SampleRate, 1));
	const double Step = 1.0 / Rate;
	const float Engine = EngineGain.load(std::memory_order_relaxed);
	const double Pitch = EngineHz.load(std::memory_order_relaxed);
	const float Pump = PumpGain.load(std::memory_order_relaxed);
	const double Alarm = AlarmHz.load(std::memory_order_relaxed);
	const double On = AlarmOn.load(std::memory_order_relaxed);
	const double Period = On + AlarmOff.load(std::memory_order_relaxed);
	const float AlarmLevel = AlarmGain.load(std::memory_order_relaxed);

	// **Glided, not stepped (M5P).** Every level and the pitch move towards what
	// the game thread asked for over about 30 ms, the alarm's gate over 5. They
	// used to jump once a frame, and a level that steps sixty times a second is
	// a crackle -- half of what the first playtest heard as static.
	const float Glide = 1.0f - FMath::Exp(-1.0f / (0.03f * Rate));
	const float GateGlide = 1.0f - FMath::Exp(-1.0f / (0.005f * Rate));
	// One-pole low passes as a fraction per sample, two stages each so the
	// noise falls away rather than hisses: the rumble near 90 Hz, the pumps
	// near 250. The pumps were one stage near 1 kHz -- the other half.
	const float RumbleCut = 1.0f - FMath::Exp(-UE_TWO_PI * 90.0f / Rate);
	const float PumpCut = 1.0f - FMath::Exp(-UE_TWO_PI * 250.0f / Rate);

	for (int32 Sample = 0; Sample < NumSamples; ++Sample)
	{
		EngineNow += Glide * (Engine - EngineNow);
		PumpNow += Glide * (Pump - PumpNow);
		AlarmNow += Glide * (AlarmLevel - AlarmNow);
		PitchNow += Glide * (Pitch - PitchNow);

		// The engine: a note with two softer harmonics over a low rumble. A
		// turbine through a hull is a hum; the saw this replaced was a buzz.
		EnginePhase = FMath::Fmod(EnginePhase + PitchNow * Step, 1.0);
		const float Angle = static_cast<float>(EnginePhase) * UE_TWO_PI;
		const float Note = FMath::Sin(Angle) + 0.35f * FMath::Sin(2.0f * Angle) + 0.15f * FMath::Sin(3.0f * Angle);
		Rumble += RumbleCut * (Noise.FRandRange(-1.0f, 1.0f) - Rumble);
		RumbleLow += RumbleCut * (Rumble - RumbleLow);

		// The pumps: a low wash that rises with the heat.
		Pumped += PumpCut * (Noise.FRandRange(-1.0f, 1.0f) - Pumped);
		PumpedLow += PumpCut * (Pumped - PumpedLow);

		// The alarm: a sine in its own rhythm, its gate faded rather than cut.
		AlarmClock = Period > 0.0 ? FMath::Fmod(AlarmClock + Step, Period) : 0.0;
		AlarmPhase = FMath::Fmod(AlarmPhase + Alarm * Step, 1.0);
		GateNow += GateGlide * ((Alarm > 0.0 && AlarmClock < On ? 1.0f : 0.0f) - GateNow);
		const float Tone = GateNow * FMath::Sin(static_cast<float>(AlarmPhase) * UE_TWO_PI);

		OutAudio[Sample] = FMath::Clamp(EngineNow * (0.45f * Note + 6.0f * RumbleLow)
			+ PumpNow * 6.0f * PumpedLow + AlarmNow * Tone, -1.0f, 1.0f);
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
		// Lower than it was (55-165 Hz): a hull hums, it does not whine.
		Out.EngineHz.store(static_cast<float>(45.0 + 70.0 * Throttle), std::memory_order_relaxed);
		Out.EngineGain.store(static_cast<float>(LastEngine), std::memory_order_relaxed);
		Out.PumpGain.store(static_cast<float>(LastPump), std::memory_order_relaxed);
		Out.AlarmHz.store(static_cast<float>(LastAlarmHz), std::memory_order_relaxed);
		Out.AlarmOn.store(Alarms.Num() > 0 ? static_cast<float>(Alarms[0].OnSeconds) : 0.0f, std::memory_order_relaxed);
		Out.AlarmOff.store(Alarms.Num() > 0 ? static_cast<float>(Alarms[0].OffSeconds) : 1.0f, std::memory_order_relaxed);
		Out.AlarmGain.store(Alarms.Num() > 0 ? 0.35f : 0.0f, std::memory_order_relaxed);
	}
}
