// The pole-to-equator climate transect: T051's acceptance, as a file.
//
// **A separate mode rather than a step in the scripted flight**, for the same
// reason the collision transect is: it has nothing to do with flying. Climate
// is a pure function of position, so this needs a planet only to borrow its
// seed and radius — it walks a meridian, samples, writes `out/climate.txt`
// and exits. Seconds, not the flight's three minutes.
//
// Launch with `-climate`.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "LedgerClimateTransect.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerClimateTransect : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/// Walks the meridian and writes the report. Returns false if the acceptance
	/// does not hold, which the log says plainly rather than leaving to whoever
	/// opens the file.
	bool WriteTransect();
};
