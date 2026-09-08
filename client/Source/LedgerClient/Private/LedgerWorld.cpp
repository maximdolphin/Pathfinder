#include "LedgerWorld.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "LedgerAtmosphere.h"
#include "LedgerPlanet.h"
#include "LedgerSettlement.h"
#include "LedgerShip.h"
#include "LedgerSimSubsystem.h"
#include "LedgerTerrainMath.h"
#include "UnrealClient.h"

namespace
{
	/// Where the sun is, as a direction from the planet's centre. Everything
	/// about daylight — the site choice, the light's rotation, the framing of
	/// the first shot — derives from this one vector.
	const FVector3d SunDirection = FVector3d(0.55, 0.35, 0.78).GetSafeNormal();
}

// ---------------------------------------------------------------- game mode

ALedgerGameMode::ALedgerGameMode()
{
	DefaultPawnClass = ALedgerShip::StaticClass();
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
	const double PlanetRadius = PlanetDefaults != nullptr ? PlanetDefaults->Radius : 637100000.0;

	// Over the lit hemisphere, so the first frame is a planet and not an eclipse.
	const FVector Start = FVector(SunDirection * (PlanetRadius * 2.0));
	const FRotator LookDown = (FVector::ZeroVector - Start).Rotation();

	// APlayerStart, not a bare AActor: an actor with no root component cannot
	// hold a transform, so its location silently reads back as the origin — and
	// the origin is the centre of the planet.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Spot = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Start, LookDown, Params);
	if (Spot == nullptr)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	UE_LOG(LogLedger, Log, TEXT("player start: %s"), *Start.ToCompactString());
	return Spot;
}

// ---------------------------------------------------------------- world

bool ULedgerWorldBuilder::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE only. Spawning a planet into the editor's preview world would
	// put actors in a level nobody asked to modify.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

ALedgerShip* ULedgerWorldBuilder::GetShip() const
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	return Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;
}

void ULedgerWorldBuilder::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	SunFacing = SunDirection;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Planet = InWorld.SpawnActor<ALedgerPlanet>(
		ALedgerPlanet::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Planet == nullptr)
	{
		UE_LOG(LogLedger, Error, TEXT("failed to spawn the planet"));
		return;
	}

	Atmosphere = InWorld.SpawnActor<ALedgerAtmosphere>(
		ALedgerAtmosphere::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Atmosphere != nullptr)
	{
		Atmosphere->ConfigureForPlanet(Planet->Radius, Planet->MaxElevation);
	}

	// A star, angled to light the side of the planet the descent comes down on.
	const FRotator SunRotation = (-FVector(SunFacing)).Rotation();
	if (ADirectionalLight* Sun = InWorld.SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(), FVector::ZeroVector, SunRotation, Params))
	{
		Sun->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Component->SetIntensity(11.0f);
			Component->SetAtmosphereSunLight(true);
			Component->SetDynamicShadowDistanceMovableLight(600000.0f);
		}
	}

	if (ASkyLight* Sky = InWorld.SpawnActor<ASkyLight>(ASkyLight::StaticClass(), Params))
	{
		if (USkyLightComponent* Component = Sky->GetLightComponent())
		{
			Component->SetMobility(EComponentMobility::Movable);
			// Sky fill. With the sun anywhere near vertical, every wall in the town
			// is lit only by the sky, and at intensity 1 they read as black
			// rectangles under lit roofs.
			Component->SetIntensity(3.2f);
			Component->SourceType = ESkyLightSourceType::SLS_CapturedScene;
			// Real-time capture: the ambient has to change as the ship descends
			// through the atmosphere, or the ground stays lit like space and the
			// transition reads as a cut.
			Component->bRealTimeCapture = true;
			Component->RecaptureSky();
		}
	}

	// Exposure. The single setting that decides whether the transition works:
	// space is nearly black, a sunlit surface is not, and no fixed exposure
	// serves both. The slow adaptation is also the transition the eye reads.
	if (APostProcessVolume* PostProcess = InWorld.SpawnActor<APostProcessVolume>(
		APostProcessVolume::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params))
	{
		PostProcess->bUnbound = true;
		FPostProcessSettings& Settings = PostProcess->Settings;

		Settings.bOverride_AutoExposureMethod = true;
		Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
		Settings.bOverride_AutoExposureMinBrightness = true;
		Settings.AutoExposureMinBrightness = 0.03f;
		Settings.bOverride_AutoExposureMaxBrightness = true;
		Settings.AutoExposureMaxBrightness = 8.0f;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 0.1f;
		Settings.bOverride_AutoExposureSpeedUp = true;
		Settings.AutoExposureSpeedUp = 1.2f;
		Settings.bOverride_AutoExposureSpeedDown = true;
		Settings.AutoExposureSpeedDown = 0.8f;
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.35f;
	}

	// The site has to be chosen before the town can be built on it, and both
	// before the descent aims anywhere.
	ChooseSite();

	Settlement = InWorld.SpawnActor<ALedgerSettlement>(
		ALedgerSettlement::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Settlement != nullptr)
	{
		Settlement->Build(Planet, SiteDirection, static_cast<uint32>(Planet->Seed));
	}

	PlaceRegionMarkers(InWorld);

	// A scripted reentry: settle in orbit, fly down to the town, and capture
	// along the way. One continuous world, one continuous camera, no seam.
	FTimerManager& Timers = InWorld.GetTimerManager();
	Timers.SetTimer(OrbitCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::CaptureOrbit), 14.0f, false);
	Timers.SetTimer(DescendTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::BeginDescent), 18.0f, false);
	Timers.SetTimer(EntryCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::CaptureEntry), 42.0f, false);
	Timers.SetTimer(SurfaceCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::CaptureSurface), 68.0f, false);
	Timers.SetTimer(TownCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::FrameTown), 78.0f, false);
	Timers.SetTimer(AscentTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::BeginAscent), 112.0f, false);
	Timers.SetTimer(CoastCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::FrameCoast), 96.0f, false);
	Timers.SetTimer(UnderwaterTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::FrameUnderwater), 104.0f, false);
	Timers.SetTimer(SpaceCaptureTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::FrameSpace), 126.0f, false);

	UE_LOG(LogLedger, Log, TEXT("world built: planet, atmosphere, sun, town, ship"));
}

