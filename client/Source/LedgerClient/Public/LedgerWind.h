// One wind, asked for in world space. T093.
//
// **The point of this file is that there is only one of it.** A wind that the
// flight model, the grass, the particles, the audio and the settlement each
// worked out for themselves would be five winds, and they would disagree the
// first time anybody changed one -- which is the failure mode the acceptance is
// written against. So the physics lives in LedgerWeather, in latitude and
// longitude and metres per second, and this is the one place that turns a world
// position into that question and the answer back into a world vector.
//
// It is a query, not a state. Nothing here is ticked, accumulated or
// interpolated: the weather is a function of time, so the wind now and the wind
// in an hour are two calls with different arguments.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerWind.generated.h"

class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;

UCLASS()
class LEDGERCLIENT_API ULedgerWind : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/// Wind at a world position, centimetres per second in world space.
	///
	/// The altitude is taken from the position's distance to the body's centre
	/// against the terrain under it, so the same call answers for a blade of
	/// grass and for a ship at ten kilometres.
	FVector WindAt(const FVector& WorldPosition) const;

	/// The same, in metres per second, for callers who work in SI -- which is
	/// everything above the renderer.
	FVector3d WindAtMetres(const FVector& WorldPosition) const;

	/// Speed at a world position, metres per second. The number audio and the
	/// settlement want, and cheaper than the vector for callers who only need
	/// how hard it is blowing.
	double SpeedAt(const FVector& WorldPosition) const;

	/// Where the viewer is and what the wind is doing there, refreshed each
	/// tick and published to the material parameter collection so vegetation
	/// reads the same number the flight model does.
	FVector3d ViewerWindMetres() const { return LastViewerWind; }

private:
	void Publish();

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;

	FVector3d LastViewerWind = FVector3d::ZeroVector;
	double LastPublishedSpeed = -1.0;
};
