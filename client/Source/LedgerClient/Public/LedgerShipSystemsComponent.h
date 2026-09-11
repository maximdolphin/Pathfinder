// A ship's systems, running on the ship. T131's machine.
//
// The pure models in LedgerShipSystems, ticked on the actor: power solved,
// heat moved, shields charged, gear driven, reactors fuelled, parts worn and
// the air kept, every frame. The ship asks it how much of the thrust it wants
// the systems can give -- power, wear and fuel all have a say -- and tells it
// what thrust it used, so the fuel goes with the flying.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"
#include "LedgerShipSystemsComponent.generated.h"

UCLASS()
class LEDGERCLIENT_API ULedgerShipSystemsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULedgerShipSystemsComponent();

	/// A ship to run. Until this is called there is nothing to run and every
	/// share is whole, so a ship with no definition flies as it always did.
	void Configure(const FLedgerShipDefinition& InDefinition);
	bool IsConfigured() const { return Definition.Components.Num() > 0; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	const FLedgerShipDefinition& GetDefinition() const { return Definition; }
	const FLedgerShipState& GetState() const { return State; }
	FLedgerShipState& EditState() { return State; }

	/// The share of the main and manoeuvring thrust the systems can give now.
	double MainThrustShare() const { return MainShare; }
	double ManoeuvringThrustShare() const { return ManoeuvringShare; }

	/// The thrust the flight model used this frame, newtons, for the fuel.
	void Burned(double MainNewtons, double ManoeuvringNewtons, double DeltaSeconds);

	/// Mass, from the hull, components and contents: what the thrust pushes.
	double MassKg() const;

	/// A hit at a point on the ship, metres in its frame, from a direction:
	/// the shields take what they can, the hull what is left, and the component
	/// nearest the point is damaged by what gets through.
	void TakeHit(const FVector3d& PointMetres, const FVector3d& FromDirection, double EnergyJoules);

	const FLedgerPowerReport& LastPower() const { return Power; }

	/// T128: a fitting shown before it is made -- what the configuration
	/// screen draws. The ship is not changed by it.
	void ShowPreview(const FLedgerOutfitPreview& InPreview, const FString& Label)
	{
		Preview = InPreview;
		PreviewLabel = Label;
		bPreviewing = true;
	}
	void ClearPreview() { bPreviewing = false; }
	const FLedgerOutfitPreview* GetPreview() const { return bPreviewing ? &Preview : nullptr; }
	const FString& GetPreviewLabel() const { return PreviewLabel; }
	ELedgerAirWarning LastAirWarnings() const { return AirWarnings; }

private:
	FLedgerShipDefinition Definition;
	FLedgerShipState State;
	FLedgerPowerReport Power;
	ELedgerAirWarning AirWarnings = ELedgerAirWarning::None;
	FLedgerOutfitPreview Preview;
	FString PreviewLabel;
	bool bPreviewing = false;
	int32 MainEngine = INDEX_NONE;
	int32 Manoeuvring = INDEX_NONE;
	double MainShare = 1.0;
	double ManoeuvringShare = 1.0;
	double MainFuel = 1.0;
	double ManoeuvringFuel = 1.0;
};
