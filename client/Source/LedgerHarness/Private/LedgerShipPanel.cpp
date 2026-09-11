#include "LedgerShipPanel.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerShipSystems.h"
#include "LedgerShipSystemsComponent.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr double PanelSettleSeconds = 15.0;
	/// Seconds each state is held before it is photographed.
	constexpr double PanelHoldSeconds = 4.0;
}

bool ULedgerShipPanel::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerShipPanel::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerShipPanel, STATGROUP_Tickables);
}

void ULedgerShipPanel::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("shippanel"));
	Lines.Add(TEXT("The ship panel, photographed (T119, T122, T128)."));
	Lines.Add(TEXT(""));
}

void ULedgerShipPanel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;
	ULedgerShipSystemsComponent* Systems = Ship != nullptr ? Ship->GetSystems() : nullptr;
	if (Planet == nullptr || Systems == nullptr || !Systems->IsConfigured())
	{
		return;
	}
	const FLedgerShipDefinition& Definition = Systems->GetDefinition();
	FLedgerShipState& State = Systems->EditState();
	ShipName = Definition.Name;
	Clock += DeltaSeconds;
	StageClock += DeltaSeconds;

	// On the ground a kilometre from the town, the camera off its quarter and
	// far enough back that the panel does not cover the ship.
	const FVector3d Site = Builder->GetSiteDirection().GetSafeNormal();
	const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Site).GetSafeNormal();
	const FVector3d Up = (Site + East * (100000.0 / Planet->Radius)).GetSafeNormal();
	const FVector3d North = FVector3d::CrossProduct(Up, East);
	const FVector Where = FVector(FVector3d(Planet->GetActorLocation()) + Up * (Planet->SurfaceRadiusAt(Up) + 250.0));
	Ship->SetFlightEnabled(false);
	Ship->SetActorLocation(Where);
	Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(North), FVector(Up)).Rotator());
	Ship->SetActorHiddenInGame(false);
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Where, FRotator::ZeroRotator, SpawnParams);
		Camera->GetCameraComponent()->SetFieldOfView(60.0f);
		Controller->SetViewTarget(Camera);
	}
	const double Reach = Definition.Hull.LengthMetres / 9.0;
	const FVector Eye = Where + FVector((-North * 2000.0 - East * 1400.0 + Up * 800.0) * Reach);
	Camera->SetActorLocationAndRotation(Eye, FRotationMatrix::MakeFromXZ(Where - Eye, FVector(Up)).Rotator());

	// With the panel in the frame: this is a photograph of the screen.
	auto Shot = [this](const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),
			TEXT(".."), TEXT("out"), FString::Printf(TEXT("panel-%s-%s.png"), *ShipName, Name))), true, false);
	};
	auto Next = [this]()
	{
		++Stage;
		StageClock = 0.0;
	};
	const int32 Shield = Definition.FindComponent(TEXT("shield"));
	int32 Hold = INDEX_NONE;
	for (int32 Index = 0; Index < Definition.Components.Num() && Hold == INDEX_NONE; ++Index)
	{
		Hold = Definition.Components[Index].Type == TEXT("CargoHold") ? Index : INDEX_NONE;
	}
	bHasHold = Hold != INDEX_NONE;

	if (Stage == 0)
	{
		if (Shield != INDEX_NONE)
		{
			State.Components[Shield].bOn = false;
		}
		if (Clock < PanelSettleSeconds)
		{
			return;
		}
		HeadroomDown = Systems->LastPower().ThrustHeadroomKw;
		Shot(TEXT("0-shield-down"));
		Next();
		return;
	}
	if (Stage == 1)
	{
		if (Shield != INDEX_NONE)
		{
			State.Components[Shield].bOn = true;
		}
		if (StageClock < PanelHoldSeconds)
		{
			return;
		}
		HeadroomUp = Systems->LastPower().ThrustHeadroomKw;
		Shot(TEXT("1-shield-up"));
		Next();
		return;
	}
	if (Stage == 2)
	{
		// A second after the last photograph, so it is not in it: the plant
		// the fitting test offers, twice the output and twice the mass.
		if (!bPreviewShown && StageClock > 1.0)
		{
			bPreviewShown = true;
			// The first plant, whatever the file calls it: the hauler has two.
			int32 Reactor = INDEX_NONE;
			for (int32 Index = 0; Index < Definition.Components.Num() && Reactor == INDEX_NONE; ++Index)
			{
				Reactor = Definition.Components[Index].Param(TEXT("outputKw")) > 0.0 ? Index : INDEX_NONE;
			}
			if (Reactor != INDEX_NONE)
			{
				FLedgerComponent Big = Definition.Components[Reactor];
				Big.Id = TEXT("reactor_large");
				Big.MassKg = 1600.0;
				Big.Params.Add(TEXT("outputKw"), 700.0);
				Big.Params.Add(TEXT("slotClass"), 2.0);
				const FLedgerOutfitPreview Preview = LedgerShips::PreviewSwap(Definition, Definition.Components[Reactor].Id, Big);
				Systems->ShowPreview(Preview, FString::Printf(TEXT("reactor_large, 700 kW and 1600 kg, in place of %s"), *Definition.Components[Reactor].Id));
				bFitsAndCosts = Preview.bFits && Preview.MassDeltaKg > 0.0 && Preview.HeatDeltaKw > 0.0
					&& Preview.AccelerationAfter < Preview.AccelerationBefore;
				Lines.Add(FString::Printf(TEXT("a 700 kW plant offered for the reactor slot: fits %s; %+.0f kg, %+.0f kW of heat, %+.0f kW of margin; main thrust %.0f to %.0f m/s2; roll inertia %.0f to %.0f kg m2"),
					Preview.bFits ? TEXT("yes") : *Preview.Why, Preview.MassDeltaKg, Preview.HeatDeltaKw, Preview.PowerMarginDeltaKw,
					Preview.AccelerationBefore, Preview.AccelerationAfter, Preview.RollInertiaBefore, Preview.RollInertiaAfter));
			}
		}
		if (StageClock < PanelHoldSeconds)
		{
			return;
		}
		Shot(TEXT("2-fitting"));
		Next();
		return;
	}
	if (Stage == 3)
	{
		if (StageClock > 1.0 && !bStowed)
		{
			bStowed = true;
			Systems->ClearPreview();
			if (Hold != INDEX_NONE)
			{
				// Crates of ore until the hold says no, and why.
				FLedgerCargoItem Ore;
				Ore.Name = TEXT("ore");
				Ore.MassKg = 800.0;
				Ore.VolumeM3 = 2.0;
				FString Why;
				while (Crates < 1000 && LedgerShipSystems::Stow(Definition, State, Hold, Ore, &Why))
				{
					++Crates;
				}
				HoldBound = Why;
			}
		}
		if (StageClock < PanelHoldSeconds)
		{
			return;
		}
		if (bHasHold)
		{
			Shot(TEXT("3-hold"));
		}
		Next();
		return;
	}
	// A second for the last photograph to be written.
	if (StageClock < 1.0)
	{
		return;
	}
	bRunning = false;
	Finish();
}

void ULedgerShipPanel::Finish()
{
	const bool bShield = HeadroomUp < HeadroomDown - 1.0;
	const bool bHold = !bHasHold || (Crates > 0 && !HoldBound.IsEmpty());
	Lines.Add(FString::Printf(TEXT("power left for thrust, as the panel shows it: %.0f kW with the shield down, %.0f kW with it up"),
		HeadroomDown, HeadroomUp));
	if (bHasHold)
	{
		Lines.Add(FString::Printf(TEXT("the hold took %d crates of ore (800 kg, 2 m3 each) and then said: %s"), Crates, *HoldBound));
	}
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("raising the shield shows on the panel as power taken from thrust: %s"), bShield ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("an oversized plant shows its mass, heat and handling before it is fitted: %s"), bFitsAndCosts ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("the hold fills until volume or mass binds, and shows it: %s"),
		!bHasHold ? TEXT("(no hold on this ship)") : (bHold ? TEXT("yes") : TEXT("NO"))));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bShield && bFitsAndCosts && bHold ? TEXT("PASS") : TEXT("FAIL")));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("ship panel: %s"), *Line);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), FString::Printf(TEXT("panel-%s.txt"), *ShipName))));
	FPlatformMisc::RequestExit(false);
}
