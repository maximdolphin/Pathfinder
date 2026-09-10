// One place, one camera, one day. T072.
//
// The unit tests prove noon is when the sun is highest. This is the other half
// of the same claim, in the world: the same ground photographed at eight moments
// across one rotation, with nothing changing between them except the time.
//
// **Everything else is held still on purpose.** The site is chosen once and
// reused; the camera does not move; the terrain is the same patches. A sweep
// that let the site follow the light would produce eight different landscapes
// and prove nothing about either.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerDaySweep.generated.h"

class ACameraActor;
class ADirectionalLight;

UCLASS()
class LEDGERHARNESS_API ULedgerDaySweep : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Place();

	/// Points the world's directional light where the ephemeris says the star
	/// is at a time, and returns the solar altitude there in degrees.
	double AimSunAt(double SecondsFromEpoch);

	void Report();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<ADirectionalLight> Sun;

	FLedgerSystem System;

	/// Where this is happening, as a unit direction in the body frame. Fixed
	/// for the whole sweep.
	FVector3d Anchor = FVector3d::UnitZ();

	/// Local noon at the anchor, which is where the sweep is centred so that
	/// the middle of the sequence is the middle of the day.
	double NoonSeconds = 0.0;
	double DaySeconds = 0.0;

	/// What each step measured, for the report.
	TArray<double> AltitudeDegrees;

	/// The ground under the camera, as the terrain query answers it and as the
	/// height field computes it. They should be the same number.
	double QueriedGroundMetres = 0.0;
	double FieldGroundMetres = 0.0;

	int32 Step = 0;
	bool bMeasured = false;
	bool bRunning = false;
	bool bPlaced = false;
	double Settle = 0.0;
};
