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
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/GameModeBase.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerWorld.generated.h"

class ALedgerPlanet;

/// Free flight for surveying the planet. Design §6.9 keeps ships out of scope
/// for now, so this is a camera with a throttle, not a flight model.
UCLASS()
class LEDGERCLIENT_API ALedgerOrbiterPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	ALedgerOrbiterPawn();

	virtual void Tick(float DeltaSeconds) override;

	/// Speed scales with altitude above the surface. Crossing 60 km at ground
	/// speed is unusable, and descending at orbital speed is uncontrollable —
	/// one control that works at both ends beats two that each work at one.
	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float MinSpeed = 2000.0f;

	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float MaxSpeed = 900000.0f;

	/// Fraction of altitude covered per second at full throttle.
	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float AltitudeSpeedFactor = 0.9f;
};

UCLASS()
class LEDGERCLIENT_API ALedgerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALedgerGameMode();

	/// The map has no PlayerStart, so without this the pawn spawns at the world
	/// origin — which is the centre of the planet. Start in high orbit instead,
	/// looking down, because the first thing worth seeing is the curve.
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
};

/// Spawns the scene: planet, lighting, and a marker per simulated region.
UCLASS()
class LEDGERCLIENT_API ULedgerWorldBuilder : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	ALedgerPlanet* GetPlanet() const { return Planet; }

private:
	UPROPERTY()
	TObjectPtr<ALedgerPlanet> Planet;

	/// One marker per region in the sim snapshot, placed on the surface. The
	/// point is not the geometry — it is that the world the player flies over
	/// is the same world the ledger is about.
	void PlaceRegionMarkers(UWorld& InWorld);

	/// Fires once, a few seconds in, to capture what the terrain actually looks
	/// like. Verifying a renderer by reading its log is not verifying it.
	FTimerHandle CaptureTimer;
	FTimerHandle DescendTimer;
	FTimerHandle SurfaceCaptureTimer;

	void CaptureAndReport();

	/// Drop the camera to low altitude. This is the moment the terrain system is
	/// actually being judged on: the LOD has to refine several levels and the
	/// collision cook has to keep up, which is precisely where §6.8 says every
	/// implementation of this hitches.
	void DescendToSurface();
	void CaptureSurface();
};