void ULedgerWorldBuilder::ChooseSite()
{
	if (Planet == nullptr)
	{
		return;
	}

	const FLedgerTerrainParams Params = Planet->TerrainParams();
	double BestScore = -MAX_dbl;
	SiteDirection = SunFacing;

	constexpr int32 Samples = 4096;
	const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));

	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const double Y = 1.0 - (static_cast<double>(Index) / (Samples - 1)) * 2.0;
		const double RadiusAtY = FMath::Sqrt(FMath::Max(0.0, 1.0 - Y * Y));
		const double Theta = GoldenAngle * static_cast<double>(Index);
		const FVector3d Candidate = FVector3d(
			FMath::Cos(Theta) * RadiusAtY, Y, FMath::Sin(Theta) * RadiusAtY).GetSafeNormal();

		// Well into the day, not near the terminator. Lower than this and the
		// sun sits a few degrees above the local horizon, and the whole landscape
		// comes back as a silhouette against a sunset.
		const double SunAlignment = FVector3d::DotProduct(Candidate, SunFacing);
		if (SunAlignment < 0.82)
		{
			continue;
		}

		const double Height = LedgerTerrain::Elevation(Candidate, Params);
		if (Height <= 0.0)
		{
			continue;
		}

		// Score on height, distant relief, and *flatness at the centre*. The
		// town needs somewhere level to stand; the view needs something to look
		// at. Those pull against each other, so both are in the score: probe
		// close for flat, probe far for relief.
		const FVector3d Tangent = FVector3d::CrossProduct(Candidate, FVector3d::UpVector).GetSafeNormal();
		const FVector3d Bitangent = FVector3d::CrossProduct(Candidate, Tangent);

		double NearLow = Height;
		double NearHigh = Height;
		for (int32 Probe = 0; Probe < 4; ++Probe)
		{
			const double Step = 0.00006; // ~380 m
			const FVector3d Offset = (Probe < 2 ? Tangent : Bitangent) * ((Probe & 1) ? Step : -Step);
			const double Near = LedgerTerrain::Elevation((Candidate + Offset).GetSafeNormal(), Params);
			NearLow = FMath::Min(NearLow, Near);
			NearHigh = FMath::Max(NearHigh, Near);
		}

		double FarLow = Height;
		double FarHigh = Height;
		for (int32 Probe = 0; Probe < 4; ++Probe)
		{
			const double Step = 0.0012; // ~7.6 km
			const FVector3d Offset = (Probe < 2 ? Tangent : Bitangent) * ((Probe & 1) ? Step : -Step);
			const double Far = LedgerTerrain::Elevation((Candidate + Offset).GetSafeNormal(), Params);
			FarLow = FMath::Min(FarLow, Far);
			FarHigh = FMath::Max(FarHigh, Far);
		}

		const double LocalRoughness = NearHigh - NearLow;
		const double Relief = FarHigh - FarLow;

		// Peak at 0.84: the sun about 33 degrees off vertical — mid-afternoon.
		// Nearer vertical and the relief flattens out and every wall in the town
		// falls into its own shadow; nearer the terminator and the whole
		// landscape is a silhouette.
		const double LightingBonus = (1.0 - FMath::Abs(SunAlignment - 0.84)) * Params.MaxElevation * 0.8;

		// Flat enough to build on, interesting enough to look at. Weighted hard
		// toward flatness the town sits on and hard toward relief a few
		// kilometres out, because those are different probes and can both be
		// satisfied — a valley floor under mountains.
		const double Score =
			Height * 0.4
			+ Relief * 3.4
			- LocalRoughness * 3.0
			+ LightingBonus;

		if (Score > BestScore)
		{
			BestScore = Score;
			SiteDirection = Candidate;
		}
	}

	UE_LOG(LogLedger, Log, TEXT("site: %.2f km above sea level, sun alignment %.2f"),
		LedgerTerrain::Elevation(SiteDirection, Params) / 100000.0,
		FVector3d::DotProduct(SiteDirection, SunFacing));
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
		return;
	}

	const FLedgerSnapshot& Snapshot = Sim->GetSnapshot();
	const int32 RegionCount = Snapshot.Regions.Num();

	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		// Fibonacci sphere: deterministic, and it avoids the clustering at the
		// poles that naive lat/long spacing gives.
		const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));
		const double Y = 1.0 - (static_cast<double>(Index) / FMath::Max(1.0, static_cast<double>(RegionCount - 1))) * 2.0;
		const double RadiusAtY = FMath::Sqrt(FMath::Max(0.0, 1.0 - Y * Y));
		const double Theta = GoldenAngle * static_cast<double>(Index);
		const FVector3d Direction = FVector3d(
			FMath::Cos(Theta) * RadiusAtY, Y, FMath::Sin(Theta) * RadiusAtY).GetSafeNormal();

		const double SurfaceRadius = Planet->SurfaceRadiusAt(Direction);
		const FVector Location = Planet->GetActorLocation() + FVector(Direction * (SurfaceRadius + 100000.0));

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
			// 40 m across, 2 km tall. On a 6,371 km planet anything human-sized
			// is smaller than a pixel from any altitude worth flying at.
			Component->SetWorldScale3D(FVector(400.0f, 400.0f, 20000.0f));
		}
