// Finds the steepest lit face on the planet and photographs it. T054.
//
// The acceptance is "a 60-degree face reads as rock with bedding, and there is
// debris at its foot that was not placed by hand". Finding the face is the
// fixture's job, because until T428 the planet did not have one -- the steepest
// ground anywhere was 57 degrees.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerCliffSite.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerCliffSite : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	bool FindFace();
	void Place();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	bool bRunning = false;
	bool bFound = false;
	bool bCaptured = false;
	int32 Shot = 0;
	double Settle = 0.0;

	/// The face, the way to look at it, and how steep it is.
	FVector3d Face = FVector3d::ZeroVector;
	FVector3d Downhill = FVector3d::ZeroVector;
	double SlopeDegrees = 0.0;
	double DropMetres = 0.0;
	double AltitudeMetres = 0.0;
};
