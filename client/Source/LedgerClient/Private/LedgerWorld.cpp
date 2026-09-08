#include "LedgerWorld.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerStart.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "LedgerAtmosphere.h"
#include "LedgerPlanet.h"
#include "LedgerSimSubsystem.h"
#include "LedgerTerrainMath.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"

// ---------------------------------------------------------------- pawn

ALedgerOrbiterPawn::ALedgerOrbiterPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// Start high enough to see the curve of the planet on the first frame.
	BaseTurnRate = 45.0f;
	BaseLookUpRate = 45.0f;
}

void ALedgerOrbiterPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	const ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Planet == nullptr)
	{
		return;
	}

	const FVector3d ToCentre = FVector3d(GetActorLocation() - Planet->GetActorLocation());
	const double DistanceFromCentre = ToCentre.Length();
	if (DistanceFromCentre <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double SurfaceRadius = Planet->SurfaceRadiusAt(ToCentre / DistanceFromCentre);
	const double Altitude = FMath::Max(DistanceFromCentre - SurfaceRadius, 0.0);

	const float Speed = FMath::Clamp(
		static_cast<float>(Altitude * AltitudeSpeedFactor),
		MinSpeed,
		MaxSpeed);

	if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		Movement->MaxSpeed = Speed;
		Movement->Acceleration = Speed * 4.0f;
		Movement->Deceleration = Speed * 4.0f;
	}
}

// ---------------------------------------------------------------- game mode

ALedgerGameMode::ALedgerGameMode()
{
	DefaultPawnClass = ALedgerOrbiterPawn::StaticClass();
}

AActor* ALedgerGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	// Read the planet's default radius rather than hard-coding one, so changing
	// the planet does not silently strand the player inside it.
	const ALedgerPlanet* PlanetDefaults = GetDefault<ALedgerPlanet>();
	const double PlanetRadius = PlanetDefaults != nullptr ? PlanetDefaults->Radius : 6000000.0;

	// Start over the lit hemisphere, so the first frame is a planet rather than
	// an eclipse.
	const FVector3d SunSide = FVector3d(0.55, 0.35, 0.78).GetSafeNormal();
	const FVector Start = FVector(SunSide * (PlanetRadius * 2.0));
	const FRotator LookDown = (FVector::ZeroVector - Start).Rotation();

	// APlayerStart, not a bare AActor: an actor with no root component cannot
	// hold a transform, so its location silently reads back as the origin — and
	// the origin is the centre of the planet. The symptom was the LOD
	// subdividing all six faces uniformly, which is exactly what a camera at
	// the planet's core would ask for.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Spot = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Start, LookDown, Params);
	if (Spot == nullptr)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	UE_LOG(LogLedger, Log, TEXT("player start: %s looking %s"),
		*Start.ToCompactString(), *LookDown.ToCompactString());
	return Spot;
}

// ---------------------------------------------------------------- world

