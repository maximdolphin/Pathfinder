// A ship, heard from inside. T126.
//
// Three voices from the component state and nothing else: the engine's note,
// its pitch the throttle and its loudness what the thrusters are actually
// giving; the coolant pumps, whining harder the hotter the reactor runs; and
// the most urgent alarm, in the tone and rhythm that is that system's own.
// Silence from a thruster that is being asked for thrust is itself a sound.

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerShipAudio.generated.h"

/// The engine's hum and rumble, the pumps' low noise and an alarm tone, mixed,
/// with every level and pitch pushed in from the game thread and glided to per
/// sample (M5P: the first playtest heard the old saw, a 1 kHz hiss and a gain
/// that stepped sixty times a second, and called it static).
class LEDGERCLIENT_API FLedgerShipToneGenerator : public ISoundGenerator
{
public:
	explicit FLedgerShipToneGenerator(int32 InSampleRate) : SampleRate(InSampleRate) {}

	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
	virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return 1024; }

	std::atomic<float> EngineHz{ 55.0f };
	std::atomic<float> EngineGain{ 0.0f };
	std::atomic<float> PumpGain{ 0.0f };
	std::atomic<float> AlarmHz{ 0.0f };
	std::atomic<float> AlarmOn{ 0.0f };
	std::atomic<float> AlarmOff{ 1.0f };
	std::atomic<float> AlarmGain{ 0.0f };

private:
	int32 SampleRate = 48000;
	double EnginePhase = 0.0;
	double AlarmPhase = 0.0;
	double AlarmClock = 0.0;
	// Two one-pole stages each: the rumble under the engine, the pumps' noise.
	float Rumble = 0.0f;
	float RumbleLow = 0.0f;
	float Pumped = 0.0f;
	float PumpedLow = 0.0f;
	// What is actually sounding, gliding towards what the game thread asked for.
	float EngineNow = 0.0f;
	float PumpNow = 0.0f;
	float AlarmNow = 0.0f;
	float GateNow = 0.0f;
	double PitchNow = 55.0;
	FRandomStream Noise{ 20260911 };
};

UCLASS()
class LEDGERCLIENT_API ULedgerShipVoice : public USynthComponent
{
	GENERATED_BODY()

public:
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;
	TSharedPtr<FLedgerShipToneGenerator, ESPMode::ThreadSafe> Generator;
};

UCLASS()
class LEDGERCLIENT_API ULedgerShipAudio : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/// What is sounding now, for the fixture: the alarm's words and tone, and
	/// the engine and pump levels.
	FString Sounding() const { return AlarmSays; }
	double SoundingHz() const { return LastAlarmHz; }
	double EngineLevel() const { return LastEngine; }
	double PumpLevel() const { return LastPump; }

private:
	UPROPERTY()
	TObjectPtr<ULedgerShipVoice> Voice;

	FString AlarmSays;
	double LastAlarmHz = 0.0;
	double LastEngine = 0.0;
	double LastPump = 0.0;
};
