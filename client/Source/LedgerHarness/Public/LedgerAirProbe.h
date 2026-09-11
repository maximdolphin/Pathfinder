// A column of air over the town, and the town and the renderer reading the same
// number. T099.
//
// Stands the camera at the settlement's wind vane, so the renderer's column is
// the settlement's, then walks five kilometres up it: every rung's temperature
// against the lapse-rate prediction from the bottom one, the pressure against
// the scale height, the height where it reaches freezing against the freezing
// level the rain-or-snow decision uses, and what the settlement read against
// what the renderer read.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerAirProbe.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerAirProbe : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Report();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	double Clock = 0.0;
	bool bRunning = false;
};
