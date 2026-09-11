// A ship's condition, on its outside. T127.
//
// Read from the systems, never played: the hull scorches as its integrity
// goes, a destroyed component burns where it sits, and a breached compartment
// vents where it is holed for as long as it has air to lose. So the outside of
// a ship says what its component state says, at a glance.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerShipDamageView.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;

UCLASS()
class LEDGERCLIENT_API ULedgerShipDamageView : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/// What is being shown, for the fixture.
	int32 FiresShown() const { return Fires; }
	int32 VentsShown() const { return Vents; }
	double Scorch() const { return LastScorch; }

private:
	UPointLightComponent* Light(int32 Index);
	/// A cone from a pool, in a colour: vents, flames and smoke.
	UStaticMeshComponent* Plume(TArray<TObjectPtr<UStaticMeshComponent>>& Pool, int32 Index, const FLinearColor& Colour);

	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> Lights;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Plumes;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Flames;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Smokes;

	int32 Fires = 0;
	int32 Vents = 0;
	double LastScorch = 0.0;
	double Clock = 0.0;
};
