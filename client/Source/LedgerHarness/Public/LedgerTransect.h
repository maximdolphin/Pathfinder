// M02's collision gate, as a runnable measurement.
//
// Launch with `-transect` and this runs instead of the scripted flight: two
// hundred kilometres at 900 m/s and fifty metres up, with a downward trace on
// every frame, counting the ones that hit nothing.
//
// The failure it exists to catch cannot be seen. Streaming collision is cooked
// near the camera and ahead along the velocity vector, and when it fails to
// arrive a ship has nothing beneath it for a moment somewhere in the middle of
// a long fast run — and is past it before anybody finishes being surprised.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerTransect.generated.h"

class ALedgerPlanet;
class ALedgerShip;

UCLASS()
class LEDGERHARNESS_API ULedgerTransect : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Place(ALedgerPlanet& Planet, ALedgerShip& Ship,
		APlayerController& Controller, double Along) const;
	void Finish();

	bool bRunning = false;
	double Warmup = 0.0;
	/// Whether the streaming queue has gone quiet once, for -transectsettle.
	/// Latched: unlatched it re-entered in flight and measured nothing.
	bool bSettled = false;
	/// The most unfilled patches seen during the measured flight. The terrain's
	/// own worst is a planet-lifetime figure -- 1,689 at t=0, the world loading
	/// -- and M02's first two checks ask what this transect flew through.
	int32 WorstUnfilledInFlight = 0;
	/// How many measured frames had any hole at all, and where the worst was.
	/// A worst of 1,361 says nothing by itself: confined to the first second it
	/// is the world still arriving, spread over 200 km it is a streaming fault.
	int32 FramesWithHoles = 0;
	double WorstUnfilledAtKm = 0.0;
	/// The furthest a hole was seen, and how many hole-frames fell past the
	/// first kilometre: the difference between a world still loading and a
	/// streamer that cannot keep up at 900 m/s.
	double LastHoleKm = 0.0;
	int32 HoleFramesPastFirstKm = 0;
	/// What this fixture's own Tick cost on the frame just measured.
	///
	/// 84 of 127 frames over budget are "waits" -- game, render, GPU, RHI, swap
	/// and idle all low while the wall clock reads 30 to 50 ms -- and the
	/// counters are aligned, so the time is outside everything the engine
	/// reports. The harness traces the ground every frame and probes patch
	/// edges periodically, and nothing has ever measured that. A rig that
	/// inflates its own numbers is the first thing to rule out.
	double FixtureMs = 0.0;
	double Travelled = 0.0;

	int32 Frames = 0;
	int32 Misses = 0;
	/// The misses by cause (T068): nothing drawn under the ship, drawn ground
	/// that never asked for collision, or collision asked for and not cooked.
	int32 MissesNoGround = 0;
	int32 MissesNoCollision = 0;
	int32 MissesUncooked = 0;
	double MissPatchSizeSum = 0.0;
	/// Hits on which the query also found drawn ground: the check on the check.
	int32 HitsSampled = 0;
	/// How far the collision found lies from the exact surface, metres.
	double HitErrorSum = 0.0;
	double HitErrorWorst = 0.0;

	/// Consecutive misses, and the worst run of them. One isolated miss and a
	/// hundred in a row are the same number of misses and very different bugs.
	int32 CurrentMiss = 0;
	int32 LongestMiss = 0;

	// Wall-clock frame time over the run, for the budget the gate names.
	double LastWallSeconds = 0.0;
	/// The engine's counters for the frame before, so a spike can be checked
	/// against both and the classifier's alignment shown rather than assumed.
	double LastGameMs = 0.0;
	double LastRenderMs = 0.0;
	double LastGpuMs = 0.0;
	double LastRhiMs = 0.0;
	double WorstFrameMs = 0.0;
	double FrameMsSum = 0.0;
	int32 FramesOverBudget = 0;

	/// T068: what an over-budget frame was spent on. The longest of the game
	/// thread, render thread and GPU for the frame just finished, or none of
	/// them over budget, which is a wait; and what else happened in it.
	int32 OverGame = 0;
	int32 OverRender = 0;
	int32 OverGpu = 0;
	int32 OverRhi = 0;
	int32 OverWaiting = 0;
	int32 OverWithUpload = 0;
	int32 OverWithGC = 0;
	int32 OverWithShaders = 0;
	int32 GCsSeen = 0;
	int32 GCsAtLastFrame = 0;
	TArray<TPair<double, FString>> WorstFrames;
	/// T068 seams: the edge-gap probe every 20 km, its first lines each time.
	TArray<FString> SeamSamples;
};
