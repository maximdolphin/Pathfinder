#include "LedgerEclipseWatch.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
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
	/// Minutes either side of greatest coverage. Wide enough to have ordinary
	/// daylight at both ends, so the middle has something to be darker than.
	const double EclipseOffsets[] = { -180.0, -45.0, -15.0, 0.0, 15.0, 45.0, 180.0 };
	constexpr int32 EclipseSteps = 7;

	constexpr double EclipseFirstSettle = 22.0;
	/// **Long enough for the eye to adjust, and this sequence needs it most.**
	///
	/// T076 made the sun a physical 101,367 lux, and this fixture walks from a
	/// total eclipse to full daylight -- the widest swing anywhere in the
	/// project. At 1.5 s a step the auto-exposure was still climbing out of
	/// totality when the shutter went, and the daylight frames came back washed
	/// to white: a photograph of the adaptation, not of the ground.
	constexpr double EclipseStepSettle = 12.0;
	constexpr double EclipseEyeMetres = 40.0;
}

bool ULedgerEclipseWatch::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerEclipseWatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerEclipseWatch, STATGROUP_Tickables);
}

void ULedgerEclipseWatch::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("eclipse"));
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

	// Ask the ephemeris when, before anything is drawn. That is the whole
	// shape of the acceptance: the time is a prediction, not a search for a
	// good-looking frame.
	const int32 Home = Builder->GetHomeBodyIndex();
	const double Year = LedgerEphemeris::PeriodSeconds(
		System.Bodies[0].MassKg, System.Bodies[Home].Orbit.SemiMajorAxisMetres);

	double Deepest = 0.0;
	double Cursor = 0.0;
	while (Cursor >= 0.0 && Cursor < Year * 2.0)
	{
		double Peak = 0.0;
		const double At = LedgerSky::NextEclipse(
			System, Home, Anchor, Cursor, Year * 2.0 - Cursor, Peak);
		if (At < 0.0)
		{
			break;
		}
		if (Peak > Deepest)
		{
			Deepest = Peak;
			PeakSeconds = At;
		}
		Cursor = At + 4.0 * 3600.0;
	}

	if (PeakSeconds < 0.0)
	{
		UE_LOG(LogLedger, Warning, TEXT("eclipse watch: none predicted at this site"));
		bRunning = false;
		return;
	}
	PeakCoverage = Deepest;
	What = LedgerSky::EclipsingBody(System, Home, Anchor, PeakSeconds);
	UE_LOG(LogLedger, Log,
		TEXT("eclipse watch: body %d covers %.1f%% at t = %.0f s; going to look"),
		What, PeakCoverage * 100.0, PeakSeconds);
}

void ULedgerEclipseWatch::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Anchor);
	const FVector Eye = FVector(Centre + Anchor * (Ground + EclipseEyeMetres * 100.0));

	// Looking at the ground rather than at the sky. The acceptance is that the
	// GROUND goes dark, and a frame of sky would answer a different question.
	FLedgerSurfacePoint Facing;
	Facing.AnchorDirection = Anchor;
	Facing.Metres = FVector3d(1.0, 0.0, -0.35);
	const FVector3d Toward = LedgerFrames::ToBody(Facing).Metres.GetSafeNormal();

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
}

void ULedgerEclipseWatch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f || PeakSeconds < 0.0)
	{
		return;
	}

	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr)
	{
		return;
	}

	Place();

	Settle += DeltaSeconds;
	if (Settle < (Step == 0 && !bAimed ? EclipseFirstSettle : EclipseStepSettle))
	{
		return;
	}

	// Aiming and capturing are two ticks: the clock is moved on one and the
	// frame taken on the next, so what is photographed is the sky that the
	// clock asked for rather than the one before it.
	if (bAimed)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("eclipse-%d.png"), Step)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		++Step;
		bAimed = false;
		Settle = 0.0;
		return;
	}

	if (Step >= EclipseSteps)
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	const double At = PeakSeconds + EclipseOffsets[Step] * 60.0;
	Builder->SetWhenSeconds(At);

	const double Covered = LedgerSky::StarCoveredFraction(
		System, Builder->GetHomeBodyIndex(), Anchor, At);
	OffsetsMinutes.Add(EclipseOffsets[Step]);
	Coverage.Add(Covered);

	UE_LOG(LogLedger, Log,
		TEXT("eclipse watch %d/%d: %+.0f min, %.1f%% covered, solar altitude %.1f deg"),
		Step, EclipseSteps, EclipseOffsets[Step], Covered * 100.0,
		FMath::RadiansToDegrees(LedgerSky::SolarAltitude(
			System, Builder->GetHomeBodyIndex(), Anchor, At)));

	bAimed = true;
	Settle = 0.0;
}

void ULedgerEclipseWatch::Report()
{
	FString Body;
	Body += TEXT("An eclipse the ephemeris predicted (T075).\n\n");
	Body += FString::Printf(TEXT("occulting body   %d\n"), What);
	Body += FString::Printf(TEXT("greatest cover   %.1f%%\n"), PeakCoverage * 100.0);
	Body += FString::Printf(TEXT("predicted at     %.0f s from epoch\n\n"), PeakSeconds);

	Body += TEXT("offset      covered   image\n");
	for (int32 Index = 0; Index < Coverage.Num(); ++Index)
	{
		Body += FString::Printf(TEXT("%+5.0f min    %6.1f%%   eclipse-%d.png\n"),
			OffsetsMinutes[Index], Coverage[Index] * 100.0, Index);
	}
	Body += TEXT(
		"\nThe light is not dimmed by an eclipse effect. It is the star's intensity\n"
		"times however much of its disc is still showing, so the ground darkens for\n"
		"the same reason and at the same moment the sky says it should.\n");

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			TEXT("eclipse-watch.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("eclipse watch: peak %.1f%% covered"),
		PeakCoverage * 100.0);
}
