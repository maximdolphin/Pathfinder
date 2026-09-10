// Three frames that settle whether fog is fog. T091.
//
// The acceptance is one sentence with three clauses, so this is three cameras:
// standing in the valley at dawn, standing on the ridge above it at the same
// instant, and looking down from two radii out. If the fog were planar the
// second frame would be foggy too; if it were global the third would be.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "LedgerFog.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerFogWatch.generated.h"

class ACameraActor;
class ALedgerGroundFog;

UCLASS()
class LEDGERHARNESS_API ULedgerFogWatch : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Prepare();
	void Place();
	void Report();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<ALedgerGroundFog> Fog;

	FLedgerSystem System;
	FLedgerAirProfile Air;
	FLedgerFog Weather;
	FVector3d Anchor = FVector3d::UnitZ();
	int32 Home = INDEX_NONE;

	/// The lowest and highest ground found near the site, and where they are.
	FVector3d ValleyDirection = FVector3d::UnitZ();
	FVector3d RidgeDirection = FVector3d::UnitZ();
	double ValleyMetres = 0.0;
	double RidgeMetres = 0.0;

	double DawnSeconds = -1.0;
	double NightSeconds = 0.0;
	int32 CellsFilled = 0;

	int32 Step = 0;
	bool bAimed = false;
	bool bRunning = false;
	bool bPrepared = false;
	bool bPlaced = false;
	double Settle = 0.0;
};
