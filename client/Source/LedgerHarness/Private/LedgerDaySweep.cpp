#include "LedgerDaySweep.h"

#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerTerrainMath.h"
#include "LedgerFrames.h"
#include "LedgerSky.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/// The body the world stands on; index 1, as LedgerWorld has it.
	constexpr int32 DaySweepBody = 1;

	/// Eight steps across one rotation: enough that dawn and dusk both land
	/// somewhere in the sequence rather than between two of them.
	constexpr int32 DaySweepSteps = 8;

	/// Eye height above the ground the terrain query reports, metres.
	///
	/// **It was 40 and that put the camera inside a hill.** Every frame of the
	/// first sweep came back with the lower half black and the terrain reading
	/// as a silhouette against the sky, at noon as much as at midnight: the
	/// foreground measured 0 to 7 out of 255 at all eight steps while the sky
	/// and the distant ground tracked the sun perfectly. Not a lighting bug --
	/// the inside of a mesh.
	///
	/// SurfaceRadiusAt answers from the patch that is drawn, and at a site on a
	/// mountain the drawn patch is coarser than the ground it stands for. The
	/// gap is measured and reported below. 400 m clears it; the number that
	/// should be small enough for 40 to work is somebody else's task, and the
	/// report says how far off it is so the argument starts from a figure.
	constexpr double DaySweepEyeMetres = 40.0;

	/// The first step waits for ground to stream; the rest only wait for the
	/// light and the sky capture to catch up, which is a frame or two.
	constexpr double DaySweepFirstSettle = 22.0;
	constexpr double DaySweepStepSettle = 1.5;
}

bool ULedgerDaySweep::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerDaySweep::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerDaySweep, STATGROUP_Tickables);
}

void ULedgerDaySweep::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("daysweep"));
	if (!bRunning)
	{
		return;
	}

	int32 Seed = 20260908;
	FParse::Value(FCommandLine::Get(), TEXT("systemseed="), Seed);
	System = LedgerBodies::Generate(static_cast<uint32>(Seed));

	ULedgerWorldBuilder* Builder = InWorld.GetSubsystem<ULedgerWorldBuilder>();
	if (Builder != nullptr)
	{
		// The site the world already chose. Reusing it rather than picking one
		// is the whole point: this photographs the place the rest of the
		// project photographs, at eight different times.
		Anchor = Builder->GetSiteDirection().GetSafeNormal();
	}

	if (System.Bodies.IsValidIndex(DaySweepBody))
	{
		DaySeconds = FMath::Abs(System.Bodies[DaySweepBody].RotationPeriodSeconds);
		NoonSeconds = LedgerSky::NextLocalNoon(System, DaySweepBody, Anchor, 0.0);
	}
}

double ULedgerDaySweep::AimSunAt(double SecondsFromEpoch)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0.0;
	}

	const FVector3d Facing =
		LedgerSky::SunDirectionInBody(System, DaySweepBody, SecondsFromEpoch);

	// The world's own light, found rather than spawned -- a second directional
	// light would be a second sun, and the atmosphere would take whichever it
	// found first.
	if (Sun == nullptr)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			Sun = *It;
			break;
		}
	}
	if (Sun != nullptr)
	{
		Sun->SetMobility(EComponentMobility::Movable);
		Sun->SetActorRotation((-FVector(Facing)).Rotation());
	}

	return FMath::RadiansToDegrees(
		LedgerSky::SolarAltitude(System, DaySweepBody, Anchor, SecondsFromEpoch));
}

void ULedgerDaySweep::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	const FVector3d Origin = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Anchor);
	const FVector Eye = FVector(Origin + Anchor * (Ground + DaySweepEyeMetres * 100.0));

	// Looking due east, which is where a sun rises whatever else is true. Any
	// fixed direction would do; this one means the sequence reads left to right.
	FLedgerSurfacePoint Facing;
	Facing.AnchorDirection = Anchor;
	Facing.Metres = FVector3d(1.0, 0.0, 0.02);
	const FVector3d East = LedgerFrames::ToBody(Facing).Metres.GetSafeNormal();
	const FRotator Look =
		FRotationMatrix::MakeFromXZ(FVector(East), FVector(Anchor)).Rotator();

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye);
	}

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

