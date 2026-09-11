// A deliberate stall, the spin it departs into, and the recovery. T136.
//
// The courier is let go at 130 m/s five kilometres above the ground by the
// town with its thrusters off -- the wings and the surfaces are all it has.
// Full aft stick and a boot of rudder stall it; the fixture watches it depart
// (stalled and turning faster than 30 degrees a second), holds the inputs a
// few seconds into the spin, then flies the standard recovery -- stick
// forward, rudder against the turn, ailerons neutral -- and a gentle pull
// out of the dive, writing down every second of it.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerStallTrial.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerStallTrial : public UTickableWorldSubsystem
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
	double StartHeight = 0.0;
	double Clock = 0.0;
	double PhaseClock = 0.0;
	double SinceLine = 0.0;
	double DepartedAt = -1.0;
	double RecoveredAt = -1.0;
	double LevelAt = -1.0;
	double WorstRate = 0.0;
	double LostToRecovery = 0.0;
	int32 Stage = 0;
	bool bRunning = false;
};
