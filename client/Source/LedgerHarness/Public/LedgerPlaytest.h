// The ship flown the way a player flies it, offscreen. M5P.
//
// -playtest puts the ship over the town the way a plain launch does, then drives the
// same six axes the key bindings feed through a fixed script -- off the pad,
// out over the town and back, down to a hover -- and records what a player
// would notice: whether the ship stays in one piece, how the frames hold, and
// a capture on the legs worth looking at. Then twenty seeded sequences of
// random stick, about ten minutes in all. Run it with -RenderOffScreen; it
// writes out/playtest.txt and quits.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerPlaytest.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerPlaytest : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Finish(const FString& Why);

	TArray<FString> Lines;
	FString Divergence;
	FQuat LastAttitude = FQuat::Identity;
	double Clock = 0.0;
	double LegClock = 0.0;
	double SinceSample = 0.0;
	double LastSpeed = 0.0;
	double LastHeight = 0.0;
	double PulledSeconds = 0.0;

	// The random sequences after the script (T439): the current one's seed and
	// name, and the stick it holds until SegmentLeft runs out.
	FRandomStream Stream;
	FString RandomName;
	float RandomAxes[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	double SegmentLeft = 0.0;
	/// How many; -playtestrandom=N, 0 for the script alone.
	int32 Randoms = 0;
	/// Ground met at speed during them: the pilot's, counted rather than fatal.
	/// Each hitch over 250 ms after the first minute, and what it landed on.
	FString HitchNote;
	bool bCaptureAsked = false;
	int32 CaptureStalls = 0;
	int32 GroundHits = 0;
	double WorstHit = 0.0;
	double LastHit = -100.0;
	double WorstSpin = 0.0;
	double WorstSpeed = 0.0;
	double WorstFrameMs = 0.0;
	double LowestHeight = TNumericLimits<double>::Max();
	double LowestHull = 1.0;
	int32 Leg = -1;
	int32 Frames = 0;
	int32 FramesOver = 0;
	int32 FramesOver33 = 0;
	int32 Hitches = 0;
	bool bRunning = false;
	bool bPlaced = false;
	bool bCaptured = false;
	bool bSkipSpin = false;
};