bool ULedgerWorldBuilder::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE only. Spawning a planet into the editor's preview world
	// would put actors in a level nobody asked to modify.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerWorldBuilder::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Planet = InWorld.SpawnActor<ALedgerPlanet>(
		ALedgerPlanet::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);

	if (Planet == nullptr)
	{
		UE_LOG(LogLedger, Error, TEXT("failed to spawn the planet"));
		return;
	}

	// Air, cloud and fog, sized against the planet that was just spawned.
	Atmosphere = InWorld.SpawnActor<ALedgerAtmosphere>(
		ALedgerAtmosphere::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Atmosphere != nullptr)
	{
		Atmosphere->ConfigureForPlanet(Planet->Radius, Planet->MaxElevation);
	}

	// A star, angled to light the side of the planet the descent comes down on.
	// A sun aimed anywhere else leaves the approach in its own shadow.
	SunFacing = FVector3d(0.55, 0.35, 0.78).GetSafeNormal();
	const FRotator SunRotation = (-FVector(SunFacing)).Rotation();
	if (ADirectionalLight* Sun = InWorld.SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), FVector::ZeroVector, SunRotation, Params))
	{
		Sun->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Component->SetIntensity(11.0f);
			Component->SetAtmosphereSunLight(true);
			Component->SetDynamicShadowDistanceMovableLight(400000.0f);
		}
	}

	// Exposure. This is the single setting that decides whether the transition
	// works: space is nearly black, a sunlit surface is not, and no fixed
	// exposure serves both. Left to itself the auto-exposure hunts for whatever
	// fills the frame and blows the planet out to white on the way in — which is
	// exactly what the first reentry capture did.
	//
	// The slow adaptation is also the *transition* the eye reads: brightening
	// over about two seconds as the atmosphere closes in is what makes the
	// descent feel continuous rather than cut.
	if (APostProcessVolume* PostProcess = InWorld.SpawnActor<APostProcessVolume>(
		APostProcessVolume::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params))
	{
		PostProcess->bUnbound = true;
		FPostProcessSettings& Settings = PostProcess->Settings;

		Settings.bOverride_AutoExposureMethod = true;
		Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;

		// Clamps, not a fixed value: wide enough to cover orbit-to-ground,
		// narrow enough that empty space cannot drag it to the ceiling.
		// Wide enough to span orbit and a sunlit surface. Clamped too tightly
		// (0.25 to 4.0) the ground could not be exposed down and came out as
		// flat pale green.
		Settings.bOverride_AutoExposureMinBrightness = true;
		Settings.AutoExposureMinBrightness = 0.03f;
		Settings.bOverride_AutoExposureMaxBrightness = true;
		Settings.AutoExposureMaxBrightness = 8.0f;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = -0.45f;

		Settings.bOverride_AutoExposureSpeedUp = true;
		Settings.AutoExposureSpeedUp = 1.2f;
		Settings.bOverride_AutoExposureSpeedDown = true;
		Settings.AutoExposureSpeedDown = 0.8f;

		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.35f;
	}

	if (ASkyLight* Sky = InWorld.SpawnActor<ASkyLight>(ASkyLight::StaticClass(), Params))
	{
		// ASkyLight has no SetMobility of its own; set it on the component.
		if (USkyLightComponent* Component = Sky->GetLightComponent())
		{
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetIntensity(1.0f);
			Component->SourceType = ESkyLightSourceType::SLS_CapturedScene;
			// Real-time capture: the ambient light has to change as the camera
			// descends through the atmosphere, or the ground stays lit like
			// space and the transition reads as a cut.
			Component->bRealTimeCapture = true;
			Component->RecaptureSky();
		}
	}

	PlaceRegionMarkers(InWorld);

	// Give the LOD a few seconds of settling, then capture the view and the
	// cook timings together. The pair is the §15.1 deliverable: a picture of
	// the terrain and the numbers that say whether it hitches.
	// A scripted reentry: settle in orbit, fly down, and capture at three points
	// along the way. One continuous world, one continuous camera, no seam.
	FTimerManager& Timers = InWorld.GetTimerManager();
	Timers.SetTimer(CaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::CaptureAndReport), 14.0f, false);
	Timers.SetTimer(DescendTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::BeginDescent), 18.0f, false);
	Timers.SetTimer(MidCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::CaptureAndReport), 42.0f, false);
	Timers.SetTimer(SurfaceCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::CaptureSurface), 72.0f, false);

	UE_LOG(LogLedger, Log, TEXT("world built: planet, sun, sky light, region markers"));
}

void ULedgerWorldBuilder::Capture(const TCHAR* Name)
{
	// An explicit request with an explicit path. `HighResShot` routes through
	// the console and lands wherever the screenshot settings point, which is
	// not somewhere a build script can reliably find.
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), FString(Name)));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogLedger, Log, TEXT("capture -> %s"), *Path);
}

void ULedgerWorldBuilder::CaptureAndReport()
{
	UWorld* World = GetWorld();
	if (World == nullptr || GEngine == nullptr)
	{
		return;
	}
	GEngine->Exec(World, TEXT("Ledger.Terrain.Stats"));
	Capture(DescentElapsed > 0.0 ? TEXT("terrain-entry.png") : TEXT("terrain-orbit.png"));
}