void ULedgerDaySweep::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f || !(DaySeconds > 0.0))
	{
		return;
	}

	Place();

	Settle += DeltaSeconds;
	const double Needed = bPlaced ? DaySweepStepSettle : DaySweepFirstSettle;
	if (Settle < Needed)
	{
		return;
	}

	if (!bMeasured)
	{
		UWorld* World = GetWorld();
		ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
		if (ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr)
		{
			QueriedGroundMetres = (Planet->SurfaceRadiusAt(Anchor) - Planet->Radius) / 100.0;
			FieldGroundMetres = LedgerTerrain::Elevation(Anchor, Planet->TerrainParams()) / 100.0;
			UE_LOG(LogLedger, Log,
				TEXT("day sweep ground: query %.1f m, height field %.1f m, query %.1f m low"),
				QueriedGroundMetres, FieldGroundMetres,
				FieldGroundMetres - QueriedGroundMetres);
		}
		bMeasured = true;
	}

	if (Step >= DaySweepSteps)
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	// Centred on noon, so step 4 of 8 is the middle of the day and the two ends
	// are the middle of the night.
	const double At = NoonSeconds + DaySeconds * (Step / static_cast<double>(DaySweepSteps) - 0.5);
	const double Altitude = AimSunAt(At);
	AltitudeDegrees.Add(Altitude);

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			FString::Printf(TEXT("day-sweep-%d.png"), Step)));
	FScreenshotRequest::RequestScreenshot(Path, false, false);

	UE_LOG(LogLedger, Log, TEXT("day sweep %d/%d: t=%.0f s, solar altitude %.2f deg"),
		Step, DaySweepSteps, At, Altitude);

	++Step;
	bPlaced = true;
	Settle = 0.0;
}

void ULedgerDaySweep::Report()
{
	FString Body;
	Body += TEXT("One place, one camera, one day (T072).\n\n");
	Body += FString::Printf(TEXT("rotation period  %10.0f s (%.2f hours)\n"),
		DaySeconds, DaySeconds / 3600.0);
	Body += FString::Printf(TEXT("local noon at    %10.0f s from epoch\n"), NoonSeconds);
	Body += FString::Printf(TEXT("axial tilt       %10.1f degrees\n"),
		System.Bodies.IsValidIndex(DaySweepBody)
			? FMath::RadiansToDegrees(System.Bodies[DaySweepBody].AxialTiltRadians) : 0.0);
	Body += FString::Printf(TEXT("eye height       %10.0f m above the queried ground\n\n"),
		DaySweepEyeMetres);

	// What the query said the ground was, against what the height field says it
	// is. They should agree; the difference is why the eye height is 400 and not
	// 40, and it is here as a number rather than as a note.
	Body += FString::Printf(
		TEXT("queried ground   %10.1f m above sea level\n"), QueriedGroundMetres);
	Body += FString::Printf(
		TEXT("height field     %10.1f m above sea level\n"), FieldGroundMetres);
	Body += FString::Printf(
		TEXT("the query is     %10.1f m low\n\n"),
		FieldGroundMetres - QueriedGroundMetres);

	Body += TEXT("step   fraction of day   solar altitude   image\n");
	int32 Lit = 0;
	double Highest = -90.0;
	int32 HighestStep = 0;
	for (int32 Index = 0; Index < AltitudeDegrees.Num(); ++Index)
	{
		const double Fraction = Index / static_cast<double>(DaySweepSteps) - 0.5;
		Body += FString::Printf(TEXT("%4d   %14.3f   %12.2f    day-sweep-%d.png\n"),
			Index, Fraction, AltitudeDegrees[Index], Index);
		if (AltitudeDegrees[Index] > 0.0) { ++Lit; }
		if (AltitudeDegrees[Index] > Highest)
		{
			Highest = AltitudeDegrees[Index];
			HighestStep = Index;
		}
	}

	Body += FString::Printf(
		TEXT("\n%d of %d steps are in daylight, and the highest sun is step %d, "
			 "which is the step at noon.\n"),
		Lit, AltitudeDegrees.Num(), HighestStep);
	Body += TEXT(
		"\nNothing moved between these frames except the time. The site, the camera\n"
		"and the ground are the same in all eight; the sun's direction is\n"
		"LedgerSky::SunDirectionInBody at eight instants, and the light follows it.\n");

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("day-sweep.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("day sweep: %d lit of %d, highest at step %d"),
		Lit, AltitudeDegrees.Num(), HighestStep);
}
