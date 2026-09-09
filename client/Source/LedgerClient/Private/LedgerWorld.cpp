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
#include "EngineUtils.h"
#include "LedgerSurface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerStart.h"
#include "LedgerAtmosphere.h"
#include "LedgerMeshBake.h"
#include "LedgerMeshBuilder.h"
#include "LedgerBiome.h"
#include "LedgerBiomeSurfaces.h"
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

namespace
{
	/// The one already in the level, or a new one.
	///
	/// The level holds what exists; this code holds how it behaves. Placing the
	/// sun in a map and also spawning one at BeginPlay would give two suns and
	/// the "multiple directional lights competing for forward shading" warning
	/// that cost an hour on the turntable. Spawning remains the fallback so
	/// that a world with no level -- a test, or a map that has not been rebuilt
	/// -- still comes up lit rather than mysteriously black.
	template <typename T>
	T* Placed(UWorld& InWorld, const FActorSpawnParameters& Params,
		const FVector& Location = FVector::ZeroVector,
		const FRotator& Rotation = FRotator::ZeroRotator)
	{
		for (TActorIterator<T> It(&InWorld); It; ++It)
		{
			return *It;
		}
		return InWorld.SpawnActor<T>(T::StaticClass(), Location, Rotation, Params);
	}
}

void ULedgerWorldBuilder::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	SunFacing = SunDirection;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Planet = InWorld.SpawnActor<ALedgerPlanet>(
		ALedgerPlanet::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);

	if (Planet != nullptr)
	{
		// Built here, not in the planet: the composition root is the only place
		// that gets to know about both the terrain and the materials.
		//
		// `-flatterrain` swaps the surface for an untextured constant on the
		// same geometry. It is not a look; it is the control arm. T047 asks
		// whether composing the blend into a virtual texture makes it cheaper,
		// and that question cannot be answered without knowing what the blend
		// costs -- which nobody had measured. The difference in GPU
		// milliseconds between this and the real material over the same flight
		// is the entire budget any caching scheme is competing for.
		const bool bFlatTerrain = FParse::Param(FCommandLine::Get(), TEXT("flatterrain"));
		UMaterialInterface* Surface = bFlatTerrain
			? LedgerSurface::CreateFlatMaterial(Planet, FLinearColor(0.18f, 0.20f, 0.13f), 0.9f)
			: LedgerSurface::CreateTerrainMaterial(Planet, static_cast<uint32>(Planet->Seed));
		if (bFlatTerrain)
		{
			UE_LOG(LogLedger, Log, TEXT("terrain surface: FLAT (control arm for T047)"));
		}

		Planet->SetMaterials(Surface, LedgerSurface::CreateWaterMaterial(Planet));

		// ---- biomes -------------------------------------------------------
		//
		// Loaded here for the same reason the materials are: the planet reads
		// climate, and what climate *looks* like is not the quadtree's call.
		// The control arm skips them, because a flat terrain has no surface
		// sets to bind and binding them anyway would put the cost being
		// measured back into the arm measuring it.
		if (!bFlatTerrain)
		{
			TArray<FString> BiomeErrors;
			TSharedPtr<TArray<FLedgerBiome>> Biomes = MakeShared<TArray<FLedgerBiome>>(
				LedgerBiomes::Load(LedgerBiomes::DefaultDirectory(), BiomeErrors));
			for (const FString& Error : BiomeErrors)
			{
				UE_LOG(LogLedger, Error, TEXT("biome: %s"), *Error);
			}
			UE_LOG(LogLedger, Log, TEXT("biomes: %d loaded from %s"),
				Biomes->Num(), *LedgerBiomes::DefaultDirectory());

			if (Biomes->Num() > 0)
			{
				Planet->SetBiomes(Biomes);
				ALedgerPlanet* Owner = Planet;
				Planet->SetPaletteMaterialProvider(FLedgerPaletteMaterial::CreateLambda(
					[Owner, Surface](const FLedgerBiomePalette& Palette,
						const TArray<FLedgerBiome>& Set)
					{
						return LedgerBiomeSurfaces::CreatePaletteInstance(
							Owner, Surface, Palette, Set);
					}));
			}
		}
	}
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
	if (ADirectionalLight* Sun = Placed<ADirectionalLight>(
		InWorld, Params, FVector::ZeroVector, SunRotation))
	{
		Sun->SetMobility(EComponentMobility::Movable);
		Sun->SetActorRotation(SunRotation);
		if (UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Component->SetIntensity(11.0f);
			Component->SetAtmosphereSunLight(true);
			Component->SetDynamicShadowDistanceMovableLight(600000.0f);
		}
	}

	if (ASkyLight* Sky = Placed<ASkyLight>(InWorld, Params))
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
	if (APostProcessVolume* PostProcess = Placed<APostProcessVolume>(InWorld, Params))
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

	// What the lighting actually ended up as, rather than what was asked for.
	// Moving these three actors into the level changed every capture by a
	// structural residual of 25 to 32, and a change nobody can account for is
	// the thing the capture check exists to catch.
	for (TActorIterator<ADirectionalLight> It(&InWorld); It; ++It)
	{
		const UDirectionalLightComponent* Component =
			Cast<UDirectionalLightComponent>(It->GetLightComponent());
		UE_LOG(LogLedger, Log,
			TEXT("lighting: sun rotation %s, intensity %.2f, atmosphere sun %d, mobility %d"),
			*It->GetActorRotation().ToCompactString(),
			Component ? Component->Intensity : -1.0f,
			Component ? static_cast<int32>(Component->IsUsedAsAtmosphereSunLight()) : -1,
			static_cast<int32>(It->GetRootComponent()->Mobility.GetValue()));
	}
	for (TActorIterator<ASkyLight> It(&InWorld); It; ++It)
	{
		const USkyLightComponent* Component = It->GetLightComponent();
		UE_LOG(LogLedger, Log,
			TEXT("lighting: sky intensity %.2f, real-time capture %d, source %d"),
			Component ? Component->Intensity : -1.0f,
			Component ? static_cast<int32>(Component->bRealTimeCapture) : -1,
			Component ? static_cast<int32>(Component->SourceType) : -1);
	}
	for (TActorIterator<APostProcessVolume> It(&InWorld); It; ++It)
	{
		UE_LOG(LogLedger, Log,
			TEXT("lighting: post-process unbound %d, priority %.1f, exposure %.3f-%.3f bias %.2f"),
			static_cast<int32>(It->bUnbound), It->Priority,
			It->Settings.AutoExposureMinBrightness,
			It->Settings.AutoExposureMaxBrightness,
			It->Settings.AutoExposureBias);
	}

	UE_LOG(LogLedger, Log, TEXT("world built: planet, atmosphere, sun, town, ship"));

