// A moon rises, crosses and sets, and then you go there. T088. The M03 gate.
//
// Rule 6 says a milestone ends with something playable, watched and recorded.
// Every other fixture in this module photographs one instant chosen to be
// interesting. This one photographs a night in order, at times nobody chose --
// the ephemeris was asked when the moon would rise and the camera was pointed
// then -- and then flies the crossing the map quotes.
//
// **The whole point is that the two halves are the same numbers.** The moon
// whose altitude is predicted for midnight is the body whose distance is
// quoted for the trip. If the sky and the map disagreed anywhere, the schedule
// and the flight would disagree here.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"
#include "LedgerMap.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerMoonPassage.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerMoonPassage : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	/// One moment of the night: what was asked for, and what was found there.
	struct FMoment
	{
		FString What;
		double Seconds = 0.0;
		double PredictedAltitude = 0.0;
		double RenderedAltitude = 0.0;
		double AzimuthErrorArcminutes = -1.0;
	};

	void Place();
	void Report();

	/// Find the time in a bracket where the moon's altitude crosses zero, or
	/// where it is highest. Bisection and a ternary search, both on the
	/// ephemeris -- the camera is told the answer, it does not look for it.
	double FindCrossing(double Low, double High, bool bRising) const;
	double FindTransit(double Low, double High) const;

	double AltitudeOf(double Seconds) const;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FVector3d Anchor = FVector3d::UnitZ();
	int32 Home = INDEX_NONE;
	int32 Moon = INDEX_NONE;

	TArray<FMoment> Moments;
	int32 Step = 0;

	/// Aiming and capturing are two ticks, not one -- the spheres are placed on
	/// their own tick, so moving the clock and photographing in the same frame
	/// photographs the sky as it was a step ago.
	bool bAimed = false;

	bool bRunning = false;
	double Settle = 0.0;

	/// The crossing, quoted before anything is flown.
	FLedgerSite From;
	FLedgerSite To;
	double QuotedSeconds = -1.0;
	double QuotedDistance = 0.0;
	double LandedGravity = 0.0;
	bool bLandedAirless = false;
};
