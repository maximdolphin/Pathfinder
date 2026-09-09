// Eight angles, three framings, two rigs, on a stage that renders the same way
// twice.

#include "LedgerTurntable.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerLog.h"
#include "LedgerShip.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/// Where the stage sits: far enough from the planet that the atmosphere
	/// renders nothing, the clouds are not in shot and no terrain streams. All
	/// three of those animate, and an animating background cannot be diffed.
	const FVector StagePosition(0.0, 0.0, 2.0e10);

	/// Frames spent settling before each shot. Lumen's screen probes accumulate
	/// over frames, so this is a count and not a duration: the same count from
	/// the same state converges to the same image, where the same *time* does
	/// not because it is a different number of frames on a different machine.
	constexpr int32 FramesToSettle = 24;

	/// Eight yaw angles. Not sixteen: the point is to catch a silhouette that
	/// only works from the front, and eight does that for a fraction of the
	/// wall-clock.
	constexpr int32 YawSteps = 8;

	/// Framings, as a multiple of the subject's bounding radius. Silhouette
	/// reads the shape, standing reads it the way a person meets it, and close
	/// reads the surface treatment -- which is where generated detail either
	/// holds up or turns into noise.
	const double Framings[] = { 4.0, 2.0, 0.9 };
	const TCHAR* FramingNames[] = { TEXT("silhouette"), TEXT("standing"), TEXT("close") };

	/// Two rigs. The first is a hard key with almost no fill, which is the only
	/// condition in which panel breaks and edge wear can be judged. The second
	/// is a soft, high, filled setup that flatters everything and is therefore
	/// the one that must not be looked at alone.
	struct FRig
	{
		const TCHAR* Name;
		FRotator Key;
		float KeyIntensity;
		FRotator Fill;
		float FillIntensity;
	};

	const FRig Rigs[] = {
		{ TEXT("raking"), FRotator(-12.0, 35.0, 0.0), 9.0f, FRotator(-40.0, -150.0, 0.0), 0.35f },
		{ TEXT("soft"),   FRotator(-55.0, 20.0, 0.0), 6.0f, FRotator(-25.0, -160.0, 0.0), 2.6f },
	};

	constexpr int32 ShotCount = YawSteps * 3 * 2;
}

bool ULedgerTurntable::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerTurntable::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!FParse::Param(FCommandLine::Get(), TEXT("turntable")))
	{
		return;
	}

	bRunning = true;
	UE_LOG(LogLedger, Log, TEXT("turntable: %d shots (%d yaw x 3 framings x 2 rigs), %d frames settle each"),
		ShotCount, YawSteps, FramesToSettle);
}

bool ULedgerTurntable::Stage()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Controller == nullptr || Controller->GetPawn() == nullptr)
	{
		return false;
	}

	// The ship, for now. When the generators land this becomes a lookup by
	// name; there is exactly one generated asset today and inventing the lookup
	// for it would be inventing it for assets whose shape is not decided.
	Subject = Controller->GetPawn();
	if (ALedgerShip* Ship = Cast<ALedgerShip>(Subject))
	{
		Ship->SetFlightEnabled(false);
	}
	Subject->SetActorLocation(StagePosition);
	Subject->SetActorRotation(FRotator::ZeroRotator);

	FVector Origin = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	Subject->GetActorBounds(true, Origin, Extent);
	StageOrigin = Origin;
	SubjectRadius = FMath::Max(100.0, Extent.Size());

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;

	// A backdrop, so that "silhouette" framing shows a silhouette. A dark hull
	// against empty space is a picture of nothing, and it is exactly the kind
	// of image that gets nodded at.
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		AStaticMeshActor* Backdrop = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), StageOrigin, FRotator::ZeroRotator, Params);
		if (Backdrop != nullptr && Backdrop->GetStaticMeshComponent() != nullptr)
		{
			UStaticMeshComponent* Mesh = Backdrop->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Sphere);
			// The engine sphere is 100 cm across, and it is scaled inside out so
			// the camera sits within it.
			const double Scale = SubjectRadius * 0.4;
			Backdrop->SetActorScale3D(FVector(-Scale, -Scale, -Scale));
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			// The engine's own material, not the project's. LedgerHarness
			// already has its three allowed module dependencies, and reaching
			// into LedgerMaterial for one grey backdrop would break the
			// layering rule to save four lines.
			if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
			{
				if (UMaterialInstanceDynamic* Grey =
					UMaterialInstanceDynamic::Create(Base, Backdrop))
				{
					Grey->SetVectorParameterValue(
						TEXT("Color"), FLinearColor(0.18f, 0.19f, 0.21f));
					Mesh->SetMaterial(0, Grey);
				}
			}
			StageActors.Add(Backdrop);
		}
	}

	// Two lights, both movable and both under this fixture's control. The
	// world's sun is left where it is; it is 20,000 km away and lighting a
	// planet, and rotating it is what unlit the entire world last time.
	for (int32 Which = 0; Which < 2; ++Which)
	{
		if (ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(
			ADirectionalLight::StaticClass(), StageOrigin, FRotator::ZeroRotator, Params))
		{
			Light->SetMobility(EComponentMobility::Movable);
			StageActors.Add(Light);
		}
	}

	Camera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), StageOrigin, FRotator::ZeroRotator, Params);
	if (Camera == nullptr)
	{
		return false;
	}
	if (UCameraComponent* Component = Camera->GetCameraComponent())
	{
		FPostProcessSettings& Post = Component->PostProcessSettings;
		// Exposure pinned, for the same reason as the surface study: this
		// fixture photographs one asset under two rigs, and auto exposure
		// exists to cancel exactly the difference being photographed.
		Post.bOverride_AutoExposureMinBrightness = true;
		Post.bOverride_AutoExposureMaxBrightness = true;
		Post.AutoExposureMinBrightness = 1.0f;
		Post.AutoExposureMaxBrightness = 1.0f;
		Post.bOverride_AutoExposureBias = true;
		Post.AutoExposureBias = 0.0f;
		// Motion blur and any temporal ghosting would smear one shot into the
		// next, which is both wrong and unreproducible.
		Post.bOverride_MotionBlurAmount = true;
		Post.MotionBlurAmount = 0.0f;
	}
	Controller->SetViewTarget(Camera);

	UE_LOG(LogLedger, Log, TEXT("turntable: subject radius %.0f cm, stage at %.0f km"),
		SubjectRadius, StagePosition.Z / 100000.0);
	return true;
}

