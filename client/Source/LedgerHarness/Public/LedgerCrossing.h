// Watch a moon, go there, land on it. T088's second clause.
//
// **In one session** is the whole of the difficulty. The world is built around
// one body: its terrain, its sky and its town are all functions of which body
// that is, and until now the only way to stand on a different one was to start
// a new process with `-body=`. T088's acceptance asks for the crossing, not for
// two photographs taken on different afternoons.
//
// So this drives the whole thing end to end: it looks at the moon from the home
// world, quotes the trip with the map from T087, runs the clock forward by the
// quoted time with T085's accelerator while the ship flies the route, hands the
// world builder the moon, and lands.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"
#include "LedgerMap.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerCrossing.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerCrossing : public UTickableWorldSubsystem
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

	FLedgerSystem System;
	FVector3d Anchor = FVector3d::UnitZ();
	int32 Home = INDEX_NONE;
	int32 Moon = INDEX_NONE;

	FLedgerSite From;
	FLedgerSite To;

	/// What the map said before anything moved, and what actually happened.
	double QuotedSeconds = -1.0;
	double QuotedDistance = 0.0;
	double DepartureSeconds = 0.0;

	/// Recorded at the far end, to show the ground under the ship is the
	/// moon's and not a copy of the planet's.
	double ArrivedGravity = 0.0;
	double ArrivedRadiusMetres = 0.0;
	bool bArrivedAirless = false;
	bool bSwitched = false;

	int32 Step = 0;
	bool bAimed = false;
	bool bRunning = false;
	bool bPrepared = false;
	double Settle = 0.0;
};
