// Playable proof for M04: fly into a storm front. T107.
//
// Rule 6: the gate, run and recorded. The deepest daylit storm in ten days is
// found, and the edge of its severe core along a line into its centre; the ship
// is started fifteen kilometres outside that edge, below the cloud base, and
// flown in through its own flight model. Every half second it records what the
// pilot would notice -- how far they can see through the rain, how hard the air
// is shoving the ship, how loud it is -- and a frame every ten; then it looks
// down from orbit at the same place.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerStormFront.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerStormFront : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Finish();

	struct FSample
	{
		double Seconds = 0.0;
		double FromEdgeMetres = 0.0;
		double Severity = 0.0;
		double VisibilityMetres = 0.0;
		double GustMetresPerSecond = 0.0;
		double Loudness = 0.0;
		int32 Thunder = 0;
	};

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FLedgerAirProfile Air;
	int32 Home = INDEX_NONE;
	double StormSeconds = 0.0;
	FVector3d StormUp = FVector3d::UnitZ();
	FVector3d Inward = FVector3d::UnitX();
	FVector3d EdgeUp = FVector3d::UnitZ();
	FVector3d StartUp = FVector3d::UnitZ();

	TArray<FSample> Track;
	TArray<FString> Lines;
	FVector3d WindMean = FVector3d::ZeroVector;
	double Clock = 0.0;
	double SinceSample = 0.0;
	double SinceFrame = 0.0;
	int32 Frame = 0;
	int32 Stage = 0;
	bool bRunning = false;
};
