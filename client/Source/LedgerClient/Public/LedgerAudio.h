// Wind, heard. T102.
//
// **Sound is a property of the air, not of the weather.** The same 30 m/s of
// wind is a gale at sea level, a whisper on a thin-aired world and nothing at
// all above the atmosphere, because what reaches an ear is the momentum flux
// the air carries: half rho v squared. So this asks LedgerAir for the density
// where the listener is and ULedgerWind for the speed there, multiplies them
// the one way that is physically meaningful, and turns the result into a level.
// There is no separate "is the player in space" flag to get out of step with
// the ephemeris -- vacuum is simply where rho is zero.
//
// The noise itself is generated rather than played: wind has no loop point and
// no asset, it is broadband noise shaped by how fast the air is moving past
// things. A faster wind excites smaller eddies, so the spectrum brightens with
// speed, which is the difference between a moan and a hiss.

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerAudio.generated.h"

/// Broadband noise through a one-pole low pass, with the cutoff and the gain
/// pushed in from the game thread.
///
/// Deliberately the simplest generator that is honest: white noise is what a
/// turbulent boundary layer radiates, and a single pole is enough to say
/// "faster wind, brighter sound" without inviting a synthesiser into a
/// simulation project.
class FLedgerWindGenerator : public ISoundGenerator
{
public:
	explicit FLedgerWindGenerator(int32 InSampleRate);

	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
	virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return 1024; }

	/// Set from the game thread each tick; read on the audio thread.
	void SetTarget(float InGain, float InCutoffHz);

	/// Root-mean-square of the last block actually rendered. **What came out,
	/// not what was asked for** -- the fixture reports this so "silent in
	/// vacuum" is a measurement of the audio and not of the intention.
	float LastRms() const { return Rms.load(std::memory_order_relaxed); }

private:
	int32 SampleRate = 48000;

	std::atomic<float> TargetGain{ 0.0f };
	std::atomic<float> TargetCutoffHz{ 400.0f };
	std::atomic<float> Rms{ 0.0f };

	/// Smoothed on the audio thread, so a step change in the wind is a fade
	/// rather than a click.
	float Gain = 0.0f;
	float CutoffHz = 400.0f;
	float Filtered = 0.0f;
	FRandomStream Noise{ 20260910 };
};

UCLASS()
class LEDGERCLIENT_API ULedgerWindVoice : public USynthComponent
{
	GENERATED_BODY()

public:
	virtual ISoundGeneratorPtr CreateSoundGenerator(
		const FSoundGeneratorInitParams& InParams) override;

	void SetTarget(float InGain, float InCutoffHz);
	float LastRms() const;

private:
	TSharedPtr<FLedgerWindGenerator, ESPMode::ThreadSafe> Generator;

	/// Kept because CreateSoundGenerator can be called again after a device
	/// swap, and the level has to survive it.
	float PendingGain = 0.0f;
	float PendingCutoffHz = 400.0f;
};

/// What the world sounds like, which for now is the wind.
UCLASS()
class LEDGERCLIENT_API ULedgerAudio : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/// The level the listener should be hearing, 0 to 1, and what it was
	/// computed from. Queried by the fixture and by anything that wants to know
	/// how loud it is without owning an audio device.
	struct FHeard
	{
		double AltitudeMetres = 0.0;
		double DensityKgPerM3 = 0.0;
		double SpeedMetresPerSecond = 0.0;
		double DynamicPressurePascals = 0.0;
		double Gain = 0.0;
		double CutoffHz = 0.0;
	};

	FHeard Heard() const { return LastHeard; }

	/// What the generator actually put out, root-mean-square over its last
	/// block. Zero when there is no audio device, which is why it is reported
	/// beside the level rather than instead of it.
	double HeardRms() const;

	/// Level from dynamic pressure, 0 to 1. **Pure, so it can be tested
	/// without an audio device**, which matters because the automation runs
	/// with the renderer and the mixer switched off.
	static double GainFromDynamicPressure(double Pascals);
	static double CutoffFromSpeed(double MetresPerSecond);

private:
	UPROPERTY()
	TObjectPtr<ULedgerWindVoice> Voice;

	FHeard LastHeard;
	double SinceLog = 0.0;
	double LastLoggedGain = -1.0;
};
