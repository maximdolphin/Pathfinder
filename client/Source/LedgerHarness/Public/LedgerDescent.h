// A 300 m/s descent, counted for holes every frame. T027.
//
// The acceptance is "a 300 m/s descent shows no unfilled patches at any
// point", and "at any point" is the part a photograph cannot answer: a hole is
// a frame or two of sky through the ground, somewhere on a screen that is
// mostly elsewhere. So this flies the descent straight down over the site --
// the ship moving and reporting its velocity, so the streamer's lead along it
// is exercised -- and reads the planet's own count of unfilled nodes on every
// frame of the way.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerDescent.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerDescent : public UTickableWorldSubsystem
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

	FVector3d Anchor = FVector3d::UnitZ();
	double AltitudeMetres = 0.0;
	double Waited = 0.0;
	double Held = 0.0;
	bool bDescending = false;
	bool bRunning = false;

	int32 Frames = 0;
	int32 FramesWithHoles = 0;
	int32 WorstHoles = 0;
	double WorstHolesAtMetres = 0.0;
	double DescentSeconds = 0.0;
};
