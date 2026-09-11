// Change the wind and time how long everything takes to notice. T093.
//
// The acceptance is one sentence: all five consumers respond to the same wind
// change within a second. So this makes a change -- a real one, the weather at
// another moment, chosen because the wind there points somewhere else -- and
// then watches the flight model, the vegetation's parameter collection, the
// rain, the audio and the town's wind vane, each at its own position, until
// every one of them reads what the field says is blowing there.
//
// **A change that could not be missed.** The new moment is picked by searching
// for the largest turn in the wind at the site, so a consumer that ignored the
// field entirely could not pass by already agreeing with it.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerWindProbe.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerWindProbe : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	/// One consumer's reading against the field at the same place: metres per
	/// second, both, and whether they agree.
	struct FReading
	{
		FVector3d Consumer = FVector3d::ZeroVector;
		FVector3d Field = FVector3d::ZeroVector;
		bool bAvailable = false;
		bool bSpeedOnly = false;
	};

	static constexpr int32 Consumers = 5;
	static const TCHAR* ConsumerName(int32 Index);

	void Read(FReading (&Out)[Consumers]) const;
	static bool Agrees(const FReading& Reading);
	void Report();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FLedgerAirProfile Air;
	FVector3d Anchor = FVector3d::UnitZ();
	int32 Home = INDEX_NONE;

	double BeforeSeconds = 0.0;
	double AfterSeconds = 0.0;
	double TurnDegrees = 0.0;

	/// What each consumer read just before the switch, so the report can show
	/// it moved rather than only that it ended up right.
	FReading Before[Consumers];
	FReading After[Consumers];

	/// Seconds of real time from the switch to agreement, per consumer; a
	/// negative number is "never".
	double Latency[Consumers] = { -1.0, -1.0, -1.0, -1.0, -1.0 };
	int32 LatencyFrames[Consumers] = { -1, -1, -1, -1, -1 };

	int32 Phase = 0;
	double Settle = 0.0;
	double SwitchedAt = 0.0;
	int32 FramesSinceSwitch = 0;
	bool bRunning = false;
};
