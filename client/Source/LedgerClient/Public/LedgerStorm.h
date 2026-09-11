// Storms, seen, heard and felt. T097.
//
// **The tail of the weather that already exists, not a second system.** A storm
// is a low deep enough to be dangerous (LedgerWeather::StormAt), and everything
// here reads that one answer: the four deepest lows go to the cloud material so
// the overcast seen from orbit is where they are, flashes are drawn at the rate
// the severity sets around the viewer, and thunder follows each flash by the
// time sound takes to cover the distance. The ship asks the same subsystem how
// bad it is where it is flying.

#pragma once

#include "CoreMinimal.h"
#include "LedgerWeather.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerStorm.generated.h"

class UMaterialParameterCollection;
class UPointLightComponent;
class ULedgerWindVoice;

UCLASS()
class LEDGERCLIENT_API ULedgerStorm : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/// The storm at a world position, at the world's time.
	FLedgerStorm StormAt(const FVector& WorldPosition) const;

	/// The storm's gusts at a world position, centimetres per second in world
	/// space. Zero outside a storm. The ship adds these to the wind it flies in.
	FVector GustAt(const FVector& WorldPosition) const;

	/// Flashes drawn and thunderclaps heard since begin play, for the fixture.
	int32 FlashCount() const { return Flashes; }
	int32 ThunderCount() const { return Thunders; }

	/// The lows as last published to the clouds: a unit direction from the
	/// planet's centre and an angular radius in radians.
	const TArray<FVector4d>& PublishedLows() const { return Lows; }

private:
	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Flash;

	UPROPERTY()
	TObjectPtr<ULedgerWindVoice> Thunder;

	bool bLookedForCollection = false;
	TArray<FVector4d> Lows;
	TArray<double> LowDepths;
	double PublishedWhen = -1.0e300;
	double LoggedWhen = -1.0e300;

	/// Thunder on its way: seconds until it arrives, and how loud it will be.
	TArray<TPair<double, float>> Rumbles;
	double FlashLeft = 0.0;
	double ThunderLevel = 0.0;
	int32 Flashes = 0;
	int32 Thunders = 0;
	FRandomStream Dice{ 20260911 };
};
