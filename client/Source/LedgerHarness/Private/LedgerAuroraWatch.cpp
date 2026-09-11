#include "LedgerAuroraWatch.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerAuroraView.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerSky.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	struct FAuroraStep
	{
		const TCHAR* What;
		bool bEvent;
		bool bOrbit;
	};

	const FAuroraStep AuroraSteps[] =
	{
		{ TEXT("event-ground"), true,  false },
		{ TEXT("quiet-ground"), false, false },
		{ TEXT("event-orbit"),  true,  true  },
	};

	constexpr double AuroraSettleSeconds = 15.0;
	constexpr double AuroraDarkDegrees = -12.0;
	constexpr double AuroraOrbitMetres = 3000000.0;

	FVector3d AuroraDirection(double Latitude, double Longitude)
	{
		return FVector3d(FMath::Cos(Latitude) * FMath::Cos(Longitude),
			FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
	}

	/// A point at a magnetic latitude, at an azimuth around the magnetic pole.
	FVector3d AuroraAround(const FVector3d& Pole, double MagneticLatitude, double Azimuth)
	{
		const FVector3d A = FVector3d::CrossProduct(Pole, FVector3d::UnitX()).GetSafeNormal();
		const FVector3d B = FVector3d::CrossProduct(Pole, A);
		const double Colatitude = LedgerPi * 0.5 - MagneticLatitude;
		return (Pole * FMath::Cos(Colatitude)
			+ (A * FMath::Cos(Azimuth) + B * FMath::Sin(Azimuth)) * FMath::Sin(Colatitude)).GetSafeNormal();
	}
}

bool ULedgerAuroraWatch::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerAuroraWatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerAuroraWatch, STATGROUP_Tickables);
}

FLedgerAurora ULedgerAuroraWatch::Model(const FVector3d& Where, double When) const
{
	return LedgerAurora::At(System, Home, Air, FMath::Asin(FMath::Clamp(Where.Z, -1.0, 1.0)),
		FMath::Atan2(Where.Y, Where.X), When);
}

void ULedgerAuroraWatch::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("aurorawatch"));
	if (!bRunning)
	{
		return;
	}
	ULedgerWorldBuilder* Builder = InWorld.GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr || Builder->GetPlanet() == nullptr)
	{
		bRunning = false;
		return;
	}
	System = Builder->GetSystem();
	Home = Builder->GetHomeBodyIndex();
	Air = LedgerAir::For(System, Home, Builder->GetWhenSeconds());
	Pole = LedgerAurora::MagneticPole(System, Home);
	const double Start = Builder->GetWhenSeconds();

	// The strongest event in sixty days.
	double Strongest = 0.0;
	for (double When = Start; When < Start + 60.0 * 86400.0; When += 1800.0)
	{
		bool bEvent = false;
		const double Kp = LedgerAurora::ActivityKp(System, When, &bEvent);
		if (bEvent && Kp > Strongest)
		{
			Strongest = Kp;
			EventSeconds = When;
		}
	}

	// Under its oval, where it is darkest.
	const FLedgerAurora Oval = Model(Pole, EventSeconds);
	double Darkest = TNumericLimits<double>::Max();
	for (int32 Degrees = 0; Degrees < 360; Degrees += 5)
	{
		const FVector3d Candidate = AuroraAround(Pole, Oval.OvalMagneticLatitude, FMath::DegreesToRadians(static_cast<double>(Degrees)));
		const double Sun = LedgerSky::SolarAltitude(System, Home, Candidate, EventSeconds);
		if (Sun < Darkest)
		{
			Darkest = Sun;
			Site = Candidate;
			LowSite = AuroraAround(Pole, FMath::DegreesToRadians(15.0), FMath::DegreesToRadians(static_cast<double>(Degrees)));
		}
	}

	// A quiet night at the same place.
	for (double When = Start; When < Start + 60.0 * 86400.0 && QuietSeconds < 0.0; When += 1800.0)
	{
		bool bEvent = false;
		const double Kp = LedgerAurora::ActivityKp(System, When, &bEvent);
		if (!bEvent && Kp < 2.5 && FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Site, When)) < AuroraDarkDegrees)
		{
			QuietSeconds = When;
		}
	}

	Lines.Add(TEXT("Aurora in a solar event, and not otherwise (T103)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("dipole %.2f of Earth's; magnetic pole at %.1f,%.1f"),
		LedgerAurora::DipoleRelative(System.Bodies[Home]),
		FMath::RadiansToDegrees(FMath::Asin(Pole.Z)), FMath::RadiansToDegrees(FMath::Atan2(Pole.Y, Pole.X))));
	Lines.Add(FString::Printf(TEXT("event: Kp %.1f at t=%.0f s; the oval at %.1f degrees magnetic; site %.2f,%.2f with the sun %.0f degrees down"),
		Strongest, EventSeconds, FMath::RadiansToDegrees(Oval.OvalMagneticLatitude),
		FMath::RadiansToDegrees(FMath::Asin(Site.Z)), FMath::RadiansToDegrees(FMath::Atan2(Site.Y, Site.X)),
		-FMath::RadiansToDegrees(Darkest)));
	Lines.Add(FString::Printf(TEXT("quiet night at the same site: t=%.0f s"), QuietSeconds));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("aurora watch: %s"), *Line);
	}
	if (Strongest <= 0.0 || QuietSeconds < 0.0)
	{
		bRunning = false;
		Finish();
	}
}

