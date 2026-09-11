// A low crosses a site, and it rains. T095.
//
// The acceptance has three clauses and they need three different kinds of
// evidence, so this fixture produces all three from one run: a *time* series
// across the passage, to show something arrives and leaves; a *vertical*
// profile at the worst of it, to show rain below and snow above; and the
// altitude at which the one becomes the other, found by bisecting what the
// model actually answers rather than by reading the freezing level off it.
//
// When the front arrives is not chosen. It is found, by scanning the weather
// for the moment the pressure anomaly over the site is deepest -- which is the
// same arithmetic the renderer will do when it is asked what the sky is doing,
// because there is only the one weather.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "LedgerPrecipitation.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerFrontWatch.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerFrontWatch : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	void Place();
	void Report();

	struct FMoment
	{
		const TCHAR* What = nullptr;
		double HoursFromPeak = 0.0;
		double AnomalyPascals = 0.0;
		double RateMillimetresPerHour = 0.0;
		ELedgerPrecipitation Kind = ELedgerPrecipitation::None;
		int32 Drops = 0;
	};

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	FLedgerSystem System;
	FLedgerAirProfile Air;
	FVector3d Anchor = FVector3d::UnitZ();
	int32 Home = INDEX_NONE;

	double Latitude = 0.0;
	double Longitude = 0.0;
	double SurfaceKelvin = 0.0;

	/// When the anomaly over the site is deepest, and how deep.
	double PeakSeconds = 0.0;
	double PeakAnomaly = 0.0;

	TArray<FMoment> Moments;

	/// The vertical profile at the peak, and the changeover found in it.
	TArray<TPair<double, ELedgerPrecipitation>> Profile;
	double BisectedBoundary = 0.0;
	double DeclaredFreezingLevel = 0.0;

	int32 Step = 0;
	bool bAimed = false;
	bool bRunning = false;
	double Settle = 0.0;
};
