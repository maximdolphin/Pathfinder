// How loud the wind is, all the way up. T102.
//
// The acceptance is two claims: the noise scales with dynamic pressure, and it
// stops entirely above the atmosphere. Both are about a curve rather than a
// moment, so this walks a listener from the ground to well past the top of the
// air and writes down what it hears at each rung -- the density, the wind, the
// dynamic pressure, the level, and what the generator actually put out.
//
// It runs the real subsystem rather than recomputing the arithmetic: a fixture
// that worked the answer out for itself would agree with itself and prove
// nothing.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerWindHeard.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerWindHeard : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Report();

	struct FRung
	{
		double AltitudeMetres = 0.0;
		double DensityKgPerM3 = 0.0;
		double SpeedMetresPerSecond = 0.0;
		double DynamicPressurePascals = 0.0;
		double Gain = 0.0;
		double CutoffHz = 0.0;
		double Rms = 0.0;
	};

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FLedgerAirProfile Air;
	FVector3d Anchor = FVector3d::UnitZ();
	int32 Home = INDEX_NONE;

	TArray<double> Ladder;
	TArray<FRung> Rungs;

	int32 Step = 0;
	double Settle = 0.0;
	bool bRunning = false;
};
