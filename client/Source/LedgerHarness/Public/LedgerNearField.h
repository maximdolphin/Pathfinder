// Does the ground have anything on it at walking distance? T429.
//
// Two numbers decide that and neither can be got by looking at a screenshot:
// how coarse the drawn triangles are where the camera is standing, and how far
// the drawn surface departs from a flat plane over the fifty metres in front of
// it. A photograph of ground that is secretly a plane looks like ground,
// because the material is doing the work.
//
// So this parks on the ground, waits for the finest patches to arrive, and
// measures. It also samples the height function twice -- once with the grid it
// is actually drawn on and once at a spacing too coarse for the near-field
// band -- which is the before and after of T429 in a single run, with no
// rebuild in between.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerNearField.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerNearField : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Park();
	bool Measure();

	UPROPERTY()
	TObjectPtr<class ACameraActor> Camera;

	bool bRunning = false;
	bool bCaptured = false;
	bool bLookedDown = false;
	bool bLoggedGiveUp = false;

	/// The T430 pair under -pomprobe: -1 not started, 0 and 1 the two frames.
	int32 PomShot = -1;
	int32 PomFrames = 0;
	double Waited = 0.0;
};
