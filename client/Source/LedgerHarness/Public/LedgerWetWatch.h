// The rain stops, and the ground dries. T096.
//
// The acceptance is a sequence -- wet after rain, puddles in the hollows, both
// gone within the hour -- so this is a time series at one spot: a frame while
// it is still raining, one as it stops, and one every twenty minutes after,
// each with the water the model says is on the ground at that moment.
//
// When the rain stops is found, not chosen: the first moment in thirty days
// that a shower ends over the site in daylight with the ground soaked and two
// dry hours to follow, so every frame is lit and nothing re-wets it mid-run.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "LedgerPrecipitation.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerWetWatch.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerWetWatch : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Place();
	void Finish();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FLedgerAirProfile Air;
	FVector3d Anchor = FVector3d::UnitZ();
	int32 Home = INDEX_NONE;

	double Latitude = 0.0;
	double Longitude = 0.0;
	double SurfaceKelvin = 0.0;

	/// When the rain stops over the site.
	double StopSeconds = 0.0;

	/// What the view published at each frame, for the verdict.
	TArray<FLedgerSurfaceWater> Seen;
	TArray<FString> Lines;

	int32 Step = 0;
	bool bAimed = false;
	bool bRunning = false;
	double Settle = 0.0;
};
