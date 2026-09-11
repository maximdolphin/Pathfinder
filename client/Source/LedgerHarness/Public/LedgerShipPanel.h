// The ship panel, photographed: T119, T122 and T128 on screen.
//
// The ship (-ship=, the courier by default) sits on the ground by the town
// with its panel up. The fixture photographs the panel with the shield down
// and then up, with an oversized plant offered for the reactor slot, and --
// when the ship has a hold -- with the hold filled until volume or mass
// binds; and writes down the numbers the panel showed.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerShipPanel.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerShipPanel : public UTickableWorldSubsystem
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
	FString ShipName;
	FString HoldBound;
	int32 Stage = 0;
	int32 Crates = 0;
	double Clock = 0.0;
	double StageClock = 0.0;
	double HeadroomDown = 0.0;
	double HeadroomUp = 0.0;
	bool bPreviewShown = false;
	bool bFitsAndCosts = false;
	bool bStowed = false;
	bool bHasHold = false;
	bool bRunning = false;
};
