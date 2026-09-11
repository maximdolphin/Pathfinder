// A steep entry and a shallow one, flown. T100.
//
// The automation test proves the arithmetic; this flies the ship through it:
// from 120 km at orbital speed, once at eight degrees down and once at one,
// through the same flight model, air and heat shield the player has, with the
// clock sped up so a ten-minute entry takes a little over one. Each run records
// the flux, the skin, the heat taken in and radiated, and the hull, and
// photographs the ship at the height of its glow.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerEntryBurn.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerEntryBurn : public UTickableWorldSubsystem
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
	double Damage[2] = { 0.0, 0.0 };
	double PeakKm[2] = { 0.0, 0.0 };
	int32 Step = 0;
	double Clock = 0.0;
	double Settle = 0.0;
	bool bStarted = false;
	bool bShot = false;
	bool bRunning = false;
};
