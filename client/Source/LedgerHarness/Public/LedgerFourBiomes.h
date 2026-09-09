// Stand still in four places and look at the ground. T437.
//
// The proof M2S is held to, and it is deliberately not a number. Every other
// fixture in this milestone measures something -- pixels per quad, RMS from a
// fitted line, autocorrelation at the tiling period -- and a surface can pass
// all of them and still look like a video game. So the last one photographs
// four different biomes at eye height and the verdict is a person's.
//
// What it *can* check is that the four are actually different places: same
// camera, same height, same sun, four dominant biomes, and a measured
// difference between the four images. Four photographs of the same desert
// would satisfy a careless reading of the acceptance and this refuses it.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerFourBiomes.generated.h"

class ACameraActor;

UCLASS()
class LEDGERHARNESS_API ULedgerFourBiomes : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	bool FindSites();
	void Place();
	void Report();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	/// Where each shot stands, and what the ground there is called.
	TArray<FVector3d> Sites;
	TArray<FString> Names;

	bool bRunning = false;
	bool bFound = false;
	bool bCaptured = false;
	int32 Shot = 0;
	double Settle = 0.0;
};
