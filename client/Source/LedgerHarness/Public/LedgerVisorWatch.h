// Rain on the canopy, slow and fast, and fog cleared by the heater. T101.
//
// Through the ship's own camera, moved to the cockpit, at a place and time the
// weather has rain in daylight: slow through the rain, then fast, then with the
// wipers on and the glass left to fog, then with the heater on. Each frame is
// photographed with the canopy state it was drawn with.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerVisorWatch.generated.h"

UCLASS()
class LEDGERHARNESS_API ULedgerVisorWatch : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Finish();

	FLedgerSystem System;
	FLedgerAirProfile Air;
	int32 Home = INDEX_NONE;
	FVector3d Anchor = FVector3d::UnitZ();
	double RainSeconds = 0.0;
	double Along = 0.0;

	double Water[4] = { 0.0, 0.0, 0.0, 0.0 };
	double Flow[4] = { 0.0, 0.0, 0.0, 0.0 };
	double Fog[4] = { 0.0, 0.0, 0.0, 0.0 };

	TArray<FString> Lines;
	int32 Step = 0;
	double Clock = 0.0;
	bool bRunning = false;
};