void ULedgerWorldBuilder::BeginDescent()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr || Planet == nullptr)
	{
		return;
	}

	// Find somewhere worth landing.
	//
	// Most of a real planet is flat — that is what makes it real, and it is also
	// why an arbitrary descent vector puts the camera on a featureless plain and
	// proves nothing about the terrain. Sample the sphere and go where the
	// relief is, the same way you would choose a shot.
	const FLedgerTerrainParams Params = Planet->TerrainParams();
	double BestScore = -MAX_dbl;
	DescentDirection = FVector3d(0.35, 0.25, 1.0).GetSafeNormal();

	constexpr int32 Samples = 4096;
	const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const double Y = 1.0 - (static_cast<double>(Index) / (Samples - 1)) * 2.0;
		const double RadiusAtY = FMath::Sqrt(FMath::Max(0.0, 1.0 - Y * Y));
		const double Theta = GoldenAngle * static_cast<double>(Index);
		const FVector3d Candidate = FVector3d(
			FMath::Cos(Theta) * RadiusAtY, Y, FMath::Sin(Theta) * RadiusAtY).GetSafeNormal();

		// Daylight first. Relief is worth nothing in the dark, and half the
		// planet is dark at any moment.
		const double SunAlignment = FVector3d::DotProduct(Candidate, SunFacing);
		if (SunAlignment < 0.45)
		{
			continue;
		}

		const double Height = LedgerTerrain::Elevation(Candidate, Params);
		if (Height <= 0.0)
		{
			continue;
		}

		// Score on height *and* local variation: a high plateau is no more
		// interesting to look at than a low one. Four probes a few kilometres
		// out give the local range for the cost of four noise evaluations.
		const FVector3d Tangent = FVector3d::CrossProduct(Candidate, FVector3d::UpVector).GetSafeNormal();
		const FVector3d Bitangent = FVector3d::CrossProduct(Candidate, Tangent);
		const double Step = 0.0009; // ~5.7 km on this planet
		double Lowest = Height;
		double Highest = Height;
		for (int32 Probe = 0; Probe < 4; ++Probe)
		{
			const FVector3d Offset = (Probe < 2 ? Tangent : Bitangent) * ((Probe & 1) ? Step : -Step);
			const double Near = LedgerTerrain::Elevation((Candidate + Offset).GetSafeNormal(), Params);
			Lowest = FMath::Min(Lowest, Near);
			Highest = FMath::Max(Highest, Near);
		}

		// Prefer sites a little off the sub-solar point: light straight down
		// flattens everything, and a lower sun rakes the relief.
		const double LightingBonus = (1.0 - FMath::Abs(SunAlignment - 0.7)) * Params.MaxElevation * 0.25;
		const double Score = Height + (Highest - Lowest) * 2.5 + LightingBonus;
		if (Score > BestScore)
		{
			BestScore = Score;
			DescentDirection = Candidate;
		}
	}

	UE_LOG(LogLedger, Log, TEXT("landing site: %.2f km above sea level, sun alignment %.2f"),
		LedgerTerrain::Elevation(DescentDirection, Params) / 100000.0,
		FVector3d::DotProduct(DescentDirection, SunFacing));

	const FVector3d Current = FVector3d(Pawn->GetActorLocation() - Planet->GetActorLocation());
	DescentStartAltitude = Current.Length();
	// Two kilometres up: above the relief, low enough that collision is being
	// cooked and the LOD has refined several levels.
	// 1.2 km above the local ground. High enough to see across the range, low
	// enough that relief still reads as relief rather than as texture.
	DescentEndAltitude = Planet->SurfaceRadiusAt(DescentDirection) + 120000.0;
	DescentElapsed = 0.0;

	World->GetTimerManager().SetTimer(
		DescentStepTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::StepDescent),
		1.0f / 60.0f,
		true);

	UE_LOG(LogLedger, Log, TEXT("reentry: %.0f cm -> %.0f cm over %.0f s"),
		DescentStartAltitude, DescentEndAltitude, DescentDuration);
}

void ULedgerWorldBuilder::StepDescent()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr || Planet == nullptr)
	{
		return;
	}

	DescentElapsed += 1.0 / 60.0;
	const double RawAlpha = FMath::Clamp(DescentElapsed / DescentDuration, 0.0, 1.0);

	// Ease in and out. A linear descent arrives at the ground at full speed and
	// reads as a camera being dragged; easing reads as flying.
	const double Alpha = RawAlpha * RawAlpha * (3.0 - 2.0 * RawAlpha);

	// Interpolate the *logarithm* of the altitude, not the altitude. Linear
	// interpolation from 6,000 km down to 3 km spends almost the whole descent
	// in empty space and then crosses the entire atmosphere in the last half
	// second. Log space gives a constant relative rate — the altitude halves
	// every so many seconds — which is both what an approach looks like and
	// what gives the LOD time to refine.
	const double Altitude = FMath::Exp(FMath::Lerp(
		FMath::Loge(DescentStartAltitude), FMath::Loge(DescentEndAltitude), Alpha));

	const FVector Location = Planet->GetActorLocation() + FVector(DescentDirection * Altitude);
	Pawn->SetActorLocation(Location);

	// Pitch from straight-down at the start to a shallow descent angle at the
	// end, so the horizon rises into frame the way it does on a real approach.
	// The camera follows the *controller's* rotation, not the pawn's.
	const FVector Up(DescentDirection);
	FVector Forward = FVector::CrossProduct(Up, FVector(0.0, 1.0, 0.0)).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::CrossProduct(Up, FVector(1.0, 0.0, 0.0)).GetSafeNormal();
	}

	const FVector Look = FMath::Lerp(FVector(-Up), Forward - Up * 0.34, Alpha).GetSafeNormal();

	// `MakeFromXZ`, not `Look.Rotation()`. A rotation built from a direction
	// alone has no opinion about roll and resolves it against *world* Z — which
	// on a sphere is only vertical at one point, so the horizon came out tilted
	// everywhere else. Passing the local up as the Z reference is what makes it
	// level.
	Controller->SetControlRotation(FRotationMatrix::MakeFromXZ(Look, Up).Rotator());

	if (RawAlpha >= 1.0)
	{
		World->GetTimerManager().ClearTimer(DescentStepTimer);
		UE_LOG(LogLedger, Log, TEXT("reentry complete at %s"), *Location.ToCompactString());
	}
}

