#include "LedgerAirProbe.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerAir.h"
#include "LedgerCloud.h"
#include "LedgerEnvironment.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerPrecipitation.h"
#include "LedgerPrecipitationView.h"
#include "LedgerSettlement.h"
#include "LedgerShip.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/// Long enough for the town to be built and the precipitation view to have
	/// ticked with the camera standing at the vane.
	constexpr double AirProbeSettleSeconds = 20.0;
}

bool ULedgerAirProbe::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerAirProbe::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerAirProbe, STATGROUP_Tickables);
}

void ULedgerAirProbe::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("airprobe"));
}

void ULedgerAirProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerSettlement* Town = Builder != nullptr ? Builder->GetSettlement() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Town == nullptr || Controller == nullptr)
	{
		return;
	}

	// The camera at the vane, so the renderer is asking about the same column.
	const FVector Vane = Town->VaneTopLocation();
	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetActorHiddenInGame(true);
	}
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Vane, FRotator::ZeroRotator, SpawnParams);
		Controller->SetViewTarget(Camera);
	}
	Camera->SetActorLocation(Vane);

	Clock += DeltaSeconds;
	if (Clock < AirProbeSettleSeconds)
	{
		return;
	}
	bRunning = false;
	Report();
}

void ULedgerAirProbe::Report()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder->GetPlanet();
	ALedgerSettlement* Town = Builder->GetSettlement();
	const ULedgerPrecipitationView* View = World->GetSubsystem<ULedgerPrecipitationView>();

	const FVector Vane = Town->VaneTopLocation();
	const FVector3d Up = (FVector3d(Vane) - FVector3d(Planet->GetActorLocation())).GetSafeNormal();
	const FLedgerAirProfile Air = LedgerAir::For(
		Builder->GetSystem(), Builder->GetHomeBodyIndex(), Builder->GetWhenSeconds());
	const double Lapse = LedgerCloud::EnvironmentalLapseRate(Air);
	const FLedgerAirHere Base = LedgerEnvironment::At(World, Vane);

	TArray<FString> Lines;
	Lines.Add(TEXT("Temperature and pressure through a column over the town (T099)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("vane at %.0f m above the datum, %.1f m above the ground; lapse %.5f K/m, scale height %.0f m"),
		Base.AltitudeMetres, Base.AboveGroundMetres, Lapse, Air.ScaleHeightMetres));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("  up m    kelvin   predicted   pascals   predicted   kg/m3"));
	double WorstKelvin = 0.0;
	double WorstPascalFraction = 0.0;
	for (int32 Rung = 0; Rung <= 10; ++Rung)
	{
		const double Height = Rung * 500.0;
		const FLedgerAirHere Here = LedgerEnvironment::At(World, Vane + FVector(Up * Height * 100.0));
		// From the bottom rung and the height alone, not from the function's
		// own idea of how far above the ground it is.
		const double Kelvin = Base.Kelvin - Lapse * Height;
		const double Pascals = Base.Pascals * FMath::Exp(-Height / Air.ScaleHeightMetres);
		WorstKelvin = FMath::Max(WorstKelvin, FMath::Abs(Here.Kelvin - Kelvin));
		WorstPascalFraction = FMath::Max(WorstPascalFraction, FMath::Abs(Here.Pascals - Pascals) / Pascals);
		Lines.Add(FString::Printf(TEXT("%6.0f  %8.3f  %10.3f  %8.0f  %10.0f   %.4f"),
			Height, Here.Kelvin, Kelvin, Here.Pascals, Pascals, Here.DensityKgPerM3));
	}

	// Where the column reaches freezing, found by bisecting the field, against
	// the freezing level the rain-or-snow decision reads.
	const double Declared = LedgerPrecip::FreezingLevelMetres(Air, Base.GroundKelvin);
	double Low = 0.0;
	double High = 20000.0;
	for (int32 Step = 0; Step < 60; ++Step)
	{
		const double Middle = (Low + High) * 0.5;
		const FVector At = Vane + FVector(Up * (Middle - Base.AboveGroundMetres) * 100.0);
		(LedgerEnvironment::At(World, At).Kelvin > 273.15 ? Low : High) = Middle;
	}
	const double Found = (Low + High) * 0.5;

	const double Renderer = View != nullptr ? View->GroundKelvin() : 0.0;
	const double Settlement = Town->AirAtVane().GroundKelvin;

	const bool bLapse = WorstKelvin < 1.0e-6 && WorstPascalFraction < 1.0e-9;
	const bool bFreezing = Declared <= 0.0 || FMath::Abs(Found - Declared) < 0.01;
	const bool bSame = View != nullptr && Renderer == Settlement;
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("worst temperature against the lapse rate %.2e K, pressure against the scale height %.2e"),
		WorstKelvin, WorstPascalFraction));
	Lines.Add(FString::Printf(TEXT("freezing %.3f m above the ground by bisecting the field, %.3f m by the precipitation model"),
		Found, Declared));
	Lines.Add(FString::Printf(TEXT("ground at the vane: settlement reads %.6f K, renderer reads %.6f K"),
		Settlement, Renderer));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("temperature matches the lapse-rate prediction: %s"), bLapse ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("the rain-or-snow boundary is the field's freezing level: %s"), bFreezing ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("the settlement reads the number the renderer does: %s"), bSame ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bLapse && bFreezing && bSame ? TEXT("PASS") : TEXT("FAIL")));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("air probe: %s"), *Line);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("air.txt"))));
	FPlatformMisc::RequestExit(false);
}
