// A severe storm: dangerous to fly through, and a storm from orbit in the same
// place. T097.
//
// The deepest low in the next ten days that sits under a daylit sky is found,
// not chosen. Then: a frame from orbit over its centre and one over calm air
// at the same latitude; a frame from inside the cloud, with the flashes and
// thunder counted; and two forty-second flights at the same height and speed,
// one through the core and one through the calm air, each with what it did to
// the hull.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "LedgerWeather.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerStormWatch.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerStormWatch : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Finish();

	struct FRun
	{
		double Hull = 1.0;
		int32 Strikes = 0;
		FVector3d WindSum = FVector3d::ZeroVector;
		double WindSquares = 0.0;
		int32 Samples = 0;
		double CrossTrackMax = 0.0;
		double GustRms() const;
	};

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FLedgerAirProfile Air;
	int32 Home = INDEX_NONE;

	double StormSeconds = 0.0;
	FVector3d StormUp = FVector3d::UnitZ();
	FVector3d CalmUp = FVector3d::UnitZ();
	double StormDepthPascals = 0.0;
	double StormRadiusMetres = 0.0;
	FLedgerStorm AtCentre;
	FLedgerStorm AtCalm;

	FRun Runs[2];
	FVector3d Heading = FVector3d::UnitX();
	int32 FlashesInside = 0;
	int32 ThundersInside = 0;

	TArray<FString> Lines;
	int32 Step = 0;
	double Clock = 0.0;
	bool bStarted = false;
	bool bRunning = false;
};
