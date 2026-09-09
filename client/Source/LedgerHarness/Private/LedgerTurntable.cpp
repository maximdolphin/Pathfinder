// Eight angles, three framings, two rigs, on a stage that renders the same way
// twice.

#include "LedgerTurntable.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
	/// How far the backdrop sits from the subject, as a multiple of its radius.
	/// It has to clear the widest framing, which is four.
	constexpr double BackdropScale = 8.0;

	/// Frames spent settling before each shot. Lumen's screen probes accumulate
	/// over frames, so this is a count and not a duration: the same count from
	/// the same state converges to the same image, where the same *time* does
	/// not because it is a different number of frames on a different machine.
	///
	/// Twenty-four was not enough. Every camera move invalidates the probes and
	/// they reconverge over the frames that follow, so two runs disagreed on a
	/// handful of shots by a mean of 0.06 of a channel -- invisible, and still
	/// a failed byte-comparison. The whole point of this fixture is that a diff
	/// means a change, so the settle is long enough that a diff means one.
	constexpr int32 FramesToSettle = 64;

	/// Frames spent composing the first shot before anything is captured.
	/// Without it the first framing's eight images differed between runs by a
	/// mean of 0.06 of a channel and never more than 9 -- Lumen still
	/// converging from a cold start, not nondeterminism, but enough to fail a
	/// byte-identical check on eleven of forty-eight files. The rest of the
	/// run was already identical, which is what said it was warm-up.
	constexpr int32 FramesToWarmUp = 150;

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
	// The subject is not moved. An earlier version carried it 200,000 km out to
	// a spot with nothing else in it, on the reasoning that the planet's clouds
	// and sun animate and an animating background cannot be diffed. Everything
	// about that placement measured correct -- subject there, camera 152 m away
	// pointing at it, view target right, backdrop and both lights spawned -- and
	// all forty-eight frames came back black. Rather than keep theorising about
	// why nothing rendered at a coordinate nothing else in the project has ever
	// rendered at, the stage is built where the ship already is, which the
	// scripted flight demonstrably draws. The backdrop is what hides the world,
	// and that is its job either way.
	Subject->SetActorRotation(FRotator::ZeroRotator);

	// `false` -- include components that do not collide. The first version
	// passed `true`, which counts only colliding components, and the hull is a
	// procedural mesh with collision off. The bounds came back empty, the radius
	// fell to its own one-metre floor, and every framing was computed from that.
	// Worse, the camera-outside check used the same empty bounds and passed
	// forty-eight times out of forty-eight: a measurement checked against itself
	// agrees with itself, and all forty-eight images were black.
	// Bounds over the components that actually draw something, unioned by hand.
	// GetActorBounds includes the whole actor, and this pawn carries a 52 m
	// camera boom -- so it reported a 33 m subject for a hull a few metres
	// across, and every framing was computed three times too far out. The ship
	// came back as a speck in the middle of the frame and the fixture had no
	// way to notice.
	FBox Box(ForceInit);
	Subject->ForEachComponent<UMeshComponent>(true, [&Box](const UMeshComponent* Component)
	{
		if (Component->IsRegistered() && Component->IsVisible())
		{
			Box += Component->Bounds.GetBox();
		}
	});

	if (!Box.IsValid)
	{
		UE_LOG(LogLedger, Error,
			TEXT("turntable: the subject has no visible mesh component. There is "
			     "nothing to photograph, and a contact sheet of nothing is still 48 files."));
		return false;
	}

	StageOrigin = Box.GetCenter();
	SubjectExtent = Box.GetExtent();
	SubjectRadius = SubjectExtent.Size();

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;

	// A backdrop, so that "silhouette" framing shows a silhouette. A dark hull
	// against empty space is a picture of nothing, and it is exactly the kind
	// of image that gets nodded at.
	//
	// A plane held behind the subject and turned to face the camera, not a
	// sphere around the stage. The sphere was scaled negative to turn it inside
	// out, which does not do what it looks like it does -- it never rendered,
	// and the planet was visible straight through where it should have been.
	// It also enclosed the whole stage and cast a shadow over it.
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		Backdrop = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), StageOrigin, FRotator::ZeroRotator, Params);
		if (Backdrop != nullptr && Backdrop->GetStaticMeshComponent() != nullptr)
		{
			UStaticMeshComponent* Mesh = Backdrop->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Plane);
			// The engine plane is 100 cm square, and it has to cover the frame
			// at the widest framing with room to spare.
			//
			// The arithmetic: the camera sits at 4 radii, the plane at 8, so
			// they are 12 apart, and a 90 degree horizontal field of view needs
			// a half-width equal to that distance -- 24 radii across. The first
			// version was exactly 24, which is exactly wrong: the frame's
			// corners reach 15% further than its sides, so at two of the eight
			// yaw angles the planet showed past the edge, and the planet streams
			// terrain, which does not render the same way twice. Double it and
			// stop thinking about it.
			const double Scale = (SubjectRadius * BackdropScale * 6.0) / 100.0;
			Backdrop->SetActorScale3D(FVector(Scale, Scale, Scale));
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			// It must not cast a shadow onto its own subject.
			Mesh->SetCastShadow(false);
			if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
			{
				if (UMaterialInstanceDynamic* Grey =
					UMaterialInstanceDynamic::Create(Base, Backdrop))
				{
					Grey->SetVectorParameterValue(
						TEXT("Color"), FLinearColor(0.16f, 0.17f, 0.19f));
					Mesh->SetMaterial(0, Grey);
				}
			}
			StageActors.Add(Backdrop);
		}
	}

	// The world's own sun is switched off for the duration. It is not one of
	// this fixture's lights, and leaving it on produced the warning drawn
	// across the viewport: multiple directional lights competing to be the one
	// used for forward shading, resolved by picking whichever is brightest --
	// which is not a thing a review fixture should leave to a tie-break.
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		if (UDirectionalLightComponent* Component =
			Cast<UDirectionalLightComponent>(It->GetLightComponent()))
		{
			WorldSunIntensity = Component->Intensity;
			WorldSun = *It;
			Component->SetIntensity(0.0f);
		}
		break;
	}

	// Two lights, with an explicit priority so that the choice of which one
	// drives forward shading is stated rather than inferred from brightness.
	for (int32 Which = 0; Which < 2; ++Which)
	{
		if (ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(
			ADirectionalLight::StaticClass(), StageOrigin, FRotator::ZeroRotator, Params))
		{
			Light->SetMobility(EComponentMobility::Movable);
			if (UDirectionalLightComponent* Component =
				Cast<UDirectionalLightComponent>(Light->GetLightComponent()))
			{
				Component->ForwardShadingPriority = Which == 0 ? 10 : 0;
				Component->SetAtmosphereSunLight(false);
			}
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

	// Everything that has to be true for a shot to contain anything, printed
	// once. Forty-eight black images passed the only check this fixture had,
	// because that check compared the bounds against themselves; a picture of
	// nothing is not distinguishable from a picture of something by any
	// property of the camera alone.
	int32 Backdrops = 0;
	int32 Lights = 0;
	for (AActor* Actor : StageActors)
	{
		Backdrops += Cast<AStaticMeshActor>(Actor) != nullptr ? 1 : 0;
		Lights += Cast<ADirectionalLight>(Actor) != nullptr ? 1 : 0;
	}

	UE_LOG(LogLedger, Log,
		TEXT("turntable: subject extent (%.0f, %.0f, %.0f) cm, radius %.0f cm, "
		     "framings %.0f / %.0f / %.0f cm"),
		SubjectExtent.X, SubjectExtent.Y, SubjectExtent.Z, SubjectRadius,
		SubjectRadius * Framings[0], SubjectRadius * Framings[1],
		SubjectRadius * Framings[2]);
	UE_LOG(LogLedger, Log,
		TEXT("turntable: subject at %s, bounds centre %s"),
		*Subject->GetActorLocation().ToCompactString(),
		*StageOrigin.ToCompactString());
	UE_LOG(LogLedger, Log,
		TEXT("turntable: %d backdrop, %d lights, view target %s"),
		Backdrops, Lights,
		Controller->GetViewTarget() == Camera ? TEXT("is the turntable camera")
		                                      : TEXT("IS NOT the turntable camera"));
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

	// The backdrop follows, always square to the camera and always behind the
	// subject. The engine plane's face points along +Z, so the rotation is
	// built from that axis rather than from a forward vector.
	if (Backdrop != nullptr)
	{
		const FVector TowardCamera = (Eye - StageOrigin).GetSafeNormal();
		Backdrop->SetActorLocation(StageOrigin - TowardCamera * (SubjectRadius * BackdropScale));
		Backdrop->SetActorRotation(FRotationMatrix::MakeFromZ(TowardCamera).Rotator());
	}

	if (Index == 0 && SettleFrames == 0)
	{
		UE_LOG(LogLedger, Log,
			TEXT("turntable shot 0: camera %s, looking at %s, %.0f cm away"),
			*Eye.ToCompactString(), *StageOrigin.ToCompactString(),
			FVector::Distance(Eye, StageOrigin));
	}
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

	if (WarmUpFrames < FramesToWarmUp)
	{
		++WarmUpFrames;
		return;
	}

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

	if (WorldSun != nullptr)
	{
		if (UDirectionalLightComponent* Component =
			Cast<UDirectionalLightComponent>(WorldSun->GetLightComponent()))
		{
			Component->SetIntensity(WorldSunIntensity);
		}
	}

	FString Body;
	Body += TEXT("Turntable.\n\n");
	Body += FString::Printf(TEXT("  shots            %d\n"), ShotCount);
	Body += FString::Printf(TEXT("  subject extent   %.0f x %.0f x %.0f cm\n"),
		SubjectExtent.X, SubjectExtent.Y, SubjectExtent.Z);
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
