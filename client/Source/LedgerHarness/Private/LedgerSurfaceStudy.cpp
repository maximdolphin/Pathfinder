// Photographs the ground from standing height, from a truck, and from a hill.

#include "LedgerSurfaceStudy.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	// Three distances and two lighting conditions, which is what the gate asks
	// for. The distances are chosen to be the ones a person actually occupies:
	// standing on it, looking across a field, and looking at a hillside. A
	// surface that only survives one of those is not a surface, it is a decal.
	//
	// The two sun angles matter more than they sound. A high sun flattens
	// everything and hides a normal map completely; a low one rakes across the
	// surface and is the only condition in which height and normal detail can
	// be judged at all. Photograph only at noon and you cannot tell a normal
	// map from a painted-on one.
	constexpr double SettleSeconds = 4.0;

	/// One photograph: where the camera stands, what it looks at, where the sun is.
	struct FStudyShot
	{
		const TCHAR* Name;
		double EyeHeightMetres;
		double LookAheadMetres;
		double SunElevationDegrees;
	};

	const FStudyShot StudyShots[] =
	{
		{ TEXT("surface-boots-low.png"),    1.7,     6.0,  8.0 },
		{ TEXT("surface-boots-high.png"),   1.7,     6.0, 62.0 },
		{ TEXT("surface-field-low.png"),   12.0,    90.0,  8.0 },
		{ TEXT("surface-field-high.png"),  12.0,    90.0, 62.0 },
		{ TEXT("surface-hill-low.png"),   140.0,  1400.0,  8.0 },
		{ TEXT("surface-hill-high.png"),  140.0,  1400.0, 62.0 },
	};
}

bool ULedgerSurfaceStudy::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerSurfaceStudy::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!FParse::Param(FCommandLine::Get(), TEXT("surfacestudy")))
	{
		return;
	}

	bRunning = true;
	UE_LOG(LogLedger, Log, TEXT("surface study: %d shots, %.0f s settle each"),
		UE_ARRAY_COUNT(StudyShots), SettleSeconds);
}

void ULedgerSurfaceStudy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Planet == nullptr || Controller == nullptr || Controller->GetPawn() == nullptr)
	{
		return;
	}

	if (Index >= UE_ARRAY_COUNT(StudyShots))
	{
		UE_LOG(LogLedger, Log, TEXT("surface study: done"));
		bRunning = false;
		FGenericPlatformMisc::RequestExit(false);
		return;
	}

	// Re-place every frame. The ship is still a physics actor and will settle,
	// drift or fall; a fixture that places once and photographs four seconds
	// later is photographing wherever it ended up.
	Place();

	Settle += DeltaSeconds;
	if (Settle < SettleSeconds)
	{
		return;
	}

	if (!bCaptured)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString(StudyShots[Index].Name)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogLedger, Log, TEXT("surface study -> %s"), *Path);
		bCaptured = true;
		return;
	}

	// One frame after the request, so the screenshot is actually written before
	// the camera moves out from under it.
	++Index;
	Settle = 0.0;
	bCaptured = false;
}