#if WITH_EDITOR
		Actor->SetActorLabel(Snapshot.Regions[Index].Name);
#endif
	}
}

// ---------------------------------------------------------------- captures

void ULedgerWorldBuilder::Capture(const TCHAR* Name)
{
	// An explicit request with an explicit path. `HighResShot` routes through the
	// console and lands wherever the screenshot settings point, which is not
	// somewhere a build script can reliably find.
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), FString(Name)));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogLedger, Log, TEXT("capture -> %s"), *Path);
}

void ULedgerWorldBuilder::CaptureOrbit()
{
	if (UWorld* World = GetWorld())
	{
		if (GEngine != nullptr)
		{
			GEngine->Exec(World, TEXT("Ledger.Terrain.Stats"));
		}
	}
	Capture(TEXT("terrain-orbit.png"));
}

void ULedgerWorldBuilder::CaptureEntry()
{
	Capture(TEXT("terrain-entry.png"));
}

void ULedgerWorldBuilder::CaptureSurface()
{
	if (UWorld* World = GetWorld())
	{
		if (GEngine != nullptr)
		{
			GEngine->Exec(World, TEXT("Ledger.Terrain.Stats"));
		}
	}
	Capture(TEXT("terrain-surface.png"));
}

void ULedgerWorldBuilder::CaptureTown()
{
	Capture(TEXT("terrain-town.png"));
}

// ---------------------------------------------------------------- descent

