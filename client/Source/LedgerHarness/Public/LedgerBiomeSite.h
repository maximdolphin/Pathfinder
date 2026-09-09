// Finds where three biomes meet and photographs it. T053.
//
// The acceptance is "three biomes meet on one slope with no visible blend band,
// and the boundary explained by the climate field". The scripted flight cannot
// show that: it lands where it lands, and on this planet that is desert -- the
// census says half the land is. A capture of desert is not evidence about a
// boundary.
//
// So this searches for the boundary rather than hoping to fly over one, and
// prints the climate that produced it beside the picture. A photograph of three
// grounds meeting proves nothing on its own; a photograph plus the temperature,
// the moisture and the three weights at that point is the acceptance.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerBiomeSite.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerBiomeSite : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	/// Searches the planet for the most even three-way biome meeting point and
	/// writes the report. Returns false if there is nothing to look at.
	bool FindSite();

	void Place();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	bool bRunning = false;
	bool bFound = false;
	bool bCaptured = false;
	int32 Index = 0;
	double Settle = 0.0;

	/// The site, as a unit vector on the sphere, and the direction the ground
	/// changes fastest in.
	FVector3d Site = FVector3d::ZeroVector;
	FVector3d Across = FVector3d::ZeroVector;

	/// The sharpest boundary on the planet, and the way across it.
	///
	/// A separate site because the two questions are separate. The three-way
	/// site answers "do three biomes blend"; it happens to sit in the middle of
	/// a gentle gradient, so it cannot answer "is there a visible band". This
	/// one can, because it is the worst case the planet has.
	FVector3d Edge = FVector3d::ZeroVector;
	FVector3d EdgeAcross = FVector3d::ZeroVector;
};
