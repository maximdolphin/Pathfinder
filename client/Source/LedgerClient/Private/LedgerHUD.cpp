#include "LedgerHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "LedgerShip.h"
#include "LedgerShipAudio.h"
#include "LedgerShipSystems.h"
#include "LedgerShipSystemsComponent.h"
#include "Misc/CommandLine.h"

namespace
{
	const FLinearColor HudText(0.92f, 0.94f, 0.96f);
	const FLinearColor HudDim(0.60f, 0.64f, 0.68f);
	const FLinearColor HudWarn(1.0f, 0.46f, 0.26f);
	const FLinearColor HudGood(0.56f, 0.95f, 0.66f);

	/// A gauge reading with its unit, by what it reads.
	FString HudReading(const FString& Reading, double Value)
	{
		if (Reading == TEXT("temperatureK"))
		{
			return FString::Printf(TEXT("%.0f K"), Value);
		}
		if (Reading == TEXT("powerKw"))
		{
			return FString::Printf(TEXT("%.0f kW"), Value);
		}
		if (Reading == TEXT("contentsKg"))
		{
			return FString::Printf(TEXT("%.0f kg"), Value);
		}
		if (Reading == TEXT("pressurePa") || Reading == TEXT("oxygenPa"))
		{
			return FString::Printf(TEXT("%.1f kPa"), Value / 1000.0);
		}
		// Condition, deployment and hull integrity are fractions.
		return FString::Printf(TEXT("%.0f%%"), Value * 100.0);
	}
}

