// T046's acceptance, as a runnable capture pass.
//
// Launch with `-surfacestudy`. Nothing flies. The camera is planted on the
// ground at the town site and photographs it at three distances under two sun
// angles, then exits.
//
// It exists because the scripted flight cannot answer the question. Its lowest
// capture is a couple of hundred metres up, which is past the material's detail
// fade, so every image it produces is of ground that has already faded to a
// flat wash — and judging a surface material on those is how you conclude the
// textures are working when they are not, or the reverse.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerSurfaceStudy.generated.h"

class ACameraActor;
class ALedgerPlanet;

UCLASS()
class LEDGERHARNESS_API ULedgerSurfaceStudy : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/// Puts the camera and the sun where the current shot wants them.
	void Place();

	/// The view. Spawned once and moved, so the transition between shots
	/// does not rebuild exposure state from scratch each time.
	UPROPERTY()
	TObjectPtr<ACameraActor> Camera = nullptr;

	bool bRunning = false;
	int32 Index = 0;

	/// Seconds spent settling before the current shot. Terrain streams, the sky
	/// light rebuilds and auto-exposure adapts; photographing before all three
	/// have finished measures the loading screen.
	double Settle = 0.0;
	bool bCaptured = false;
};
