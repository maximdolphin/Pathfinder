// The same sky code on three different atmospheres. T089.
//
// The acceptance is that three bodies render correctly from ground and orbit
// **from data alone** -- so this fixture contains no per-body anything. It reads
// the profile the body's mass, radius, temperature and composition produce,
// photographs the sky from the ground and from two radii out, and writes down
// the numbers that produced it.
//
// Run it once per body: `-airshow -body=1`, `-airshow -body=4`, and so on.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerAirShow.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerAirShow : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Place();
	void Report();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FLedgerAirProfile Air;
	FVector3d Anchor = FVector3d::UnitZ();
	int32 Home = INDEX_NONE;

	/// When the sun is on the horizon here, found from the ephemeris rather
	/// than by looking for a pretty frame.
	double SunsetSeconds = -1.0;
	double NoonSeconds = 0.0;

	int32 Step = 0;
	bool bAimed = false;
	bool bRunning = false;
	double Settle = 0.0;
};
