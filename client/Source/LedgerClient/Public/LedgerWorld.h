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
#include "LedgerBody.h"
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
class LEDGERCLIENT_API ULedgerWorldBuilder : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/// Only to keep the sky light where the viewer is. See KeepSkyWithViewer.
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void KeepSkyWithViewer();

	/// Kept so Tick can move it to the viewer every frame.
	UPROPERTY()
	TObjectPtr<class ASkyLight> Sky;

	bool bSkyMoved = false;

	/// Where the sky was last captured from, so it is re-captured when the
	/// viewer has actually gone somewhere.
	FVector LastSkyCapture = FVector(TNumericLimits<double>::Max());
	FVector3d LastSkyCaptureSun = FVector3d::ZeroVector;

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	ALedgerPlanet* GetPlanet() const { return Planet; }
	ALedgerSettlement* GetSettlement() const { return Settlement; }
	ALedgerAtmosphere* GetAtmosphere() const { return Atmosphere; }

	/// Direction from the planet's centre to the town. The reentry aims here.
	FVector3d GetSiteDirection() const { return SiteDirection; }

	/// Direction from the planet's centre toward the sun.
	FVector3d GetSunFacing() const { return SunFacing; }

	/// The bodies this world is one of, when it is, and which one is underfoot.
	const FLedgerSystem& GetSystem() const { return System; }
	double GetWhenSeconds() const { return WhenSeconds; }

	/// Moves the world's clock, and everything the clock decides.
	///
	/// **That includes the sun.** The first version set only the number, and
	/// the consequence took a while to find: the moon moved to the new time
	/// while the directional light stayed where it was at begin play, so a body
	/// the ephemeris called 99.7% lit was photographed as a thin crescent --
	/// lit correctly, for the wrong moment.
	///
	/// The terrain does not follow. It carries the season it was generated
	/// with, so this is a knob for hours and not for months.
	void SetWhenSeconds(double Seconds);
	int32 GetHomeBodyIndex() const;

	/// Tear the world down and build it again around a different body. T088.
	///
	/// **This is the first piece of M07 arriving early**, and it arrives
	/// because T088 asks for a moon to be flown to and landed on *in one
	/// session* -- which a world that can only be built during BeginPlay cannot
	/// do. The terrain, the sky and the settlement are all functions of which
	/// body this is, so all three go and are made again; the ship and the clock
	/// are not, because neither is.
	void SwitchToBody(int32 BodyIndex);

private:
	UPROPERTY()
	TObjectPtr<ALedgerPlanet> Planet;

	UPROPERTY()
	TObjectPtr<ALedgerAtmosphere> Atmosphere;

	UPROPERTY()
	TObjectPtr<ALedgerSettlement> Settlement;

	/// The bodies this world is one of, and when it is. Generated from a seed
	/// at BeginPlay; the sun's direction is a consequence of both.
	FLedgerSystem System;
	double WhenSeconds = 0.0;

	/// Direction from the planet's centre toward the sun. Half the planet is in
	/// darkness at any moment — a site chosen on relief alone lands on whichever
	/// half, and the first time it did, the shot came back black.
	///
	/// No longer a constant: T072 derives it from the orbit and the rotation at
	/// WhenSeconds, so `-when=` moves the sun and nothing else has to.
	FVector3d SunFacing = FVector3d::UnitZ();
	FVector3d SiteDirection = FVector3d::UnitZ();

	/// Picks somewhere worth landing: high relief, in daylight. Earth is mostly
	/// flat, so an arbitrary descent vector proves nothing about the terrain.
	void BuildWorldFor(UWorld& InWorld);

	/// The body a switch is on its way to, or INDEX_NONE.
	///
	/// **A world is torn down on one frame and built on the next.** Doing both
	/// inside one tick had the old world's render resources being released
	/// while the new world's were being created, and it ended in a null
	/// dereference on the render thread with a breadcrumb no more specific than
	/// "SceneRender". A frame between them costs nothing and removes the whole
	/// class of overlap.
	int32 PendingBody = INDEX_NONE;
	void ChooseSite();

	void PlaceRegionMarkers(UWorld& InWorld);

};