void ALedgerHUD::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas == nullptr || GEngine == nullptr || FParse::Param(FCommandLine::Get(), TEXT("nohud")))
	{
		return;
	}
	const ALedgerShip* Ship = Cast<ALedgerShip>(GetOwningPawn());
	const ULedgerShipSystemsComponent* Systems = Ship != nullptr ? Ship->GetSystems() : nullptr;
	if (Systems == nullptr || !Systems->IsConfigured())
	{
		return;
	}
	const FLedgerShipDefinition& Definition = Systems->GetDefinition();
	const FLedgerShipState& State = Systems->GetState();
	const FLedgerPowerReport& Power = Systems->LastPower();

	// ---- the rows, gathered first so the panel behind them can be sized
	TArray<TPair<FString, FLinearColor>> Rows;
	Rows.Emplace(FString::Printf(TEXT("%s   %.1f t"), *Definition.Name, Systems->MassKg() / 1000.0), HudText);
	Rows.Emplace(FString::Printf(TEXT("power   made %.0f kW   asked %.0f   delivered %.0f"),
		Power.SupplyKw, Power.DemandKw, Power.DeliveredKw), HudText);
	Rows.Emplace(FString::Printf(TEXT("left for thrust   %.0f kW"), Power.ThrustHeadroomKw), HudGood);
	if (Power.Shed.Num() > 0)
	{
		Rows.Emplace(TEXT("shed   ") + FString::JoinBy(Power.Shed, TEXT(", "),
			[&Definition](int32 Index) { return Definition.Components[Index].Id; }), HudWarn);
	}
	Rows.Emplace(FString::Printf(TEXT("thrust   main %.0f%%   manoeuvring %.0f%%"),
		Systems->MainThrustShare() * 100.0, Systems->ManoeuvringThrustShare() * 100.0), HudText);
	int32 HoldIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Definition.Components.Num(); ++Index)
	{
		const FLedgerComponentState& Part = State.Components[Index];
		if (Definition.Components[Index].Type == TEXT("Shield"))
		{
			Rows.Emplace(FString::Printf(TEXT("shield   %s   %.0f kW   fore %.0f  aft %.0f  port %.0f  starboard %.0f kJ"),
				Part.bOn ? TEXT("up") : TEXT("down"), Part.PowerKw,
				Part.SectorKj[0], Part.SectorKj[1], Part.SectorKj[2], Part.SectorKj[3]), Part.bOn ? HudGood : HudDim);
		}
		if (Definition.Components[Index].Type == TEXT("CargoHold") && HoldIndex == INDEX_NONE)
		{
			HoldIndex = Index;
		}
	}
	if (const ULedgerShipAudio* Audio = GetWorld()->GetSubsystem<ULedgerShipAudio>())
	{
		if (!Audio->Sounding().IsEmpty())
		{
			Rows.Emplace(TEXT("ALARM   ") + Audio->Sounding(), HudWarn);
		}
	}

	// Every gauge, through the one path an instrument reads by: a dead
	// sensor blanks its gauge and says why rather than showing a number.
	Rows.Emplace(TEXT(""), HudText);
	for (int32 Index = 0; Index < Definition.Instruments.Num(); ++Index)
	{
		const FLedgerInstrument& Gauge = Definition.Instruments[Index];
		const FLedgerGaugeReading Reading = LedgerShipSystems::ReadGauge(Definition, State, Index);
		Rows.Emplace(Reading.bAvailable
			? FString::Printf(TEXT("%s   %s"), *Gauge.Id, *HudReading(Gauge.Reading, Reading.Value))
			: FString::Printf(TEXT("%s   --   %s"), *Gauge.Id, *Reading.Why),
			Reading.bAvailable ? HudText : HudWarn);
	}

	// The configuration screen: a fitting, with what it would cost, before it is made.
	if (const FLedgerOutfitPreview* Preview = Systems->GetPreview())
	{
		Rows.Emplace(TEXT(""), HudText);
		Rows.Emplace(FString::Printf(TEXT("FITTING   %s"), *Systems->GetPreviewLabel()), HudText);
		if (!Preview->bFits)
		{
			Rows.Emplace(TEXT("   does not fit: ") + Preview->Why, HudWarn);
		}
		else
		{
			Rows.Emplace(FString::Printf(TEXT("   mass       %+.0f kg"), Preview->MassDeltaKg), HudText);
			Rows.Emplace(FString::Printf(TEXT("   heat       %+.0f kW flat out"), Preview->HeatDeltaKw),
				Preview->HeatDeltaKw > 0.0 ? HudWarn : HudGood);
			Rows.Emplace(FString::Printf(TEXT("   power      %+.0f kW of margin"), Preview->PowerMarginDeltaKw),
				Preview->PowerMarginDeltaKw >= 0.0 ? HudGood : HudWarn);
			Rows.Emplace(FString::Printf(TEXT("   handling   main thrust %.0f to %.0f m/s2, roll inertia %.0f to %.0f kg m2"),
				Preview->AccelerationBefore, Preview->AccelerationAfter, Preview->RollInertiaBefore, Preview->RollInertiaAfter),
				Preview->AccelerationAfter < Preview->AccelerationBefore ? HudWarn : HudGood);
		}
	}

	double HoldVolume = 0.0;
	double HoldMaxKg = 0.0;
	double HoldKg = 0.0;
	if (HoldIndex != INDEX_NONE)
	{
		HoldVolume = Definition.Components[HoldIndex].Param(TEXT("volumeM3"));
		HoldMaxKg = Definition.Components[HoldIndex].Param(TEXT("maxKg"));
		for (const FLedgerCargoItem& Item : State.Components[HoldIndex].Cargo)
		{
			HoldKg += Item.MassKg;
		}
		Rows.Emplace(TEXT(""), HudText);
		Rows.Emplace(FString::Printf(TEXT("HOLD   %.0f of %.0f m3   %.0f of %.0f kg   %d items"),
			LedgerShipSystems::HoldVolumeUsedM3(State, HoldIndex), HoldVolume, HoldKg, HoldMaxKg,
			State.Components[HoldIndex].Cargo.Num()), HudText);
	}

	// ---- drawn: a dark panel, the rows, and the hold as a grid of cubic metres
	UFont* Font = GEngine->GetSmallFont();
	const float Scale = FMath::Max(Canvas->ClipY / 760.0f, 0.75f);
	const float Step = 15.0f * Scale;
	const float Left = 24.0f * Scale;
	const float Top = 24.0f * Scale;
	const float Cell = 13.0f * Scale;
	const int32 Columns = 15;
	const int32 Cells = FMath::Clamp(FMath::CeilToInt(HoldVolume), 0, 150);
	const float GridHeight = Cells > 0 ? (FMath::DivideAndRoundUp(Cells, Columns) * Cell + 2.0f * Cell) : 0.0f;
	float Width = Columns * Cell;
	for (const TPair<FString, FLinearColor>& Row : Rows)
	{
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		GetTextSize(Row.Key, TextWidth, TextHeight, Font, Scale);
		Width = FMath::Max(Width, TextWidth);
	}
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.62f), Left - 10.0f * Scale, Top - 8.0f * Scale,
		Width + 20.0f * Scale, Rows.Num() * Step + GridHeight + 16.0f * Scale);
	float Y = Top;
	for (const TPair<FString, FLinearColor>& Row : Rows)
	{
		DrawText(Row.Key, Row.Value, Left, Y, Font, Scale);
		Y += Step;
	}
	if (Cells > 0)
	{
		// One square a cubic metre, filled in stowing order, each load its own
		// colour; and under it the mass, against what the hold may carry.
		const FLinearColor Loads[] = { FLinearColor(0.85f, 0.62f, 0.25f), FLinearColor(0.35f, 0.62f, 0.90f),
			FLinearColor(0.55f, 0.85f, 0.45f), FLinearColor(0.85f, 0.40f, 0.55f) };
		TArray<FLinearColor> Filled;
		TMap<FString, int32> Colours;
		for (const FLedgerCargoItem& Item : State.Components[HoldIndex].Cargo)
		{
			const int32 Colour = Colours.FindOrAdd(Item.Name, Colours.Num()) % UE_ARRAY_COUNT(Loads);
			for (int32 Metre = 0; Metre < FMath::Max(FMath::CeilToInt(Item.VolumeM3), 1); ++Metre)
			{
				Filled.Add(Loads[Colour]);
			}
		}
		for (int32 Index = 0; Index < Cells; ++Index)
		{
			const float CellX = Left + (Index % Columns) * Cell;
			const float CellY = Y + (Index / Columns) * Cell;
			DrawRect(FLinearColor(0.2f, 0.22f, 0.25f, 0.9f), CellX, CellY, Cell - 1.0f, Cell - 1.0f);
			if (Filled.IsValidIndex(Index))
			{
				DrawRect(Filled[Index], CellX + 1.0f, CellY + 1.0f, Cell - 3.0f, Cell - 3.0f);
			}
		}
		const float MassY = Y + FMath::DivideAndRoundUp(Cells, Columns) * Cell + 0.5f * Cell;
		DrawRect(FLinearColor(0.2f, 0.22f, 0.25f, 0.9f), Left, MassY, Columns * Cell, 0.6f * Cell);
		DrawRect(HoldKg > HoldMaxKg * 0.95 ? HudWarn : HudGood, Left, MassY,
			Columns * Cell * static_cast<float>(FMath::Clamp(HoldKg / FMath::Max(HoldMaxKg, 1.0), 0.0, 1.0)), 0.6f * Cell);
	}
}
