// A thousand queries against a thousand traces. T061.
//
// The acceptance is that the sampling API agrees with a physics trace to the
// millimetre, and there is only one way to establish that: trace, sample, and
// subtract. Both have to happen in a running world with collision cooked, so
// this is a fixture rather than an automation test.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerTerrainQueryCheck.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerTerrainQueryCheck : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	bool Compare();

	bool bRunning = false;
	double Waited = 0.0;
};
