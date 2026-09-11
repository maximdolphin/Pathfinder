// Fog that knows where the ground is. T091.
//
// **A grid of local fog volumes, filled to a level, and that is the whole
// idea.** Exponential height fog measures density against world Z on an
// infinite plane, which on a sphere means it is right at one point and fills
// space everywhere else. This does not measure anything against a plane: it
// samples the terrain, finds the lowest ground nearby, and puts fog in every
// cell whose ground is below a level a hundred metres above that.
//
// Filling a valley and leaving the ridge clear is then not a feature. It is
// what a level surface does to a landscape.
//
// And it is invisible from orbit for the same reason a real fog bank is: it is
// a hundred metres of a six-thousand-kilometre body, drawn by components that
// have an edge.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LedgerFog.h"
#include "LedgerGroundFog.generated.h"

class ALedgerPlanet;
class ALedgerAtmosphere;
class UStaticMeshComponent;

UCLASS()
class LEDGERCLIENT_API ALedgerGroundFog : public AActor
{
	GENERATED_BODY()

public:
	ALedgerGroundFog();

	/// Lay fog over the low ground around a place.
	///
	/// Returns how many cells were filled, which is the number worth logging:
	/// on flat ground it is nearly all of them and in mountains it is a few.
	int32 Configure(
		ALedgerPlanet* Planet, const FVector3d& AnchorDirection,
		const FLedgerFog& Fog);

	/// The level the fog is filled to, metres above the body's datum. Reported
	/// so a fixture can put a camera above and below it.
	double TopMetres() const { return FilledToMetres; }
	double FloorMetres() const { return ValleyFloorMetres; }
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Volumes;

	double FilledToMetres = 0.0;
	double ValleyFloorMetres = 0.0;
	/// Volumetric fog is on only while the camera is near the pool: the height
	/// fog it needs turns the view from orbit into a white disc under an orange
	/// sky, and a pool seen from orbit is not something anyone can see anyway.
	TWeakObjectPtr<ALedgerAtmosphere> Atmosphere;
	FVector PoolCentre = FVector::ZeroVector;
	bool bVolumetricOn = false;
};
