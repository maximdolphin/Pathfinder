// A town. Design §3 scale for MVP is "one star system, three to five
// planets/moons" — this is the ground truth underneath one dot on that map.
//
// Everything here is placed on a *sphere*, which is the only thing that makes it
// harder than scattering props on a plane: there is no global up, so every
// building has its own, and a street laid out in a straight line in the tangent
// plane bends over the horizon. Positions are therefore built in a local tangent
// basis and projected back onto the surface height function — the same function
// the terrain mesh comes from, so nothing floats and nothing sinks.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LedgerSettlement.generated.h"

class ALedgerPlanet;
class UProceduralMeshComponent;

UCLASS()
class LEDGERCLIENT_API ALedgerSettlement : public AActor
{
	GENERATED_BODY()

public:
	ALedgerSettlement();

	/// Builds the town around a direction on the planet. Returns the world
	/// location of the landing pad, which is where the ship wants to arrive.
	FVector Build(ALedgerPlanet* InPlanet, const FVector3d& SiteDirection, uint32 InSeed);

	UPROPERTY(EditAnywhere, Category = "Ledger|Town")
	int32 BuildingCount = 34;

	UPROPERTY(EditAnywhere, Category = "Ledger|Town")
	int32 TreeCount = 900;

	/// Radius of the built-up area, in centimetres. 400 m across.
	UPROPERTY(EditAnywhere, Category = "Ledger|Town")
	double TownRadius = 20000.0;

	/// Trees scatter out to here.
	UPROPERTY(EditAnywhere, Category = "Ledger|Town")
	double TreeRadius = 90000.0;

	FVector GetPadLocation() const { return PadLocation; }

private:
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Structures;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Foliage;

	UPROPERTY()
	TObjectPtr<ALedgerPlanet> Planet;

	FVector PadLocation = FVector::ZeroVector;

	/// Surface point and local frame for a tangent-plane offset from the site.
	/// This is the one piece of maths the whole file depends on: a town laid out
	/// in flat coordinates has to be bent back onto the sphere, or it drifts
	/// off the ground within a few hundred metres.
	void SurfaceFrame(
		const FVector3d& SiteDirection,
		const FVector3d& East,
		const FVector3d& North,
		double OffsetEast,
		double OffsetNorth,
		FVector& OutLocation,
		FVector& OutUp) const;
};
