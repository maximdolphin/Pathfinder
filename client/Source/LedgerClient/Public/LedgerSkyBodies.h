// The other bodies, actually in the sky. T074.
//
// **Nothing here draws a phase.** Each body is a real sphere, placed in the real
// direction, lit by the same directional light as the ground -- so the crescent
// is the one the geometry makes, and there is no second implementation of
// "which part is lit" to disagree with `LedgerSky::IlluminatedFraction`. The
// only thing this fakes is distance.
//
// It has to fake distance. The moon is 4.3e8 m away and 1.7e6 m across; putting
// it there puts it past every sensible depth range. So it sits on a fixed sky
// shell and is scaled until it subtends the angle it really subtends, which
// leaves direction, apparent size and phase all correct and only parallax
// wrong -- and parallax against a sphere with no surface detail is not visible.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerSkyBodies.generated.h"

class UStaticMeshComponent;

UCLASS()
class LEDGERCLIENT_API ULedgerSkyBodies : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/// Where the sphere standing for a body actually sits in the world.
	///
	/// A fixture that photographs a moon rising needs to be able to say that
	/// the thing in the frame is where the ephemeris put it, and nothing else
	/// in the client can answer that -- every other check compares one
	/// calculation with another.
	bool WorldPositionOf(int32 BodyIndex, FVector& Out) const;

private:
	UPROPERTY()
	TObjectPtr<AActor> Holder;

	/// One sphere per body, in the same order as Bodies below.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Spheres;

	/// Which system body each sphere stands for.
	TArray<int32> Bodies;

	bool bBuilt = false;
};