void ULedgerAuroraWatch::Tick(float DeltaSeconds)
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
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}
	const FAuroraStep& Now = AuroraSteps[Step];
	const double When = Now.bEvent ? EventSeconds : QuietSeconds;
	Builder->SetWhenSeconds(When);

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetActorHiddenInGame(true);
	}
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		Controller->SetViewTarget(Camera);
		Camera->GetCameraComponent()->SetFieldOfView(90.0f);
	}

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	FVector3d Eye;
	FVector3d Forward;
	FVector3d Up;
	if (Now.bOrbit)
	{
		Eye = Centre + Pole * (Planet->Radius + AuroraOrbitMetres * 100.0);
		Forward = -Pole;
		Up = (Site - Pole * FVector3d::DotProduct(Site, Pole)).GetSafeNormal();
	}
	else
	{
		// On the ground, looking towards the magnetic pole and up into the sky
		// the oval crosses.
		Eye = Centre + Site * (Planet->SurfaceRadiusAt(Site) + 200.0);
		const FVector3d Towards = (Pole - Site * FVector3d::DotProduct(Pole, Site)).GetSafeNormal();
		Forward = (Towards * FMath::Cos(FMath::DegreesToRadians(25.0)) + Site * FMath::Sin(FMath::DegreesToRadians(25.0))).GetSafeNormal();
		Up = Site;
	}
	Camera->SetActorLocationAndRotation(FVector(Eye), FRotationMatrix::MakeFromXZ(FVector(Forward), FVector(Up)).Rotator());

	Clock += DeltaSeconds;
	if (Clock < AuroraSettleSeconds)
	{
		return;
	}
	FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			FString::Printf(TEXT("aurora-%d-%s.png"), Step, Now.What))), false, false);

	Seen[Step] = Model(Site, When);
	const ULedgerAuroraView* View = World->GetSubsystem<ULedgerAuroraView>();
	const FLedgerAurora Drawn = View != nullptr ? View->Drawn() : FLedgerAurora();
	const FString Line = FString::Printf(
		TEXT("%-13s Kp %.1f%s: overhead at the site %.2f, oval %.1f degrees magnetic, drawn at strength %.2f"),
		Now.What, Seen[Step].Kp, Seen[Step].bEvent ? TEXT(" (event)") : TEXT(""), Seen[Step].Overhead,
		FMath::RadiansToDegrees(Seen[Step].OvalMagneticLatitude), Drawn.Strength);
	UE_LOG(LogLedger, Log, TEXT("aurora watch: %s"), *Line);
	Lines.Add(Line);

	++Step;
	Clock = 0.0;
	if (Step >= UE_ARRAY_COUNT(AuroraSteps))
	{
		bRunning = false;
		Finish();
	}
}

void ULedgerAuroraWatch::Finish()
{
	const FLedgerAurora Low = Model(LowSite, EventSeconds);
	const bool bHigh = Seen[0].Overhead > 0.3;
	const bool bQuiet = Seen[1].Overhead == 0.0 && !Seen[1].bEvent;
	const bool bNotLow = Low.Overhead < 1.0e-3;
	Lines.Add(FString::Printf(TEXT("the same event at 15 degrees magnetic: overhead %.4f"), Low.Overhead));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("aurora at high latitude during the event: %s"), bHigh ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("none on a quiet night: %s"), bQuiet ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("none at low latitude in the same event: %s"), bNotLow ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bHigh && bQuiet && bNotLow ? TEXT("PASS") : TEXT("FAIL")));
	for (int32 Index = Lines.Num() - 6; Index < Lines.Num(); ++Index)
	{
		UE_LOG(LogLedger, Log, TEXT("aurora watch: %s"), *Lines[Index]);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("aurora.txt"))));
	FPlatformMisc::RequestExit(false);
}
