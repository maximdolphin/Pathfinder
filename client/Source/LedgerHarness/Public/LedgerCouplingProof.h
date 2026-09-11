// Playable proof for M05: shoot out a power coupling. T131.
//
// Rule 6: the gate, run and recorded. The courier sits on the ground by the
// town, shields up, systems running. A shot destroys its aft power coupling;
// then the clock runs twenty times fast while the fixture writes down, every
// second, what its thrusters can give, how hot its reactor is, what has power,
// what it sounds like and what its hull shows. When the loads have shed or
// enough time has passed, a workshop replaces the coupling and it watches the
// ship come back.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerCouplingProof.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerCouplingProof : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Finish();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	TArray<FString> Lines;
	TArray<FString> LostOrder;
	TSet<FString> Unpowered;
	int32 Stage = 0;
	int32 Frame = 0;
	double Clock = 0.0;
	double GameClock = 0.0;
	double SinceSample = 0.0;
	double HitAt = 0.0;
	double RepairedAt = 0.0;
	double ReactorAtHit = 0.0;
	double ReactorPeak = 0.0;
	double ReactorAtEnd = 0.0;
	double MainAfterHit = 1.0;
	double SideAfterHit = 1.0;
	double MainAtEnd = 0.0;
	double SideAtEnd = 0.0;
	int32 PoweredAtStart = 0;
	int32 PoweredAtEnd = 0;
	bool bSawFire = false;
	bool bTookAfterHit = false;
	bool bHeardAlarm = false;
	bool bRunning = false;
};
