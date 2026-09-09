// The scripted flight: a camera on rails that flies the world and photographs
// it, so every commit can be compared against the last one by looking.
//
// **This is a test fixture, and it lives in a DeveloperTool module so that it
// is absent from a shipping build entirely.** It had been sharing a file with
// the code that spawns the world — a thousand lines of camera script and
// screenshot capture wrapped around two hundred lines of production. The split
// is not tidiness: the flight teleports the ship, disables its flight model and
// overrides its camera boom, and none of that belongs anywhere a player can
// reach.
//
// It holds no world state of its own. Everything it needs — the planet, the
// site, the sun, the town — it asks the world builder for at the moment it
// needs it, which is what keeps the dependency pointing one way.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerFlightHarness.generated.h"

class ALedgerPlanet;
class ALedgerSettlement;
class ALedgerShip;
class ULedgerWorldBuilder;

UCLASS()
class LEDGERHARNESS_API ULedgerFlightHarness : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/// The shot that is waiting for the world to stop streaming, and how long
	/// it has waited. A capture taken before the terrain settles is a
	/// photograph of the loading state, and two machines load at different
	/// speeds.
	const TCHAR* PendingCapture = nullptr;
	double SettleWaited = 0.0;
	FTimerHandle SettleTimer;

	void TakeWhenSettled();

	static constexpr double SettlePollSeconds = 0.1;

	/// Bounded, so a world that never settles still yields an image. Four
	/// seconds is far longer than a settled frame needs and short enough that
	/// eight of them do not move the phases apart.
	static constexpr double SettleLimitSeconds = 4.0;

	// ---- what the flight asks the world for -----------------------------
	//
	// Looked up rather than cached: subsystem initialisation order is not
	// guaranteed, and the first scripted step does not run for fourteen
	// seconds, by which time the world has certainly been built.
	ULedgerWorldBuilder* Builder() const;
	ALedgerPlanet* Planet() const;
	ALedgerSettlement* Town() const;
	ALedgerShip* GetShip() const;
	FVector3d Site() const;
	FVector3d Sun() const;

	// ---- the flight's own state -----------------------------------------

	/// Where the coast search landed, kept so the underwater shot does not run
	/// an 8192-sample sweep of the globe a second time for the same answer.
	FVector3d CoastSite = FVector3d::ZeroVector;
	FVector3d CoastSeaward = FVector3d::ZeroVector;

	/// Descent state. The ship is *flown* down rather than teleported: a
	/// teleport proves nothing about whether the transition holds together, and
	/// the whole claim being made is that there is no seam to hide.
	double DescentStartAltitude = 0.0;
	double DescentEndAltitude = 0.0;
	double DescentElapsed = 0.0;
	double DescentDuration = 46.0;

	double SweepElapsed = 0.0;
	int64 SweepStartBuilds = 0;
	int64 SweepStartHits = 0;

	FTimerHandle OrbitCaptureTimer;
	FTimerHandle DescendTimer;
	FTimerHandle DescentStepTimer;
	FTimerHandle EntryCaptureTimer;
	FTimerHandle SurfaceCaptureTimer;
	FTimerHandle TownCaptureTimer;
	FTimerHandle SweepTimer;
	FTimerHandle SweepStepTimer;
	FTimerHandle SweepEndTimer;
	FTimerHandle CoastCaptureTimer;
	FTimerHandle UnderwaterTimer;
	FTimerHandle AscentTimer;
	FTimerHandle SpaceCaptureTimer;
	FTimerHandle PerformanceTimer;

	// ---- the steps -------------------------------------------------------

	void Capture(const TCHAR* Name);
	void CaptureOrbit();
	void CaptureEntry();
	void CaptureSurface();
	void CaptureTown();

	void BeginDescent();
	void StepDescent();

	/// Points the ship up and opens the throttle. Everything after this runs
	/// through the ship's own flight model, so the climb demonstrates that
	/// gravity and thrust balance rather than being a second animation.
	void BeginAscent();

	/// Moves the ship to a vantage point with the sun behind the camera. The
	/// first town capture came back as silhouettes because the pad happens to
	/// sit on the shadowed side of its own buildings.
	void FrameTown();

	/// Turns the ship to look back down at the planet before the last capture.
	/// Climbing straight out leaves the camera pointed at empty sky, which
	/// proves the ascent worked and shows nothing at all.
	void FrameSpace();

	/// Flies to a coastline and looks out to sea. The landing site is chosen
	/// for relief and is therefore inland, so nothing else in the sequence can
	/// answer the only question that matters about the water: whether the
	/// waterline is a surface with a horizon or a change of colour.
	void FrameCoast();

	/// Drops the camera below the waterline at the same coast, so the crossing
	/// has a captured before and after rather than only a code path.
	void FrameUnderwater();

	/// Flies a straight line out from the town and back, three times over.
	/// The rest of the sequence is a one-way trip, so it never asks the terrain
	/// for ground it has already had — and retracing is the only case that
	/// measures the patch cache.
	void BeginRidgeSweep();
	void StepRidgeSweep();
	void EndRidgeSweep();

	/// Writes a depth transect running seaward from the coast to out/. A shelf
	/// break is a feature of the profile, not of any one view: from the surface
	/// it is a line where the colour changes and from orbit it is invisible.
	void DumpBathymetry();

	/// Names the phase the frame-time recorder attributes subsequent frames to.
	void MarkPhase(const TCHAR* Name);

	/// Writes out/performance.txt at the end of the run.
	void WritePerformanceReport();
};
