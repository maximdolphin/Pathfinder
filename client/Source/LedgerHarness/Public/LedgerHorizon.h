// A mountain range eighty kilometres away. T059.
//
// The acceptance is that it "has a silhouette and reads as terrain rather than
// as a gradient", and half of that is a photograph and half is a number. The
// number is the one worth automating: how much of the frame's horizon line is
// terrain standing above the sphere's own curve, and how ragged that line is.
// A gradient has a smooth horizon; terrain has a jagged one.
//
// This was blocked until T428, and the arithmetic is why. At 80 km the horizon
// drop is about 500 m, so on the old planet -- whose highest land was 1,372 m --
// the tallest thing anywhere would have been a bump on the curve. It is 2,462 m
// now, which stands about 1,960 m proud at that distance: roughly 30 px of
// silhouette on a 1920-wide frame at 90 degrees. Small, and no longer nothing.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerHorizon.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerHorizon : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	bool FindRange();
	void Place();
	void Report();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	/// The highest land found, where the camera stands to look at it, and how
	/// high that peak is.
	FVector3d Peak = FVector3d::ZeroVector;
	FVector3d Viewpoint = FVector3d::ZeroVector;
	double PeakMetres = 0.0;

	bool bRunning = false;
	bool bFound = false;
	bool bCaptured = false;
	double Settle = 0.0;
};