void ULedgerTurntable::Compose()
{
	const int32 RigIndex = Index / (YawSteps * 3);
	const int32 Rest = Index % (YawSteps * 3);
	const int32 FramingIndex = Rest / YawSteps;
	const int32 YawIndex = Rest % YawSteps;

	const FRig& Rig = Rigs[RigIndex];
	int32 LightNumber = 0;
	for (AActor* Actor : StageActors)
	{
		if (ADirectionalLight* Light = Cast<ADirectionalLight>(Actor))
		{
			const bool bKey = LightNumber == 0;
			Light->SetActorRotation(bKey ? Rig.Key : Rig.Fill);
			if (UDirectionalLightComponent* Component =
				Cast<UDirectionalLightComponent>(Light->GetLightComponent()))
			{
				Component->SetIntensity(bKey ? Rig.KeyIntensity : Rig.FillIntensity);
			}
			++LightNumber;
		}
	}

	const double Yaw = 360.0 * YawIndex / YawSteps;
	const double Distance = SubjectRadius * Framings[FramingIndex];

	// Slightly above the subject's centre and looking down a little, which is
	// how a person meets a parked object and is not how an orthographic
	// turntable shows one. The difference matters: a shape that only works from
	// its own equator does not work.
	const FVector Offset = FRotator(0.0, Yaw, 0.0).RotateVector(
		FVector(-Distance, 0.0, Distance * 0.22));
	const FVector Eye = StageOrigin + Offset;

	Camera->SetActorLocationAndRotation(Eye, (StageOrigin - Eye).Rotation());
}

bool ULedgerTurntable::CameraIsOutside(FString& Why) const
{
	if (Camera == nullptr || Subject == nullptr)
	{
		Why = TEXT("no camera or no subject");
		return false;
	}

	FVector Origin = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	Subject->GetActorBounds(true, Origin, Extent);

	// Against the bounding box rather than the sphere: a long hull's bounding
	// sphere is mostly empty, and rejecting a camera that is comfortably beside
	// the ship would make this check something to be worked around.
	const FVector Local = (Camera->GetActorLocation() - Origin).GetAbs();
	if (Local.X <= Extent.X && Local.Y <= Extent.Y && Local.Z <= Extent.Z)
	{
		Why = FString::Printf(
			TEXT("camera is inside the subject's bounds (%.0f, %.0f, %.0f within %.0f, %.0f, %.0f)"),
			Local.X, Local.Y, Local.Z, Extent.X, Extent.Y, Extent.Z);
		return false;
	}
	return true;
}

void ULedgerTurntable::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRunning)
	{
		return;
	}

	if (!bStaged)
	{
		bStaged = Stage();
		return;
	}

	if (Index >= ShotCount)
	{
		Finish();
		return;
	}

	Compose();

	if (SettleFrames < FramesToSettle)
	{
		++SettleFrames;
		return;
	}

	if (!bCaptured)
	{
		FString Why;
		if (CameraIsOutside(Why))
		{
			++Outside;
		}
		else
		{
			++Inside;
			UE_LOG(LogLedger, Error, TEXT("turntable shot %d: %s"), Index, *Why);
		}

		const int32 RigIndex = Index / (YawSteps * 3);
		const int32 Rest = Index % (YawSteps * 3);
		const FString Name = FString::Printf(TEXT("%s-%s-%02d.png"),
			Rigs[RigIndex].Name, FramingNames[Rest / YawSteps], Rest % YawSteps);

		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				TEXT("turntable"), Name));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bCaptured = true;
		return;
	}

	// One frame after the request, so the file is written before the camera
	// moves out from under it.
	++Index;
	SettleFrames = 0;
	bCaptured = false;
}

void ULedgerTurntable::Finish()
{
	bRunning = false;

	FString Body;
	Body += TEXT("Turntable.\n\n");
	Body += FString::Printf(TEXT("  shots            %d\n"), ShotCount);
	Body += FString::Printf(TEXT("  subject radius   %.0f cm\n"), SubjectRadius);
	Body += FString::Printf(TEXT("  settle           %d frames each\n"), FramesToSettle);
	Body += FString::Printf(TEXT("  camera outside   %d\n"), Outside);
	Body += FString::Printf(TEXT("  camera inside    %d\n\n"), Inside);
	Body += FString::Printf(TEXT("VERDICT: %s\n"), Inside == 0
		? TEXT("PASS - every shot was taken from outside the subject")
		: TEXT("FAIL - at least one shot was taken from inside the subject"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("turntable.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);

	UE_LOG(LogLedger, Log, TEXT("turntable: %d shots, %d outside, %d inside -> %s"),
		ShotCount, Outside, Inside, *Path);

	FGenericPlatformMisc::RequestExit(false);
}

TStatId ULedgerTurntable::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerTurntable, STATGROUP_Tickables);
}
