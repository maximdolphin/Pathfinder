#include "LedgerAudio.h"

#include "LedgerStorm.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerAir.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerWind.h"
#include "LedgerWorld.h"

namespace
{
	constexpr double AudioCentimetresPerMetre = 100.0;

	/// The dynamic pressure at which the wind is as loud as this gets.
	///
	/// 600 Pa is about 32 m/s of sea-level air -- a storm, the point past which
	/// louder conveys nothing because it is already the only thing you can
	/// hear. Below it the level follows the pressure directly, which is what
	/// the acceptance asks for and also what the ear does over this range.
	constexpr double AudioFullScalePascals = 600.0;

	/// Below this there is air but nothing to hear: 0.02 Pa is a metre a second
	/// at sea level, a still day. Without a floor the level never quite reaches
	/// zero and "silent above the atmosphere" becomes a matter of opinion.
	constexpr double AudioSilentPascals = 0.02;
}

// ------------------------------------------------------------- the generator

FLedgerWindGenerator::FLedgerWindGenerator(int32 InSampleRate)
	: SampleRate(InSampleRate > 0 ? InSampleRate : 48000)
{
}

void FLedgerWindGenerator::SetTarget(float InGain, float InCutoffHz)
{
	TargetGain.store(InGain, std::memory_order_relaxed);
	TargetCutoffHz.store(InCutoffHz, std::memory_order_relaxed);
}

int32 FLedgerWindGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	const float WantGain = TargetGain.load(std::memory_order_relaxed);
	const float WantCutoff = TargetCutoffHz.load(std::memory_order_relaxed);

	// A twentieth of a second to cross the whole range, which is fast enough
	// that flying out of a gale sounds immediate and slow enough that a
	// per-frame jitter in the wind query is not a buzz.
	const float Step = 1.0f / FMath::Max(1.0f, 0.05f * SampleRate);

	double SumOfSquares = 0.0;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		Gain = FMath::FInterpConstantTo(Gain, WantGain, Step, 1.0f);
		CutoffHz = FMath::FInterpConstantTo(CutoffHz, WantCutoff, Step, 4000.0f);

		// One pole, coefficient from the cutoff and the rate. Nothing here is
		// trying to be a filter library: it is the discretisation of a
		// first-order lag, which is what a boundary layer's spectrum looks
		// like to within the accuracy anybody can hear.
		const float Alpha = FMath::Clamp(
			1.0f - FMath::Exp(-2.0f * static_cast<float>(LedgerPi)
				* CutoffHz / SampleRate), 0.0f, 1.0f);

		const float White = Noise.FRandRange(-1.0f, 1.0f);
		Filtered += Alpha * (White - Filtered);

		// The pole eats most of the amplitude, so it is put back: without this
		// the level would depend on the cutoff as much as on the wind, and the
		// gain would stop meaning what it says.
		const float Sample = Gain * Filtered * FMath::Sqrt(2.0f / FMath::Max(Alpha, 1e-4f));
		OutAudio[Index] = FMath::Clamp(Sample, -1.0f, 1.0f);
		SumOfSquares += static_cast<double>(OutAudio[Index]) * OutAudio[Index];
	}

	Rms.store(NumSamples > 0
		? static_cast<float>(FMath::Sqrt(SumOfSquares / NumSamples))
		: 0.0f, std::memory_order_relaxed);
	return NumSamples;
}

// ----------------------------------------------------------------- the voice

ISoundGeneratorPtr ULedgerWindVoice::CreateSoundGenerator(
	const FSoundGeneratorInitParams& InParams)
{
	Generator = MakeShared<FLedgerWindGenerator, ESPMode::ThreadSafe>(
		InParams.SampleRate);
	Generator->SetTarget(PendingGain, PendingCutoffHz);
	return Generator;
}

void ULedgerWindVoice::SetTarget(float InGain, float InCutoffHz)
{
	PendingGain = InGain;
	PendingCutoffHz = InCutoffHz;
	if (Generator.IsValid())
	{
		Generator->SetTarget(InGain, InCutoffHz);
	}
}

float ULedgerWindVoice::LastRms() const
{
	return Generator.IsValid() ? Generator->LastRms() : 0.0f;
}

// ------------------------------------------------------------- the subsystem

bool ULedgerAudio::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerAudio::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerAudio, STATGROUP_Tickables);
}

