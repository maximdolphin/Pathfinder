// Rain and snow you can stand in. T095, and T093's fifth consumer.
//
// **A box that follows the camera.** Precipitation is not a place, it is a
// volume the viewer is always inside, so the particles live in a cube a few
// tens of metres across centred on wherever the listener is and wrap around its
// faces as they leave. Ten thousand drops in a forty-metre box look like rain
// over a whole landscape for the same reason a raindrop on a window looks like
// weather: nothing is visible far enough away to contradict it.
//
// Each particle carries the one velocity that matters -- the wind where it is,
// from ULedgerWind, plus its own terminal fall speed -- so rain slants and snow
// drifts without either being a parameter anybody set. That is the point of
// T093: the same subsystem the flight model and the audio read.

#pragma once

#include "CoreMinimal.h"
#include "LedgerPrecipitation.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerPrecipitationView.generated.h"

class AActor;
class UInstancedStaticMeshComponent;

UCLASS()
class LEDGERCLIENT_API ULedgerPrecipitationView : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/// What is falling where the viewer is, refreshed each tick. Queried by the
	/// fixture so the picture and the table cannot disagree.
	FLedgerPrecipitation Falling() const { return Last; }

	/// How many particles are actually drawn. Zero in fair weather, and zero is
	/// the honest answer rather than a hidden emitter.
	int32 DrawnCount() const { return Drawn; }

	/// The wind the particles were last moved by, metres per second in world
	/// space. Read every tick whether or not anything is falling, so the
	/// consumer is live in fair weather too.
	FVector3d LastWindMetres() const { return LastWind; }

private:
	void Rebuild(int32 Wanted);

	UPROPERTY()
	TObjectPtr<AActor> Holder;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Drops;

	/// World positions, in centimetres, kept in doubles because the world is
	/// six hundred million centimetres across and a float loses metres there.
	TArray<FVector3d> Positions;

	FLedgerPrecipitation Last;
	ELedgerPrecipitation DrawnKind = ELedgerPrecipitation::None;
	int32 Drawn = 0;
	FVector3d LastWind = FVector3d::ZeroVector;
	double SinceLog = 0.0;
};