void ULedgerWorldBuilder::CaptureSurface()
{
	UWorld* World = GetWorld();
	if (World == nullptr || GEngine == nullptr)
	{
		return;
	}
	GEngine->Exec(World, TEXT("Ledger.Terrain.Stats"));
	Capture(TEXT("terrain-surface.png"));
}

void ULedgerWorldBuilder::PlaceRegionMarkers(UWorld& InWorld)
{
	const ULedgerSimSubsystem* Sim = GEngine != nullptr
		? GEngine->GetEngineSubsystem<ULedgerSimSubsystem>()
		: nullptr;
	if (Sim == nullptr || !Sim->HasSnapshot() || Planet == nullptr)
	{
		UE_LOG(LogLedger, Warning, TEXT("no sim snapshot; placing no region markers"));
		return;
	}

	UStaticMesh* Marker = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Marker == nullptr)
	{
		UE_LOG(LogLedger, Warning, TEXT("no marker mesh available; regions will be invisible"));
		return;
	}

	const FLedgerSnapshot& Snapshot = Sim->GetSnapshot();
	const int32 RegionCount = Snapshot.Regions.Num();

	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		// Spread the regions evenly with a Fibonacci sphere. Deterministic, and
		// it avoids the clustering at the poles that naive lat/long spacing
		// gives — the same reason the terrain uses a cube-sphere.
		const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));
		const double Y = 1.0 - (static_cast<double>(Index) / FMath::Max(1.0, static_cast<double>(RegionCount - 1))) * 2.0;
		const double RadiusAtY = FMath::Sqrt(FMath::Max(0.0, 1.0 - Y * Y));
		const double Theta = GoldenAngle * static_cast<double>(Index);
		const FVector3d Direction = FVector3d(
			FMath::Cos(Theta) * RadiusAtY, Y, FMath::Sin(Theta) * RadiusAtY).GetSafeNormal();

		const double SurfaceRadius = Planet->SurfaceRadiusAt(Direction);
		const FVector Location = Planet->GetActorLocation() + FVector(Direction * (SurfaceRadius + 100000.0));

		// No explicit name: the actor's outer is the level, not the world, so a
		// name made unique against the world collides on the second spawn. Let
		// the engine name them and carry the readable name on the label.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AStaticMeshActor* Actor = InWorld.SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			Location,
			FRotationMatrix::MakeFromZ(FVector(Direction)).Rotator(),
			Params);
		if (Actor == nullptr)
		{
			continue;
		}

		Actor->SetMobility(EComponentMobility::Movable);
		if (UStaticMeshComponent* Component = Actor->GetStaticMeshComponent())
		{
			Component->SetStaticMesh(Marker);
			// Tall and thin, so it reads as a beacon from altitude.
			// 40 m across, 2 km tall. On a 6,371 km planet anything human-sized
			// is smaller than a pixel from any altitude worth flying at.
			Component->SetWorldScale3D(FVector(400.0f, 400.0f, 20000.0f));
		}
#if WITH_EDITOR
		Actor->SetActorLabel(Snapshot.Regions[Index].Name);
#endif

		UE_LOG(LogLedger, Log, TEXT("  region marker: %s at %s"),
			*Snapshot.Regions[Index].Name, *Location.ToCompactString());
	}
}
