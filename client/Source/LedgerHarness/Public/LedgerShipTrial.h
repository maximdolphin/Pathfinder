// A ship from a data file, flown. T108.
//
// Whichever ship `-ship=` named is held at three kilometres, then given a
// hundredth of its main thrust for three seconds through the flight model; the
// speed it gains is set against what its file says it should, and its size and
// mass are read back from the actor that was built from the file.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerShipTrial.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerShipTrial : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	FVector3d Up = FVector3d::UnitZ();
	FVector3d StartVelocity = FVector3d::ZeroVector;
	FVector3d StartForward = FVector3d::UnitX();
	double Gained = 0.0;
	double Expected = 0.0;
	double Burned = 0.0;
	double Clock = 0.0;
	int32 Stage = 0;
	bool bRunning = false;
};
