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

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	ALedgerPlanet* GetPlanet() const { return Planet; }
	ALedgerSettlement* GetSettlement() const { return Settlement; }

	/// Direction from the planet's centre to the town. The reentry aims here.
	FVector3d GetSiteDirection() const { return SiteDirection; }

	/// Direction from the planet's centre toward the sun.
	FVector3d GetSunFacing() const { return SunFacing; }

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

	/// Picks somewhere worth landing: high relief, in daylight. Earth is mostly
	/// flat, so an arbitrary descent vector proves nothing about the terrain.
	void ChooseSite();

	void PlaceRegionMarkers(UWorld& InWorld);

};
