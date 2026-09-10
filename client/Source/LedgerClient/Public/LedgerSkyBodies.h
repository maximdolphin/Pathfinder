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

class UInstancedStaticMeshComponent;
class USceneComponent;
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

	/// **The star field, which T077 computed and nothing drew.**
	///
	/// One instanced mesh with an instance per naked-eye star, hung on a holder
	/// that carries the body's rotation. Rotating the holder rather than eight
	/// hundred instances is what makes the sky turn overnight for the cost of
	/// one transform.
	UPROPERTY()
	TObjectPtr<USceneComponent> StarHolder;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Stars;

	bool bStarsBuilt = false;

	/// How many stars the catalogue holds and how far out it reaches.
	///
	/// **Three thousand in four hundred parsecs gave thirty-seven naked-eye
	/// stars**, which over a whole sphere is one every three hundred square
	/// degrees -- so a twenty-degree frame contained none and the night looked
	/// as empty as it had before. The real sky has about six thousand.
	///
	/// The lever is the volume, not the count: brightness is the distance
	/// modulus, so pulling the shell in from four hundred parsecs to a hundred
	/// and fifty makes the same stars far brighter. Twenty thousand at that
	/// radius gave 706 naked-eye, which is four stars in a twenty-degree frame
	/// -- a sky, but a thin one. A hundred and fifty thousand gives a few
	/// thousand, which is the order the real one has.
	static constexpr int32 StarCount = 150000;
	static constexpr double StarRadiusParsecs = 150.0;

	void BuildStars(const FLedgerSystem& System, int32 Home);

	bool bBuilt = false;
};
