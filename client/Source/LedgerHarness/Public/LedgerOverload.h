// Deliberate overload, and what gives first. T063.
//
// The acceptance is that under overload "the frame budget holds and the
// degradation is in detail rather than in holes". Both halves need a load that
// the streamer cannot possibly keep up with, which the scripted flight is not:
// it moves fast but continuously, so the LOD tree changes at the edges.
//
// This teleports. Every couple of seconds the ship appears somewhere else on
// the planet entirely, which invalidates the whole visible set at once -- the
// worst case the streamer will ever see, arriving on purpose.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerOverload.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerOverload : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Report();

	bool bRunning = false;
	int32 Jump = 0;
	bool bShot = false;
	double SinceJump = 0.0;
	double LastFrameAt = 0.0;

	/// Wall-clock frame times, and the worst the terrain reported each frame.
	TArray<double> FrameMs;
	TArray<int32> Holes;
	TArray<int32> Visible;
	TArray<double> DetailRefused;
};
