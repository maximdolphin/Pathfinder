// The aurora, drawn. T103.
//
// One additive shell around the planet at the height aurora glows at, with the
// oval LedgerAurora gives for the world's time. Seen from under it and from
// orbit alike, because it is the same shell.

#pragma once

#include "CoreMinimal.h"
#include "LedgerAurora.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerAuroraView.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UCLASS()
class LEDGERCLIENT_API ULedgerAuroraView : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

	/// What was last drawn, for the fixture: the oval and its strength.
	FLedgerAurora Drawn() const { return Last; }

private:
	UPROPERTY()
	TObjectPtr<AActor> Holder;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shell;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;

	FLedgerAurora Last;
	double LastLoggedKp = -1.0;
};