void ULedgerWorldBuilder::BeginDescent()
{
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	if (World == nullptr || Ship == nullptr || Planet == nullptr)
	{
		return;
	}

	// The ship stops integrating while the script owns its transform. Two
	// systems writing the same position produces a fight nobody wins.
	Ship->SetFlightEnabled(false);
	Ship->SetVelocity(FVector::ZeroVector);

	const FVector3d Current = FVector3d(Ship->GetActorLocation() - Planet->GetActorLocation());
	DescentStartAltitude = Current.Length();
	// Just above the pad, so the handover leaves the player where the town is.
	DescentEndAltitude = Planet->SurfaceRadiusAt(SiteDirection) + 26000.0;
	DescentElapsed = 0.0;

	World->GetTimerManager().SetTimer(
		DescentStepTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::StepDescent),
		1.0f / 60.0f,
		true);

	UE_LOG(LogLedger, Log, TEXT("reentry: %.0f km -> %.2f km over %.0f s"),
		DescentStartAltitude / 100000.0, DescentEndAltitude / 100000.0, DescentDuration);
}

void ULedgerWorldBuilder::StepDescent()
{
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet == nullptr)
	{
		return;
	}

	DescentElapsed += 1.0 / 60.0;
	const double RawAlpha = FMath::Clamp(DescentElapsed / DescentDuration, 0.0, 1.0);
	const double Alpha = RawAlpha * RawAlpha * (3.0 - 2.0 * RawAlpha);

	// Interpolate the *logarithm* of the altitude, not the altitude. Linear
	// interpolation from 6,000 km down to a few hundred metres spends almost the
	// whole descent in empty space and then crosses the entire atmosphere in the
	// last half second. Log space gives a constant relative rate — the altitude
	// halves every so many seconds — which is what an approach looks like and
	// what gives the LOD time to refine.
	const double Altitude = FMath::Exp(FMath::Lerp(
		FMath::Loge(DescentStartAltitude), FMath::Loge(DescentEndAltitude), Alpha));

	// Slew from wherever the ship started to directly above the town, so the
	// approach converges on the site rather than dropping onto it from orbit.
	const FVector3d StartDirection = SunFacing;
	const FVector3d Direction =
		FMath::Lerp(StartDirection, SiteDirection, FMath::Min(Alpha * 1.5, 1.0)).GetSafeNormal();

	const FVector Location = Planet->GetActorLocation() + FVector(Direction * Altitude);
	Ship->SetActorLocation(Location);

	// Pitch from straight down at the start to a shallow approach at the end, so
	// the horizon rises into frame the way it does on a real descent.
	const FVector Up(Direction);
	FVector Forward = FVector::CrossProduct(Up, FVector(0.0, 1.0, 0.0)).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::CrossProduct(Up, FVector(1.0, 0.0, 0.0)).GetSafeNormal();
	}
	const FVector Look = FMath::Lerp(FVector(-Up), Forward - Up * 0.30, Alpha).GetSafeNormal();

	// `MakeFromXZ`, not `Look.Rotation()`. A rotation built from a direction
	// alone has no opinion about roll and resolves it against *world* Z — which
	// on a sphere is only vertical at one point, so the horizon came out tilted
	// everywhere else. Passing the local up as the Z reference makes it level.
	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Look, Up).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	if (RawAlpha >= 1.0)
	{
		World->GetTimerManager().ClearTimer(DescentStepTimer);

		// Hand the ship back. From here the player flies it, with gravity.
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetFlightEnabled(true);

		UE_LOG(LogLedger, Log, TEXT("reentry complete — control handed over at %.0f m"),
			Ship->AltitudeMetres());
	}
}

