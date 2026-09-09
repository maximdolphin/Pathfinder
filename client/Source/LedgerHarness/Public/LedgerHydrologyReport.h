// The river network, at the resolution the planet will actually use. T055.
//
// The tests prove the network is *correct* -- nothing uphill, everything
// reaching the sea or a basin, the same every time -- at a coarse lattice,
// because correctness does not depend on how fine it is. This says what the
// network *is*: how much of the land drains to the sea, how many basins swallow
// the rest, and how big the biggest rivers are. Those are the numbers that say
// whether a correct network is also a plausible one.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerHydrologyReport.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerHydrologyReport : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	bool WriteReport();
};
