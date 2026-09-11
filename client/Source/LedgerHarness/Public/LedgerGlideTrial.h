// A winged ship, unpowered, in the air and out of it. T135.
//
// The courier is let go at 170 m/s with nothing on -- assist off, no thrust,
// no stick -- four kilometres above the ground by the town, and flown by its
// wings for a minute; then let go the same way at 150 km, where there is no
// air, for twenty seconds. In the air the wings hold it up and it glides; out
// of it, it falls as anything falls.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerGlideTrial.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerGlideTrial : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Finish();

	TArray<FString> Lines;
	FVector3d Up = FVector3d::UnitZ();
	FVector3d StartPosition = FVector3d::ZeroVector;
	double StartHeight = 0.0;
	double Clock = 0.0;
	double SinceLine = 0.0;
	double AirLost20 = -1.0;
	double AirSink20 = 0.0;
	double AirRatio = 0.0;
	double AirSpeedEnd = 0.0;
	double VacuumLost20 = -1.0;
	double VacuumSink20 = 0.0;
	double SinkAtRelease = 0.0;
	double Gravity = 0.0;
	double TrimSpeed = 0.0;
	double TrimGamma = 0.0;
	double TrimAlpha = 0.0;
	int32 Stage = 0;
	bool bRunning = false;
};
