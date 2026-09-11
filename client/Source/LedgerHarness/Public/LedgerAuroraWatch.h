// Aurora at high latitude in a solar event, and not otherwise. T103.
//
// The biggest event in sixty days is found, and a place under its oval on the
// night side; then the same place on a quiet night. Photographed from the
// ground looking towards the magnetic pole both times, and from orbit over the
// pole during the event, with what the model says overhead each time and at a
// low-latitude place during the same event.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerAurora.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerAuroraWatch.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerAuroraWatch : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Finish();
	FLedgerAurora Model(const FVector3d& Where, double When) const;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FLedgerAirProfile Air;
	int32 Home = INDEX_NONE;
	FVector3d Pole = FVector3d::UnitZ();
	FVector3d Site = FVector3d::UnitZ();
	FVector3d LowSite = FVector3d::UnitZ();
	double EventSeconds = 0.0;
	double QuietSeconds = -1.0;

	FLedgerAurora Seen[3];
	TArray<FString> Lines;
	int32 Step = 0;
	double Clock = 0.0;
	bool bRunning = false;
};
