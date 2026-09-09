// Flatten a pad, reload it, and see what it cost. T062.
//
// All three clauses of the acceptance in one run, because they are only
// interesting together: a delta that flattens but does not persist is a
// screenshot, one that persists but costs everywhere is a tax on the whole
// planet, and one that is free because it does nothing is neither.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerPadCheck.generated.h"

class ALedgerPlanet;
struct FLedgerTerrainSample;

UCLASS()
class LEDGERHARNESS_API ULedgerPadCheck : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	bool Report();

	/// Peak-to-trough over the pad, from the sampling API.
	double Roughness(const ALedgerPlanet& Planet, const FVector3d& Site) const;

	/// Visits the sampling API over the flat part of the pad.
	void Sweep(const ALedgerPlanet& Planet, const FVector3d& Site,
		TFunctionRef<void(const FLedgerTerrainSample&)> Visit) const;

	bool bRunning = false;
	bool bLevelled = false;
	double Waited = 0.0;

	/// What the ground was doing before, and what it was brought to.
	double Relief = 0.0;
	double Target = 0.0;
};