#if WITH_EDITOR
	// `-bakematerials` runs every material builder into a saved asset and quits.
	//
	// Here rather than in a commandlet because the builders need a world that
	// has already been built: the terrain material loads the surface sets and
	// reads their tiling out of the manifest, and a bare commandlet has none of
	// that. Building the world and then baking costs a few seconds and reuses
	// the path the game already takes.
	// `-bakemeshes` saves generated geometry as static mesh assets: Nanite, an
	// LOD chain, collision and a distance field, none of which a procedural
	// mesh component can have. ADR-0006.
	if (FParse::Param(FCommandLine::Get(), TEXT("bakemeshes")))
	{
		FString Body = TEXT("Baked meshes.\n\n");
		int32 Saved = 0;

		{
			FLedgerMeshBuilder Builder;
			ALedgerShip::DescribeHull(Builder);
			FString Line;
			if (LedgerMesh::Bake(Builder,
				FString(LedgerMesh::MeshPackageRoot) + TEXT("SM_ShipHull"),
				TEXT("SM_ShipHull"), Line) != nullptr)
			{
				++Saved;
			}
			Body += Line + TEXT("\n");
		}

		// Two trees, differing only in the lower canopy colour, because the
		// variation is baked into vertex colour and an instance cannot carry
		// its own. Two draws for the whole forest.
		for (int32 Variant = 0; Variant < 2; ++Variant)
		{
			FLedgerMeshBuilder Builder;
			const int32 CanopyStartsAt =
				ALedgerSettlement::DescribeTree(Builder, Variant == 1);
			const FString Name = FString::Printf(TEXT("SM_Tree_%s"),
				Variant == 0 ? TEXT("A") : TEXT("B"));
			FString Line;
			// No Nanite on the trees: forty-six triangles, and their colour is
			// per-vertex.
			if (LedgerMesh::Bake(Builder,
				FString(LedgerMesh::MeshPackageRoot) + Name, *Name, Line,
				/*bNanite*/ false, CanopyStartsAt) != nullptr)
			{
				++Saved;
			}
			Body += Line + TEXT("\n");
		}

		Body += FString::Printf(TEXT("\n  %d of 3 saved\n\n"), Saved);
		Body += FString::Printf(TEXT("VERDICT: %s\n"), Saved == 3 ? TEXT("PASS") : TEXT("FAIL"));

		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				TEXT("bake-meshes.txt")));
		FFileHelper::SaveStringToFile(Body, *Path);
		UE_LOG(LogLedger, Log, TEXT("mesh bake -> %s"), *Path);

		FGenericPlatformMisc::RequestExit(false);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("bakematerials")))
	{
		FString Report;
		const bool bAllSaved = LedgerSurface::BakeMaterials(Report);

		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				TEXT("bake-materials.txt")));
		FFileHelper::SaveStringToFile(Report, *Path);
		UE_LOG(LogLedger, Log, TEXT("bake -> %s"), *Path);

		FGenericPlatformMisc::RequestExit(!bAllSaved);
	}
#endif
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
