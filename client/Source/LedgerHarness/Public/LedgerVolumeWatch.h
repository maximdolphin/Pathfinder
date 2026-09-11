// The volumetric budget in the worst weather there is. T105.
//
// Inside the deepest storm in ten days, in its cloud and its rain: the GPU frame
// with the volumetrics off, then at each detail level held in turn -- the
// difference is what the volumetrics cost -- then with the budget choosing
// under a four-millisecond allowance, and again squeezed to half of what full
// detail costs, to show it gives up detail and keeps the frame.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerVolumeWatch.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerVolumeWatch : public UTickableWorldSubsystem
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

	FVector3d StormUp = FVector3d::UnitZ();
	double StormSeconds = 0.0;

	/// Median GPU ms per phase: off, levels 0 to 3, budgeted, squeezed.
	double Medians[7] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
	int32 Chosen[2] = { -1, -1 };
	TArray<double> Samples;
	TArray<FString> Lines;
	int32 Phase = 0;
	double Clock = 0.0;
	bool bRunning = false;
};
