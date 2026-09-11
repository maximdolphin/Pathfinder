// Clouds, fog and rain under one budget. T105.
//
// **Volumetrics are the easiest way to lose a frame, so they are the first thing
// to give when one is being lost.** A ladder of detail levels -- how many
// samples the clouds take, how coarse the fog's froxels are, how many drops
// are drawn -- walked down when the GPU frame runs over its target and back up
// when there is room. The frame rate is what is held; the detail is what moves.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerVolumeBudget.generated.h"

UCLASS()
class LEDGERCLIENT_API ULedgerVolumeBudget : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	static constexpr int32 LevelCount = 4;

	/// The detail level now, 0 full to LevelCount - 1 the least.
	int32 Level() const { return CurrentLevel; }

	/// Hold a level rather than choose one: a fixture measuring each in turn.
	/// Minus one lets the budget choose again.
	void HoldLevel(int32 InLevel) { Held = InLevel; }

	/// The GPU frame time the budget steers by, milliseconds, and its target.
	double SmoothedGpuMs() const { return Smoothed; }
	double TargetGpuMs() const { return Target; }
	void SetTargetGpuMs(double Milliseconds) { Target = Milliseconds; }

private:
	void Apply(int32 InLevel);

	int32 CurrentLevel = -1;
	int32 Held = -1;
	double Smoothed = 0.0;
	double Target = 16.7;
	double SinceChange = 0.0;
	bool bTargetRead = false;
};
