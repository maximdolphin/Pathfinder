// Stand outside during an eclipse. T075.
//
// The ephemeris says when; this goes there and looks. One site, one camera, and
// a sequence through a predicted eclipse -- so the acceptance's two halves,
// "observable at the predicted time" and "the ground goes dark", are the same
// photograph.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerEclipseWatch.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerEclipseWatch : public UTickableWorldSubsystem
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
	FVector3d Anchor = FVector3d::UnitZ();

	/// When the ephemeris says the eclipse peaks, and how deep.
	double PeakSeconds = -1.0;
	double PeakCoverage = 0.0;
	int32 What = INDEX_NONE;

	TArray<double> OffsetsMinutes;
	TArray<double> Coverage;

	int32 Step = 0;
	bool bAimed = false;
	bool bRunning = false;
	double Settle = 0.0;
};
