#include "LedgerMoonShot.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
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
	constexpr int32 MoonShotSteps = 8;
	constexpr double MoonShotFirstSettle = 20.0;
	constexpr double MoonShotStepSettle = 1.5;

	/// Narrow, because a moon is half a degree across and a 90 degree frame
	/// spends four pixels on it. Ten degrees puts it at about a fifteenth of
	/// the frame, which is a disc a person can see the shape of.
	constexpr float MoonShotFieldOfView = 10.0f;

	/// Eye height above the ground, metres.
	constexpr double MoonShotEyeMetres = 40.0;
}

bool ULedgerMoonShot::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerMoonShot::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerMoonShot, STATGROUP_Tickables);
}

void ULedgerMoonShot::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("moonshot"));
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

	// The largest disc that is not the star. On a system with several moons
	// this picks the one worth photographing rather than the first one listed.
	TArray<FLedgerSkyBody> Seen;
	LedgerSky::VisibleBodies(System, Builder->GetHomeBodyIndex(), Anchor, 0.0, Seen);
	double Largest = 0.0;
	for (const FLedgerSkyBody& Body : Seen)
	{
		if (Body.BodyIndex != 0 && Body.AngularRadiusRadians > Largest)
		{
			Largest = Body.AngularRadiusRadians;
			Target = Body.BodyIndex;
		}
	}
	if (Target == INDEX_NONE)
	{
		UE_LOG(LogLedger, Warning, TEXT("moon shot: nothing in the sky but the star"));
		bRunning = false;
		return;
	}
	MonthSeconds = FMath::Abs(System.Bodies[Target].RotationPeriodSeconds);
}

void ULedgerMoonShot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f || !(MonthSeconds > 0.0))
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

	Settle += DeltaSeconds;
	if (Settle < (Step == 0 && !bAimed ? MoonShotFirstSettle : MoonShotStepSettle))
	{
		return;
	}

	// Second half of a step: the clock was moved last time round, the sky has
	// been placed for it since, and now it can be photographed.
	if (bAimed)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("moon-%d.png"), Step)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		++Step;
		bAimed = false;
		Settle = 0.0;
		return;
	}

	if (Step >= MoonShotSteps)
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	// Around the month, and at each point around it, forward to a moment when
	// the target is actually above the horizon. A phase nobody can see is a
	// phase nobody can check.
	const double Around = MonthSeconds * Step / MoonShotSteps;
	const double Day = LedgerSky::SolarDaySeconds(
		System, Builder->GetHomeBodyIndex(), Anchor, Around);
	double At = Around;
	double BestAltitude = -1.0;
	for (int32 Probe = 0; Probe < 96; ++Probe)
	{
		const double When = Around + Day * Probe / 96.0;
		TArray<FLedgerSkyBody> Seen;
		LedgerSky::VisibleBodies(System, Builder->GetHomeBodyIndex(), Anchor, When, Seen);
		const FLedgerSkyBody* Body = Seen.FindByPredicate(
			[this](const FLedgerSkyBody& B) { return B.BodyIndex == Target; });
		if (Body != nullptr && Body->DirectionInSurface.Z > BestAltitude)
		{
			BestAltitude = Body->DirectionInSurface.Z;
			At = When;
		}
	}

	Builder->SetWhenSeconds(At);

	TArray<FLedgerSkyBody> Seen;
	LedgerSky::VisibleBodies(System, Builder->GetHomeBodyIndex(), Anchor, At, Seen);
	const FLedgerSkyBody* Body = Seen.FindByPredicate(
		[this](const FLedgerSkyBody& B) { return B.BodyIndex == Target; });
	if (Body == nullptr)
	{
		++Step;
		Settle = 0.0;
		return;
	}

	// Stand somewhere, look at it.
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Anchor);
	const FVector Eye = FVector(Centre + Anchor * (Ground + MoonShotEyeMetres * 100.0));

	FLedgerSurfacePoint Local;
	Local.BodyIndex = Builder->GetHomeBodyIndex();
	Local.AnchorDirection = Anchor;
	Local.Metres = Body->DirectionInSurface;
	const FVector3d Toward = LedgerFrames::ToBody(Local).Metres.GetSafeNormal();

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye);
	}

	// **Not MakeFromXZ against local up.** The step below picks the moment the
	// body is HIGHEST, which puts it within a few degrees of the zenith -- and
	// there the look direction and local up are the same vector, so the basis
	// is degenerate and the camera points somewhere arbitrary. Eight frames of
	// empty blue sky, with the log insisting the moon was at +85 degrees.
	//
	// Fall back to east for the roll reference when the target is overhead. Any
	// stable perpendicular will do; what matters is that it is perpendicular.
	FLedgerSurfacePoint EastPoint;
	EastPoint.AnchorDirection = Anchor;
	EastPoint.Metres = FVector3d(1.0, 0.0, 0.0);
	const FVector3d East = LedgerFrames::ToBody(EastPoint).Metres.GetSafeNormal();
	const bool bOverhead = FMath::Abs(FVector3d::DotProduct(Toward, Anchor)) > 0.95;
	const FRotator Look = FRotationMatrix::MakeFromXZ(
		FVector(Toward), FVector(bOverhead ? East : Anchor)).Rotator();
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Eye, Look, SpawnParams);
		if (Camera != nullptr)
		{
			Camera->GetCameraComponent()->SetFieldOfView(MoonShotFieldOfView);
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}

	LitFraction.Add(Body->IlluminatedFraction);
	AltitudeDegrees.Add(FMath::RadiansToDegrees(
		FMath::Asin(FMath::Clamp(Body->DirectionInSurface.Z, -1.0, 1.0))));
	AngularDiameterDegrees.Add(
		FMath::RadiansToDegrees(Body->AngularRadiusRadians * 2.0));

	UE_LOG(LogLedger, Log,
		TEXT("moon shot %d/%d: t=%.0f s, %.1f%% lit, %.2f deg across, altitude %+.1f deg, "
			 "camera at %s looking %s"),
		Step, MoonShotSteps, At, Body->IlluminatedFraction * 100.0,
		FMath::RadiansToDegrees(Body->AngularRadiusRadians * 2.0),
		AltitudeDegrees.Last(), *Eye.ToCompactString(), *Look.Vector().ToCompactString());

	bAimed = true;
	Settle = 0.0;
}

