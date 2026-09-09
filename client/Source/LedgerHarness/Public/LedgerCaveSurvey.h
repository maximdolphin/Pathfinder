// What the cave field actually contains, before anything is meshed. T056.
//
// The acceptance is "walk into a cave mouth, through a passage, and out the
// other side". There is no walkable pawn until M09, but every other word of
// that is a property of the field and can be measured now: whether mouths
// exist, whether a mouth leads anywhere, and whether what it leads to comes
// back out somewhere else.
//
// Measuring first is the point. The tunnel spacing, the passage radius and the
// region threshold are all guesses, and a mesher built on top of guesses would
// take a day to tell you the field was empty.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerCaveSurvey.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerCaveSurvey : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	bool WriteSurvey();
};
