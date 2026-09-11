// Across a mountain range in a gale. T098.
//
// The site is found, not chosen: the most rugged ground under the strongest
// wind in the next five days. The ship crosses it downwind at a fixed height
// three times -- in calm air with an autopilot, in the wind hands off, and in
// the wind with the autopilot -- and each run says how far the air moved it and
// how hard the autopilot had to work. Whether the push came from the wind field
// is measured too: the hands-off ship's vertical speed against the field's
// vertical wind where it was.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerRidgeFlight.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerRidgeFlight : public UTickableWorldSubsystem
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
		double WorstHeightMetres = 0.0;
		double WorstCrossMetres = 0.0;
		double LiftSquares = 0.0;
		double StrafeSquares = 0.0;
		int32 Samples = 0;
		double UpdraughtMax = 0.0;
		double DowndraughtMax = 0.0;
		// For the correlation between the field and the ship.
		double SumW = 0.0, SumV = 0.0, SumWW = 0.0, SumVV = 0.0, SumWV = 0.0;
		bool bHitGround = false;
		double Seconds = 0.0;
		double Correlation() const;
	};

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	int32 Home = INDEX_NONE;
	double WhenSeconds = 0.0;
	FVector3d SiteUp = FVector3d::UnitZ();
	FVector3d StartUp = FVector3d::UnitZ();
	FVector3d Downwind = FVector3d::UnitX();
	double TrackMetres = 0.0;
	double CruiseMetres = 0.0;
	double ReliefMetres = 0.0;
	double WindMetresPerSecond = 0.0;

	FRun Runs[3];
	TArray<FString> Lines;
	int32 Step = 0;
	double Clock = 0.0;
	bool bStarted = false;
	bool bRunning = false;
	bool bShot = false;
};