double ULedgerAudio::GainFromDynamicPressure(double Pascals)
{
	if (!(Pascals > AudioSilentPascals))
	{
		return 0.0;
	}

	// Linear in pressure up to full scale. Not in speed: the acceptance is
	// about dynamic pressure because that is the quantity that carries the
	// air's thinness into the answer, and squaring the speed here instead
	// would be the same curve on one world and the wrong one on every other.
	return FMath::Min(Pascals / AudioFullScalePascals, 1.0);
}

double ULedgerAudio::CutoffFromSpeed(double MetresPerSecond)
{
	// **Smaller eddies at higher speed.** The energy-containing scale of a
	// boundary layer falls as the flow quickens, so the noise it radiates moves
	// up in frequency -- the difference between a moan around a building and a
	// hiss past an ear. 200 Hz still, 3 kHz in a gale, linear between.
	const double Speed = FMath::Max(MetresPerSecond, 0.0);
	return FMath::Clamp(200.0 + Speed * 90.0, 200.0, 3000.0);
}

double ULedgerAudio::HeardRms() const
{
	return Voice != nullptr ? Voice->LastRms() : 0.0;
}

void ULedgerAudio::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Attached to nothing and not spatialised: this is the sound of the air the
	// listener is standing in, which arrives from every direction at once.
	// A positioned emitter would be a wind that comes from somewhere, which is
	// a different and wrong thing.
	Voice = NewObject<ULedgerWindVoice>(&InWorld);
	Voice->bAlwaysPlay = true;
	Voice->bIsUISound = true;
	Voice->bAllowSpatialization = false;
	Voice->RegisterComponentWithWorld(&InWorld);
	Voice->Start();
	Voice->SetTarget(0.0f, 200.0f);
}

void ULedgerAudio::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	const APlayerController* Controller =
		World != nullptr ? World->GetFirstPlayerController() : nullptr;
	ULedgerWorldBuilder* Builder =
		World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	const ULedgerWind* Wind =
		World != nullptr ? World->GetSubsystem<ULedgerWind>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Controller == nullptr || Builder == nullptr || Wind == nullptr
		|| Planet == nullptr)
	{
		return;
	}

	FVector Eye = FVector::ZeroVector;
	FRotator Ignored = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(Eye, Ignored);

	const FVector3d Offset = FVector3d(Eye) - FVector3d(Planet->GetActorLocation());
	const double DistanceCm = Offset.Length();
	if (!(DistanceCm > 0.0))
	{
		return;
	}
	const FVector3d Up = Offset / DistanceCm;
	const double Altitude = FMath::Max(
		(DistanceCm - Planet->SurfaceRadiusAt(Up)) / AudioCentimetresPerMetre, 0.0);

	const FLedgerAirProfile Air = LedgerAir::For(
		Builder->GetSystem(), Builder->GetHomeBodyIndex(),
		Builder->GetWhenSeconds());

	// **One wind, asked for the same way everything else asks.** T093's whole
	// point: the flight model, the grass and this are reading the same
	// subsystem, so they cannot disagree about what the weather is doing.
	// T107: and the storm's gusts, which the ship already feels: a storm is
	// loud because the air in it is moving harder, not because it was told to be.
	const ULedgerStorm* Storms = World->GetSubsystem<ULedgerStorm>();
	const double Speed = (Wind->WindAtMetres(Eye)
		+ (Storms != nullptr ? FVector3d(Storms->GustAt(Eye)) / AudioCentimetresPerMetre : FVector3d::ZeroVector)).Length();

	FHeard Now;
	Now.AltitudeMetres = Altitude;
	Now.DensityKgPerM3 = LedgerAir::DensityAt(Air, Altitude);
	Now.SpeedMetresPerSecond = Speed;
	Now.DynamicPressurePascals = LedgerAir::DynamicPressure(Air, Altitude, Speed);
	Now.Gain = GainFromDynamicPressure(Now.DynamicPressurePascals);
	Now.CutoffHz = CutoffFromSpeed(Speed);
	LastHeard = Now;

	if (Voice != nullptr)
	{
		Voice->SetTarget(
			static_cast<float>(Now.Gain), static_cast<float>(Now.CutoffHz));
	}

	if (LastLoggedGain < 0.0 || FMath::Abs(Now.Gain - LastLoggedGain) > 0.05)
	{
		UE_LOG(LogLedger, Log,
			TEXT("audio: %.0f m altitude, rho %.4f kg/m3, wind %.1f m/s, "
				 "q %.1f Pa, level %.2f at %.0f Hz"),
			Now.AltitudeMetres, Now.DensityKgPerM3, Now.SpeedMetresPerSecond,
			Now.DynamicPressurePascals, Now.Gain, Now.CutoffHz);
		LastLoggedGain = Now.Gain;
	}
}