void ULedgerWorldBuilder::FrameTown()
{
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet == nullptr || Settlement == nullptr)
	{
		return;
	}

	const FVector Pad = Settlement->GetPadLocation();
	const FVector3d Up = SiteDirection;

	// Put the sun behind the camera. The sun is nearly overhead at this site, so
	// what matters is its *horizontal* component — the direction the shadows
	// fall. Standing on that side means looking at lit faces instead of
	// silhouettes, which is what the first town capture came back as.
	FVector3d SunHorizontal = SunFacing - Up * FVector3d::DotProduct(SunFacing, Up);
	if (SunHorizontal.IsNearlyZero())
	{
		SunHorizontal = FVector3d::CrossProduct(Up, FVector3d::UpVector).GetSafeNormal();
	}
	SunHorizontal.Normalize();

	// Back off along the sun direction and climb, so the town is below and lit.
	const FVector Vantage = Pad + FVector(SunHorizontal) * 26000.0 + FVector(Up) * 9000.0;
	const FVector Look = (Pad - Vantage).GetSafeNormal();

	Ship->SetFlightEnabled(false);
	Ship->SetVelocity(FVector::ZeroVector);
	Ship->SetActorLocation(Vantage);

	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Look, FVector(Up)).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	UE_LOG(LogLedger, Log, TEXT("town framed from %.0f m up, sun behind camera"),
		Ship->AltitudeMetres());

	// Capture a beat later, once the sky light and exposure have settled.
	FTimerHandle Shot;
	World->GetTimerManager().SetTimer(
		Shot,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::CaptureTown),
		4.0f,
		false);
}

void ULedgerWorldBuilder::BeginAscent()
{
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet == nullptr)
	{
		return;
	}

	// Back onto the chase boom, which the underwater shot pulled in to the nose.
	Ship->SetCameraBoom(5200.0f, 1500.0f);

	// Nose up, throttle open, and hand it to the flight model. Nothing after
	// this line places the ship — gravity pulls, thrust pushes, drag bleeds, and
	// whether it reaches orbit is a question about the numbers rather than about
	// the animation.
	const FVector3d Radial = FVector3d(Ship->GetActorLocation() - Planet->GetActorLocation());
	const FVector Up(Radial.GetSafeNormal());
	const FVector Lean = (Up * 0.94f + FVector(SunFacing) * 0.34f).GetSafeNormal();

	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Lean, Up).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	Ship->SetFlightEnabled(true);
	Ship->SetAutoThrottle(1.0f);

	UE_LOG(LogLedger, Log, TEXT("ascent: throttle open at %.0f m"), Ship->AltitudeMetres());
}

void ULedgerWorldBuilder::FrameSpace()
{
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet == nullptr)
	{
		return;
	}

	// Cut the throttle and turn to look back down. The ship keeps its velocity —
	// it is coasting, not parked — so this is a manoeuvre rather than a stop.
	Ship->SetAutoThrottle(0.0f);

	const FVector Down = (Planet->GetActorLocation() - Ship->GetActorLocation()).GetSafeNormal();
	const FVector Up = -Down;
	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Down, Up).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	UE_LOG(LogLedger, Log, TEXT("looking back from %.0f km, %.0f m/s"),
		Ship->AltitudeMetres() / 1000.0, Ship->GetVelocity().Size() / 100.0);

	FTimerHandle Shot;
	World->GetTimerManager().SetTimer(
		Shot,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::Capture, TEXT("terrain-space.png")),
		3.0f,
		false);
}