void ULedgerSurfaceStudy::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder->GetPlanet();
	APlayerController* Controller = World->GetFirstPlayerController();
	APawn* Pawn = Controller->GetPawn();

	const FStudyShot& Current = StudyShots[Index];

	// Near the town site, because that is the ground the rest of the project's
	// captures are of — but not *at* it. The site is the settlement, and a
	// camera at eye height there stands inside one of its thirty-two buildings
	// and photographs the inside of a wall. That looks exactly like broken
	// lighting and cost an hour of chasing the sun before the picture was read
	// properly.
	const FVector3d Site = Builder->GetSiteDirection().GetSafeNormal();
	FVector3d Sideways = FVector3d::CrossProduct(Site, FVector3d::UpVector);
	if (Sideways.IsNearlyZero())
	{
		Sideways = FVector3d::CrossProduct(Site, FVector3d::ForwardVector);
	}
	Sideways.Normalize();

	// Roughly a kilometre along the surface, which clears the settlement and
	// its trees without leaving the biome the rest of the captures are of.
	const double Offset = 100000.0 / Planet->Radius;
	const FVector3d Up = (Site + Sideways * Offset).GetSafeNormal();
	const double Ground = Planet->SurfaceRadiusAt(Up);

	FVector3d East = FVector3d::CrossProduct(Up, FVector3d::UpVector);
	if (East.IsNearlyZero())
	{
		East = FVector3d::CrossProduct(Up, FVector3d::ForwardVector);
	}
	East.Normalize();
	const FVector3d North = FVector3d::CrossProduct(East, Up).GetSafeNormal();

	const FVector Eye = FVector(Planet->GetActorLocation())
		+ FVector(Up * (Ground + Current.EyeHeightMetres * 100.0));

	// Look at a point on the ground the stated distance away, so the framing is
	// the same shot at three scales rather than three different pictures.
	const FVector Target = FVector(Planet->GetActorLocation())
		+ FVector((Up * Ground) + North * (Current.LookAheadMetres * 100.0));

	// A camera actor, not the ship. The ship's camera is on a boom behind it,
	// so putting the *ship* at eye height puts the camera underground and
	// photographs the inside of the terrain — which looks exactly like a
	// lighting bug and is not one.
	if (ALedgerShip* Ship = Cast<ALedgerShip>(Pawn))
	{
		Ship->SetFlightEnabled(false);
		// Out of shot. It is not the subject and its hull would fill the frame.
		Ship->SetActorLocation(Eye + FVector(Up * 400000.0));
	}

	const FRotator Look = (Target - Eye).Rotation();

	if (Camera == nullptr)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, Look, Params);
		if (Camera != nullptr)
		{
			// Fixed exposure, pinned by clamping the auto range to a single
			// value. Not a workaround — a requirement. This study photographs
			// the same ground under a raking sun and a high one, and auto
			// exposure exists precisely to cancel differences in scene
			// brightness. Leave it on and the two lighting conditions come back
			// looking equally bright, which is the one thing the comparison is
			// supposed to show.
			if (UCameraComponent* Component = Camera->GetCameraComponent())
			{
				FPostProcessSettings& Post = Component->PostProcessSettings;
				Post.bOverride_AutoExposureMinBrightness = true;
				Post.bOverride_AutoExposureMaxBrightness = true;
				Post.AutoExposureMinBrightness = 1.0f;
				Post.AutoExposureMaxBrightness = 1.0f;
				// Zero. AutoExposureBias is in *stops*, not lux: the sun's
				// intensity happens to be 11 and putting 11 here was +11 stops,
				// which is two thousand times over and clips the entire frame
				// to white. Every image from that run looked like fog because
				// it was fog-coloured clipping.
				Post.bOverride_AutoExposureBias = true;
				Post.AutoExposureBias = 0.0f;
			}
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}

	// The sun, raked or overhead. Rotating the existing light rather than
	// spawning one, so the sky and the atmosphere agree with it.
	// Off by default. The whole world came back unlit -- terrain, trees and
	// all -- the moment this rotated the sun, and the flight, which does not
	// touch it, is lit correctly. So the two lighting conditions are opt-in
	// until that is understood, rather than silently producing six black
	// photographs and calling it a study.
	if (!FParse::Param(FCommandLine::Get(), TEXT("studysun")))
	{
		return;
	}

	int32 Lights = 0;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		++Lights;
		const FVector3d Direction =
			(Up * FMath::Sin(FMath::DegreesToRadians(Current.SunElevationDegrees))
			 - North * FMath::Cos(FMath::DegreesToRadians(Current.SunElevationDegrees)))
			.GetSafeNormal();
		It->SetActorRotation((-FVector(Direction)).Rotation());

		if (!bCaptured && Settle == 0.0)
		{
			const FVector Forward = It->GetActorForwardVector();
			UE_LOG(LogLedger, Log,
				TEXT("study %s: sun elev %.0f, dir dot up %.3f, light forward dot up %.3f, "
				     "intensity %.1f, eye %.1f m up"),
				Current.Name, Current.SunElevationDegrees,
				FVector3d::DotProduct(Direction, Up),
				FVector::DotProduct(Forward, FVector(Up)),
				It->GetLightComponent() ? It->GetLightComponent()->Intensity : -1.0f,
				Current.EyeHeightMetres);
		}
		break;
	}
	if (Lights == 0 && !bCaptured && Settle == 0.0)
	{
		UE_LOG(LogLedger, Error, TEXT("study: no directional light in the world"));
	}
}

TStatId ULedgerSurfaceStudy::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerSurfaceStudy, STATGROUP_Tickables);
}
