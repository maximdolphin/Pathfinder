// M02's collision gate, as a runnable measurement.
//
// Launch with `-transect` and this runs instead of the scripted flight: two
// hundred kilometres at 900 m/s and fifty metres up, with a downward trace on
// every frame, counting the ones that hit nothing.
//
// The failure it exists to catch cannot be seen. Streaming collision is cooked
// near the camera and ahead along the velocity vector, and when it fails to
// arrive a ship has nothing beneath it for a moment somewhere in the middle of
// a long fast run — and is past it before anybody finishes being surprised.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerTransect.generated.h"

class ALedgerPlanet;
class ALedgerShip;

UCLASS()
class LEDGERHARNESS_API ULedgerTransect : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Place(ALedgerPlanet& Planet, ALedgerShip& Ship,
		APlayerController& Controller, double Along) const;
	void Finish();

	bool bRunning = false;
	double Warmup = 0.0;
	double Travelled = 0.0;

	int32 Frames = 0;
	int32 Misses = 0;
	/// The misses by cause (T068): nothing drawn under the ship, drawn ground
	/// that never asked for collision, or collision asked for and not cooked.
	int32 MissesNoGround = 0;
	int32 MissesNoCollision = 0;
	int32 MissesUncooked = 0;
	double MissPatchSizeSum = 0.0;
	/// Hits on which the query also found drawn ground: the check on the check.
	int32 HitsSampled = 0;

	/// Consecutive misses, and the worst run of them. One isolated miss and a
	/// hundred in a row are the same number of misses and very different bugs.
	int32 CurrentMiss = 0;
	int32 LongestMiss = 0;
};