void ULedgerWorldBuilder::FrameCoast()
{
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet == nullptr)
	{
		return;
	}

	const FLedgerTerrainParams Params = Planet->TerrainParams();

	// Find a beach: land, in daylight, with open water within a few kilometres.
	// Scoring on "just above sea level with a deep neighbour" finds a shoreline
	// rather than a lake edge or a cliff over a shallow shelf.
	FVector3d Best = SiteDirection;
	FVector3d BestSeaward = FVector3d::ZeroVector;
	double BestScore = -MAX_dbl;

	constexpr int32 Samples = 8192;
	const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));

	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const double Y = 1.0 - (static_cast<double>(Index) / (Samples - 1)) * 2.0;
		const double RadiusAtY = FMath::Sqrt(FMath::Max(0.0, 1.0 - Y * Y));
		const double Theta = GoldenAngle * static_cast<double>(Index);
		const FVector3d Candidate = FVector3d(
			FMath::Cos(Theta) * RadiusAtY, Y, FMath::Sin(Theta) * RadiusAtY).GetSafeNormal();

		if (FVector3d::DotProduct(Candidate, SunFacing) < 0.80)
		{
			continue;
		}

		const double Height = LedgerTerrain::Elevation(Candidate, Params);
		// Just above the waterline: a beach, not a headland.
		if (Height < 0.0 || Height > Params.MaxElevation * 0.012)
		{
			continue;
		}

		const FVector3d Tangent = FVector3d::CrossProduct(Candidate, FVector3d::UpVector).GetSafeNormal();
		const FVector3d Bitangent = FVector3d::CrossProduct(Candidate, Tangent);

		// Probe outward for the deepest water within about 6 km.
		double Deepest = 0.0;
		FVector3d Seaward = Tangent;
		for (int32 Probe = 0; Probe < 8; ++Probe)
		{
			const double Angle = (Probe / 8.0) * 2.0 * PI;
			const FVector3d Direction = (Tangent * FMath::Cos(Angle) + Bitangent * FMath::Sin(Angle));
			const FVector3d Sample = (Candidate + Direction * 0.00095).GetSafeNormal();
			const double SampleHeight = LedgerTerrain::Elevation(Sample, Params);
			if (SampleHeight < Deepest)
			{
				Deepest = SampleHeight;
				Seaward = Direction;
			}
		}

		if (Deepest >= 0.0)
		{
			continue;
		}

		const double Score = -Deepest - Height * 3.0;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Candidate;
			BestSeaward = Seaward;
		}
	}

	if (BestSeaward.IsNearlyZero())
	{
		UE_LOG(LogLedger, Warning, TEXT("no coastline found in daylight"));
		return;
	}

	const FVector Up(Best);
	const double SurfaceRadius = Planet->SurfaceRadiusAt(Best);

	// Sixty metres up and a little back from the waterline, looking out to sea
	// with the horizon in frame. Low enough that the surface reads as a surface.
	const FVector Location = Planet->GetActorLocation()
		+ FVector(Best * (SurfaceRadius + 6000.0))
		- FVector(BestSeaward.GetSafeNormal()) * 12000.0;

	const FVector Look = (FVector(BestSeaward.GetSafeNormal()) - Up * 0.14f).GetSafeNormal();

	Ship->SetFlightEnabled(false);
	Ship->SetVelocity(FVector::ZeroVector);
	Ship->SetActorLocation(Location);

	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Look, Up).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	UE_LOG(LogLedger, Log, TEXT("coast: shore at %.0f m elevation, water %.0f m deep nearby"),
		LedgerTerrain::Elevation(Best, Params) / 100.0, -BestScore / 100.0);

	CoastSite = Best;
	CoastSeaward = BestSeaward.GetSafeNormal();

	FTimerHandle Shot;
	World->GetTimerManager().SetTimer(
		Shot,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::Capture, TEXT("terrain-coast.png")),
		6.0f,
		false);
}

void ULedgerWorldBuilder::FrameUnderwater()
{
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (World == nullptr || Planet == nullptr || Ship == nullptr || Controller == nullptr
		|| CoastSeaward.IsNearlyZero())
	{
		return;
	}

	// Six kilometres out from the beach, which is where the coast search found
	// its deepest neighbour and therefore the only water nearby with room to
	// put a camera in.
	const FVector3d Offshore = (CoastSite + CoastSeaward * 0.00095).GetSafeNormal();
	const FVector Up(Offshore);
	const double Floor = Planet->SurfaceRadiusAt(Offshore);
	const double Depth = Planet->Radius - Floor;

	// Halfway down. This is a shelf a few metres deep, not an ocean trench,
	// and there is no room to be fussy.
	const double CameraDepth = FMath::Clamp(Depth * 0.5, 250.0, 2200.0);

	// Out to the nose. The chase boom holds the camera fifteen metres above the
	// hull, so with the ship on the sea floor the camera is still dry.
	Ship->SetCameraBoom(-700.0f, 0.0f);

	// Looking back at the shore and downward, so the frame is filled by the
	// rising sea floor. Level with the surface it would be filled by the sky
	// through the water's back faces, which are not drawn.
	const FVector Look = (FVector(-CoastSeaward) - Up * 0.25).GetSafeNormal();
	const FVector Location = Planet->GetActorLocation()
		+ FVector(Offshore * (Planet->Radius - CameraDepth)) - Look * 700.0;

	Ship->SetFlightEnabled(false);
	Ship->SetVelocity(FVector::ZeroVector);
	Ship->SetActorLocation(Location);

	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Look, Up).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);

	UE_LOG(LogLedger, Log, TEXT("underwater: camera %.1f m down over %.0f m of water"),
		CameraDepth / 100.0, Depth / 100.0);

	FTimerHandle Shot;
	World->GetTimerManager().SetTimer(
		Shot,
		FTimerDelegate::CreateUObject(this, &ULedgerWorldBuilder::Capture, TEXT("terrain-underwater.png")),
		4.0f,
		false);
}
