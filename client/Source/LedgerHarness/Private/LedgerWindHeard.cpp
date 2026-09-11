#include "LedgerWindHeard.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerAudio.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerWind.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/// Long enough for the audio generator's own fade to finish -- it crosses
	/// the whole range in a twentieth of a second -- plus a few ticks for the
	/// camera move to reach the subsystem. Nothing here waits on exposure,
	/// because nothing here is photographed.
	constexpr double WindHeardSettle = 1.2;

	constexpr double WindHeardCentimetresPerMetre = 100.0;
}

bool ULedgerWindHeard::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerWindHeard::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerWindHeard, STATGROUP_Tickables);
}

void ULedgerWindHeard::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("windheard"));
	if (!bRunning)
	{
		return;
	}

	ULedgerWorldBuilder* Builder = InWorld.GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr)
	{
		bRunning = false;
		return;
	}

	System = Builder->GetSystem();
	Anchor = Builder->GetSiteDirection().GetSafeNormal();
	Home = Builder->GetHomeBodyIndex();
	Air = LedgerAir::For(System, Home, Builder->GetWhenSeconds());

	// **The ladder is the atmosphere's, not a round number's.** Rungs in scale
	// heights mean the same picture on a thick world and a thin one, and the
	// last two are past the top on purpose: the claim is that the sound stops,
	// and a ladder that ends inside the air cannot show it stopping.
	const double H = FMath::Max(Air.ScaleHeightMetres, 1.0);
	Ladder = { 2.0, 0.1 * H, 0.5 * H, 1.0 * H, 2.0 * H, 4.0 * H,
		8.0 * H, Air.TopMetres, Air.TopMetres * 1.5 };

	UE_LOG(LogLedger, Log,
		TEXT("wind heard: body %d (%s), %s, scale height %.0f m, top %.0f m"),
		Home, *System.Bodies[Home].Name, LexToString(Air.Composition),
		Air.ScaleHeightMetres, Air.TopMetres);
}

void ULedgerWindHeard::Tick(float DeltaSeconds)
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
	ULedgerAudio* Audio = World->GetSubsystem<ULedgerAudio>();
	if (Planet == nullptr || Controller == nullptr || Audio == nullptr)
	{
		return;
	}

	if (!Ladder.IsValidIndex(Step))
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	// Straight up the anchor, looking at the horizon. Where the camera points
	// does not change what it hears; it is aimed at all because the listener
	// follows the view and a view target has to face somewhere.
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Anchor);
	const FVector Eye = FVector(
		Centre + Anchor * (Ground + Ladder[Step] * WindHeardCentimetresPerMetre));
	const FVector3d Toward =
		LedgerFrames::ToBody({ Home, Anchor, FVector3d(1.0, 0.0, 0.0) })
			.Metres.GetSafeNormal();

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye);
	}

	const FRotator Look =
		FRotationMatrix::MakeFromXZ(FVector(Toward), FVector(Anchor)).Rotator();
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Eye, Look, SpawnParams);
		if (Camera != nullptr)
		{
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}

	Settle += DeltaSeconds;
	if (Settle < WindHeardSettle)
	{
		return;
	}
	Settle = 0.0;

	const ULedgerAudio::FHeard Heard = Audio->Heard();
	FRung Rung;
	Rung.AltitudeMetres = Heard.AltitudeMetres;
	Rung.DensityKgPerM3 = Heard.DensityKgPerM3;
	Rung.SpeedMetresPerSecond = Heard.SpeedMetresPerSecond;
	Rung.DynamicPressurePascals = Heard.DynamicPressurePascals;
	Rung.Gain = Heard.Gain;
	Rung.CutoffHz = Heard.CutoffHz;
	Rung.Rms = Audio->HeardRms();
	Rungs.Add(Rung);

	UE_LOG(LogLedger, Log,
		TEXT("wind heard %d/%d: %.0f m, rho %.5f, wind %.1f m/s, q %.2f Pa, "
			 "level %.3f at %.0f Hz, rms %.3f"),
		Step + 1, Ladder.Num(), Rung.AltitudeMetres, Rung.DensityKgPerM3,
		Rung.SpeedMetresPerSecond, Rung.DynamicPressurePascals, Rung.Gain,
		Rung.CutoffHz, Rung.Rms);

	++Step;
}

void ULedgerWindHeard::Report()
{
	TArray<FString> Lines;
	Lines.Add(TEXT("Wind noise against dynamic pressure, from the ground to vacuum (T102)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("body             %d (%s)"),
		Home, System.Bodies.IsValidIndex(Home) ? *System.Bodies[Home].Name : TEXT("?")));
	Lines.Add(FString::Printf(TEXT("air              %s at %.0f Pa"),
		LexToString(Air.Composition), Air.SurfacePressurePascals));
	Lines.Add(FString::Printf(TEXT("scale height     %.0f m"), Air.ScaleHeightMetres));
	Lines.Add(FString::Printf(TEXT("top of the air   %.0f m"), Air.TopMetres));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("  altitude m   rho kg/m3   wind m/s     q Pa    level   cutoff Hz    rms"));

	for (const FRung& Rung : Rungs)
	{
		Lines.Add(FString::Printf(
			TEXT("%12.0f %11.5f %10.1f %9.2f %8.3f %11.0f %6.3f"),
			Rung.AltitudeMetres, Rung.DensityKgPerM3, Rung.SpeedMetresPerSecond,
			Rung.DynamicPressurePascals, Rung.Gain, Rung.CutoffHz, Rung.Rms));
	}

	// **The two claims, checked rather than left to the reader.**
	//
	// Proportionality is checked as a ratio between rungs rather than against a
	// constant, because the wind speed changes with height as well -- the log
	// profile and the friction backing both bite in the first hundred metres.
	// Level over q is the quantity that should be flat wherever the level is
	// neither zero nor clipped, and it is the whole of "scales with dynamic
	// pressure".
	double Worst = 0.0;
	double Reference = -1.0;
	int32 Compared = 0;
	for (const FRung& Rung : Rungs)
	{
		if (Rung.Gain <= 0.0 || Rung.Gain >= 1.0 || !(Rung.DynamicPressurePascals > 0.0))
		{
			continue;
		}
		const double Ratio = Rung.Gain / Rung.DynamicPressurePascals;
		if (Reference < 0.0)
		{
			Reference = Ratio;
		}
		Worst = FMath::Max(Worst, FMath::Abs(Ratio / Reference - 1.0));
		++Compared;
	}

	const bool bProportional = Compared >= 2 && Worst < 1e-6;
	const bool bSilentAbove = Rungs.Num() > 0
		&& Rungs.Last().Gain == 0.0
		&& Rungs.Last().DensityKgPerM3 == 0.0;

	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(
		TEXT("level / q over %d rungs varies by %.2e -- %s"),
		Compared, Worst,
		bProportional ? TEXT("proportional") : TEXT("NOT PROPORTIONAL")));
	Lines.Add(FString::Printf(
		TEXT("above the top of the air the level is %s"),
		bSilentAbove ? TEXT("exactly zero") : TEXT("NOT ZERO")));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			TEXT("wind-audio.txt")));
	FFileHelper::SaveStringArrayToFile(Lines, *Path);

	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("%s"), *Line);
	}
}
