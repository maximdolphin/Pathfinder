// The world builds itself in C++. Design §9: no Blueprint logic — and here,
// no level content either.
//
// There is no .umap with a planet and a light placed in it. A world subsystem
// spawns everything on begin play, which means the whole scene is reviewable as
// source, diffable in a pull request, and reproducible from a seed. That is the
// same argument §5.1 makes for keeping the sim out of the engine, applied one
// level down.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerWorld.generated.h"

class ALedgerAtmosphere;
class ALedgerPlanet;
class ALedgerSettlement;
class ALedgerShip;

UCLASS()
class LEDGERCLIENT_API ALedgerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALedgerGameMode();

	/// The map has no PlayerStart, so without this the pawn spawns at the world
	/// origin — which is the centre of the planet. Start in high orbit instead.
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
};

/// Spawns the scene: planet, atmosphere, lighting, a town, and the ship.
UCLASS()
class LEDGERCLIENT_API ULedgerWorldBuilder : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	ALedgerPlanet* GetPlanet() const { return Planet; }
	ALedgerSettlement* GetSettlement() const { return Settlement; }

	/// Direction from the planet's centre to the town. The reentry aims here.
	FVector3d GetSiteDirection() const { return SiteDirection; }

private:
	UPROPERTY()
	TObjectPtr<ALedgerPlanet> Planet;

	UPROPERTY()
	TObjectPtr<ALedgerAtmosphere> Atmosphere;

	UPROPERTY()
	TObjectPtr<ALedgerSettlement> Settlement;

	/// Direction from the planet's centre toward the sun. Half the planet is in
	/// darkness at any moment — a site chosen on relief alone lands on whichever
	/// half, and the first time it did, the shot came back black.
	FVector3d SunFacing = FVector3d::UnitZ();
	FVector3d SiteDirection = FVector3d::UnitZ();

	/// Where the coast search landed, kept so the underwater shot does not run
	/// an 8192-sample sweep of the globe a second time for the same answer.
	FVector3d CoastSite = FVector3d::ZeroVector;
	FVector3d CoastSeaward = FVector3d::ZeroVector;

	FTimerHandle OrbitCaptureTimer;
	FTimerHandle DescendTimer;
	FTimerHandle DescentStepTimer;
	FTimerHandle EntryCaptureTimer;
	FTimerHandle SurfaceCaptureTimer;
	FTimerHandle TownCaptureTimer;
	FTimerHandle AscentTimer;
	FTimerHandle ClimbCaptureTimer;
	FTimerHandle SweepTimer;
	FTimerHandle SweepStepTimer;
	FTimerHandle SweepEndTimer;
	double SweepElapsed = 0.0;
	int64 SweepStartBuilds = 0;
	int64 SweepStartHits = 0;

	FTimerHandle UnderwaterTimer;
	FTimerHandle SpaceCaptureTimer;
	FTimerHandle CoastCaptureTimer;

	/// Descent state. The ship is *flown* down rather than teleported: a teleport
	/// proves nothing about whether the transition holds together, and the whole
	/// claim being made is that there is no seam to hide.
	double DescentStartAltitude = 0.0;
	double DescentEndAltitude = 0.0;
	double DescentElapsed = 0.0;

	UPROPERTY(EditAnywhere, Category = "Ledger|Descent")
	double DescentDuration = 46.0;

	/// Picks somewhere worth landing: high relief, in daylight. Earth is mostly
	/// flat, so an arbitrary descent vector proves nothing about the terrain.
	void ChooseSite();

	void PlaceRegionMarkers(UWorld& InWorld);

	void Capture(const TCHAR* Name);
	void CaptureOrbit();
	void CaptureEntry();
	void CaptureSurface();
	void CaptureTown();

	void BeginDescent();
	void StepDescent();

	/// Points the ship up and opens the throttle. Everything after this runs
	/// through the ship's own flight model, so the climb is a demonstration that
	/// gravity and thrust actually balance rather than a second animation.
	void BeginAscent();

	/// Moves the ship to a vantage point with the sun behind the camera. The
	/// first town capture came back as silhouettes because the pad happens to
	/// sit on the shadowed side of its own buildings.
	void FrameTown();

	/// Turns the ship to look back down at the planet before the last capture.
	/// Climbing straight out leaves the camera pointed at empty sky, which
	/// proves the ascent worked and shows nothing at all.
	void FrameSpace();

	/// Flies to a coastline and looks out to sea.
	///
	/// The landing site is chosen for relief and is therefore inland, so none of
	/// the other captures can answer the only question that matters about the
	/// water: whether the waterline is a surface with a horizon or a change of
	/// colour on the terrain.
	void FrameCoast();

	/// Drops the camera below the waterline at the same coast, so the crossing
	/// has a captured before and after rather than only a code path.
	void FrameUnderwater();

	/// Flies a straight line out from the town and back, three times over.
	///
	/// The rest of the sequence is a one-way trip: it descends, crosses the
	/// site once and climbs out, so it never asks the terrain for ground it has
	/// already had. Retracing is the case the patch cache exists for and the
	/// only case that measures it.
	void BeginRidgeSweep();
	void StepRidgeSweep();
	void EndRidgeSweep();

	/// Writes a depth transect running seaward from the coast to out/.
	///
	/// A shelf break is a feature of the *profile*, not of any one view: from
	/// the surface it is a line where the colour changes, and from orbit it is
	/// invisible. The only honest way to see whether one is there is to sample
	/// the height function along a line and look at the numbers.
	void DumpBathymetry();

	ALedgerShip* GetShip() const;
};
