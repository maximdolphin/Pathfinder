// Point a camera at the moon and photograph its phase. T074.
//
// The tests settle whether the phase is right; they compare two computations.
// This settles whether the thing in the sky is the thing the computation
// describes, which no amount of agreement between two numbers can.
//
// It captures the same body at eight points around its month, so the sequence
// is a phase cycle rather than one lucky crescent.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerMoonShot.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerMoonShot : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Report();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FVector3d Anchor = FVector3d::UnitZ();

	/// Which body is being photographed, and how long its month is.
	int32 Target = INDEX_NONE;
	double MonthSeconds = 0.0;

	TArray<double> LitFraction;
	TArray<double> AltitudeDegrees;
	TArray<double> AngularDiameterDegrees;

	int32 Step = 0;

	/// Whether this step has aimed yet.
	///
	/// Aiming and capturing are two ticks, not one. The spheres in the sky are
	/// placed by ULedgerSkyBodies on its own tick, so setting the clock and
	/// photographing in the same frame photographs the sky as it was one step
	/// ago -- which is how eight frames of empty blue arrived with the log
	/// insisting the moon was overhead.
	bool bAimed = false;

	bool bRunning = false;
	double Settle = 0.0;
};