void ULedgerMoonShot::Report()
{
	FString Body;
	Body += TEXT("A moon photographed around its month (T074).\n\n");
	Body += FString::Printf(TEXT("body            %d\n"), Target);
	Body += FString::Printf(TEXT("month           %.0f s (%.2f days of %.2f h)\n"),
		MonthSeconds, MonthSeconds / 86400.0, 24.0);
	Body += FString::Printf(TEXT("field of view   %.0f degrees\n\n"), MoonShotFieldOfView);

	Body += TEXT("step   lit      across      altitude   image\n");
	double Brightest = 0.0;
	double Darkest = 1.0;
	for (int32 Index = 0; Index < LitFraction.Num(); ++Index)
	{
		Body += FString::Printf(TEXT("%4d  %5.1f%%   %.3f deg   %+7.1f deg   moon-%d.png\n"),
			Index, LitFraction[Index] * 100.0, AngularDiameterDegrees[Index],
			AltitudeDegrees[Index], Index);
		Brightest = FMath::Max(Brightest, LitFraction[Index]);
		Darkest = FMath::Min(Darkest, LitFraction[Index]);
	}
	Body += FString::Printf(
		TEXT("\nlit fraction runs from %.1f%% to %.1f%% around the month.\n"),
		Darkest * 100.0, Brightest * 100.0);
	Body += TEXT(
		"\nNothing here draws a phase. The body is a sphere in the sky lit by the same\n"
		"directional light as the ground, so the terminator is where the geometry puts\n"
		"it and there is no second implementation to disagree with LedgerSky.\n");

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("moon-shot.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("moon shot: %.1f%% to %.1f%% lit across the month"),
		Darkest * 100.0, Brightest * 100.0);
}
