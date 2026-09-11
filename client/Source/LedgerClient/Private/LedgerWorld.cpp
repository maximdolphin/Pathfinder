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
#include "LedgerAir.h"
#include "LedgerCloud.h"
#include "LedgerAtmosphere.h"
#include "LedgerMeshBake.h"
#include "LedgerMeshBuilder.h"
#include "LedgerBiome.h"
#include "LedgerBiomeSurfaces.h"
#include "LedgerPlanet.h"
#include "LedgerSettlement.h"
#include "LedgerRock.h"
#include "LedgerShip.h"
#include "LedgerSimSubsystem.h"
#include "LedgerTerrainMath.h"
#include "UnrealClient.h"
#include "LedgerMath.h"
#include "LedgerEphemeris.h"
#include "LedgerSky.h"

namespace
{
	/// Which body of the generated system the world is standing on.
	///
	/// Index 1 is the planet: the primary is 0 and the planet is the first
	/// thing orbiting it. `-body=2` builds the moon instead, on the same
	/// terrain, the same streamer and the same materials -- T078's claim is
	/// that a moon needs none of its own, so the only thing that changes is
	/// which row of the system description the world reads.
	/// Which body the world is currently built for.
	///
	/// **A file-static rather than a command-line read, because it changes.**
	/// It used to parse `-body=` at every call site, which is fine for a world
	/// that is built once and wrong for one that can cross to a moon -- and
	/// there are twenty-six call sites, several of them in free functions with
	/// no access to the subsystem. One value, set once at begin play and again
	/// on a switch, is smaller than threading the subsystem through all of them.
	int32 GActiveBody = INDEX_NONE;

	int32 HomeBody()
	{
		if (GActiveBody != INDEX_NONE)
		{
			return GActiveBody;
		}
		int32 Body = 1;
		FParse::Value(FCommandLine::Get(), TEXT("body="), Body);
		return Body;
	}

	void SetActiveBody(int32 Body)
	{
		GActiveBody = Body;
	}

	/// What the star's light is worth here, lux, with nothing in front of it.
	///
	/// **It used to be the number 11.** T076 replaced it with the illuminance
	/// the star actually delivers at this distance: luminosity from the star's
	/// mass, over the area of a sphere the size of its orbit. For this system
	/// that is about 108,000 lux against Earth's 127,000, because the star is
	/// 0.94 solar masses and correspondingly dimmer.
	///
	/// The consequence anybody will notice is that the number went up by four
	/// orders of magnitude, so the exposure range had to as well -- see the
	/// post-process volume below. That is the trade a physical light makes: the
	/// units mean something, and nothing downstream may quietly assume a scale.
	double SunIlluminanceLux(const FLedgerSystem& System, double SecondsFromEpoch)
	{
		if (!System.Bodies.IsValidIndex(HomeBody()))
		{
			return 100000.0;
		}
		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);
		return LedgerSky::IlluminanceLux(
			System, States[HomeBody()].PositionMetres, SecondsFromEpoch);
	}

	/// Where the sun is, as a direction from the planet's centre. Everything
	/// about daylight — the site choice, the light's rotation, the framing of
	/// the first shot — derives from this one vector.
	///
	/// **It used to be a constant, `(0.55, 0.35, 0.78)` normalised, and T072 is
	/// about it not being one.** A hand-picked vector is a fourth independent
	/// variable next to the orbit, the rotation and the season: nothing stops
	/// it disagreeing with all three, and a sky that can disagree with the
	/// ephemeris is not evidence of anything. This asks the ephemeris.
	///
	/// The body frame is the right frame to ask in because the planet actor
	/// does not turn -- the ground is fixed in world space and always has been.
	/// Advancing the clock therefore moves the sun rather than the terrain,
	/// which is the same picture and a great deal less to rebuild.
	FVector3d SunDirectionAt(const FLedgerSystem& System, double SecondsFromEpoch)
	{
		const FVector3d Sun =
			LedgerSky::SunDirectionInBody(System, HomeBody(), SecondsFromEpoch);
		return Sun.IsNearlyZero() ? FVector3d::UnitZ() : Sun;
	}

	/// Seconds from the system's epoch, from `-when=`. Zero unless asked.
	///
	/// A number of seconds rather than an hour of the day: the day is however
	/// long this planet's rotation makes it, and a clock face would be
	/// borrowing Earth's.
	double WhenFromCommandLine()
	{
		double When = 0.0;
		FParse::Value(FCommandLine::Get(), TEXT("when="), When);
		return When;
	}

	/// The seed the system is generated from, from `-systemseed=`.
	uint32 SystemSeedFromCommandLine()
	{
		int32 Seed = 20260908;
		FParse::Value(FCommandLine::Get(), TEXT("systemseed="), Seed);
		return static_cast<uint32>(Seed);
	}

	/// The sun's direction for this run, from the command line alone.
	///
	/// The game mode needs this to put the player start over the lit side, and
	/// it runs before the world subsystem has built anything -- so rather than
	/// reach for a half-initialised subsystem, both ask this. It generates the
	/// system each time, which costs microseconds and cannot disagree with
	/// itself: a seed and a time are all either caller has, and the answer is a
	/// pure function of them.
	FVector3d SunFacingForThisRun()
	{
		return SunDirectionAt(
			LedgerBodies::Generate(SystemSeedFromCommandLine()), WhenFromCommandLine());
	}
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
	const FVector Start = FVector(SunFacingForThisRun() * (PlanetRadius * 2.0));
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

TStatId ULedgerWorldBuilder::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerWorldBuilder, STATGROUP_Tickables);
}

void ULedgerWorldBuilder::Tick(float DeltaSeconds)
{
	// The second half of a switch, a frame after it was asked for: the placed
	// actors are reconfigured for the new body here.
	if (PendingBody != INDEX_NONE)
	{
		const int32 Body = PendingBody;
		PendingBody = INDEX_NONE;
		SetActiveBody(Body);

		// **The sun has to change bodies too.** SunFacing is the star's
		// direction in the *active body's* rotating frame, so it means
		// something different the moment the active body changes. Leaving it
		// alone here left the moon's landing site being chosen against the
		// planet's sun vector: the picker duly found a place with the sun
		// nearly overhead by that measure and the sky, asked properly,
		// reported it eighty degrees below the horizon. The town was built in
		// the dark and three fixtures photographed it there.
		//
		// The ChooseSite() that used to be on this line did nothing at all --
		// the planet has just been destroyed, and it returns early without one.
		// BuildWorldFor calls it again once there is a planet to choose on.
		SunFacing = SunDirectionAt(System, WhenSeconds);
		if (UWorld* World = GetWorld())
		{
			BuildWorldFor(*World);
		}
		return;
	}

	Super::Tick(DeltaSeconds);
	KeepSkyWithViewer();
}

/// The sky light has to be where the viewer is, and it is the only thing this
/// subsystem ticks for.
///
/// A real-time capture sky light renders its cubemap from its own position. At
/// the world origin -- the planet's centre -- that cubemap is the inside of the
/// planet, and the sky light delivers no light at all. Moved to the camera it
/// captures the sky the camera can see, which is also correct as the ship
/// climbs: the ambient at 200 km should not be the ambient on the ground.
///
/// ponytail: every frame, unconditionally. The capture is throttled by the
/// renderer, not by this, and a threshold on how far it has moved would be a
/// second thing to get wrong for no measured saving.
void ULedgerWorldBuilder::KeepSkyWithViewer()
{
	UWorld* World = GetWorld();
	if (World == nullptr || Sky == nullptr)
	{
		return;
	}

	const APlayerController* Controller = World->GetFirstPlayerController();
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		return;
	}
	const FVector Was = Sky->GetActorLocation();
	const FVector Now = Pawn->GetActorLocation();
	Sky->SetActorLocation(Now);

	// The sun's elevation where the viewer actually is, handed to the
	// atmosphere as the floor on the elevation the engine uses for its one
	// global transmittance. See ALedgerAtmosphere::SetSunElevationFloorDegrees:
	// without it the fog, translucency and Lumen are lit by the sun as seen
	// from the north pole, which this season is below the horizon all day.
	if (Atmosphere != nullptr && Planet != nullptr)
	{
		FVector ViewPoint = Now;
		FRotator ViewRotation = FRotator::ZeroRotator;
		Controller->GetPlayerViewPoint(ViewPoint, ViewRotation);
		const FVector3d ViewerUp =
			(FVector3d(ViewPoint) - FVector3d(Planet->GetActorLocation())).GetSafeNormal();
		Atmosphere->SetSunElevationFloorDegrees(FMath::RadiansToDegrees(FMath::Asin(
			FMath::Clamp(FVector3d::DotProduct(SunFacing.GetSafeNormal(), ViewerUp), -1.0, 1.0))));
	}

	// **Moving it is not enough: the capture has to be asked for again.**
	//
	// bRealTimeCapture re-captures when the sky itself changes -- the sun
	// moves, the atmosphere changes -- and not when the light moves. So the one
	// capture taken at world build time, from the planet's centre, stayed the
	// answer forever: a black cubemap, and a sky light that delivered nothing
	// anywhere. Moving the actor to the surface changed nothing at all until
	// this line, which is how it was established that the position was only
	// half of it.
	//
	// A kilometre of hysteresis. The capture is not free and the sky does not
	// meaningfully change over less than that; without a threshold this is a
	// cubemap render every frame.
	//
	// **Scaled with altitude, and the sun counts too.** A static capture sees
	// what the sky looked like when it was taken, so a sun that has moved is as
	// much a reason as a viewer that has; and a tenth of the altitude keeps a
	// descent from orbit from asking for a cubemap every frame.
	const double AltitudeCm = Planet != nullptr
		? FMath::Max(0.0, FVector3d::Dist(FVector3d(Now), FVector3d(Planet->GetActorLocation())) - Planet->Radius)
		: 0.0;
	const double Threshold = FMath::Max(100000.0, AltitudeCm * 0.1);
	const bool bSunMoved = FVector3d::DotProduct(SunFacing.GetSafeNormal(), LastSkyCaptureSun)
		< FMath::Cos(FMath::DegreesToRadians(1.0));
	if (FVector::Distance(Was, LastSkyCapture) > Threshold
		|| FVector::Distance(Now, LastSkyCapture) > Threshold || bSunMoved)
	{
		if (USkyLightComponent* Component = Sky->GetLightComponent())
		{
			Component->RecaptureSky();
		}
		LastSkyCapture = Now;
		LastSkyCaptureSun = SunFacing.GetSafeNormal();
	}

	// Once, so the log says whether this ever ran and where it put it. The
	// first attempt at this fix changed nothing and there was no way to tell
	// whether the position was wrong or the tick was never happening.
	if (!bSkyMoved)
	{
		bSkyMoved = true;
		UE_LOG(LogLedger, Log,
			TEXT("lighting: sky light moved from %s to %s (%.0f km from the centre)"),
			*Was.ToCompactString(), *Sky->GetActorLocation().ToCompactString(),
			Sky->GetActorLocation().Length() / 100000.0);
	}
}

int32 ULedgerWorldBuilder::GetHomeBodyIndex() const
{
	return HomeBody();
}

void ULedgerWorldBuilder::SetWhenSeconds(double Seconds)
{
	WhenSeconds = Seconds;
	SunFacing = SunDirectionAt(System, WhenSeconds);

	// **The clouds move with the clock too.** T094. Coverage is a function of
	// the pressure overhead and the pressure is a function of time, so a deck
	// that was set once at begin play would be the weather of one instant
	// hanging over every other.
	if (Atmosphere != nullptr && System.Bodies.IsValidIndex(HomeBody()))
	{
		const FLedgerAirProfile Air = LedgerAir::For(System, HomeBody(), WhenSeconds);
		const double Latitude = FMath::Asin(
			FMath::Clamp(SiteDirection.GetSafeNormal().Z, -1.0, 1.0));
		const double Longitude = FMath::Atan2(SiteDirection.Y, SiteDirection.X);
		Atmosphere->SetDecks(LedgerCloud::DecksAt(
			System, HomeBody(), Air, Latitude, Longitude, WhenSeconds));
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// How much of the star is behind something, from the site. T075.
	//
	// The illuminance a surface receives is proportional to how much of the
	// disc is still showing, so the light is simply scaled by what is left.
	// Nothing else is needed for the ground to go dark -- no separate eclipse
	// effect that could disagree with the ephemeris about when.
	//
	// Asked at the site rather than at the camera because that is where the
	// world is built and where the fixtures stand. A shadow track is a few
	// hundred kilometres wide, so the difference matters only to a viewer who
	// has flown out of it, and a viewer who has flown out of it wants the
	// sunlight back.
	const double Covered = LedgerSky::StarCoveredFraction(
		System, HomeBody(), SiteDirection, WhenSeconds);
	const double Full = SunIlluminanceLux(System, WhenSeconds);
	const float Dimmed = static_cast<float>(Full * (1.0 - Covered));

	// The world's own light, found rather than remembered: the builder does not
	// keep a handle on it, and one directional light is what this world has.
	const FRotator Rotation = (-FVector(SunFacing)).Rotation();
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		It->SetActorRotation(Rotation);
		if (UDirectionalLightComponent* Component =
			Cast<UDirectionalLightComponent>(It->GetLightComponent()))
		{
			Component->SetIntensity(Dimmed);
		}
	}

	if (Covered > 0.001)
	{
		UE_LOG(LogLedger, Log,
			TEXT("eclipse: body %d covers %.1f%% of the star, sun at %.0f of %.0f lux"),
			LedgerSky::EclipsingBody(System, HomeBody(), SiteDirection, WhenSeconds),
			Covered * 100.0, Dimmed, Full);
	}
}

void ULedgerWorldBuilder::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// The system, and when it is. Generated rather than loaded: T069 made a
	// description a pure function of a seed, so a world is a seed and a time.
	System = LedgerBodies::Generate(SystemSeedFromCommandLine());
	SetActiveBody(HomeBody());
	WhenSeconds = WhenFromCommandLine();
	SunFacing = SunDirectionAt(System, WhenSeconds);

	if (System.Bodies.IsValidIndex(HomeBody()))
	{
		const FLedgerBody& Home = System.Bodies[HomeBody()];
		UE_LOG(LogLedger, Log,
			TEXT("sky: t=%.0f s, day %.0f s, tilt %.1f deg, sun facing %.3f %.3f %.3f"),
			WhenSeconds, Home.RotationPeriodSeconds,
			FMath::RadiansToDegrees(Home.AxialTiltRadians),
			SunFacing.X, SunFacing.Y, SunFacing.Z);
		UE_LOG(LogLedger, Log,
			TEXT("season: phase %.4f of the year, declination %+.2f deg"),
			LedgerSky::SeasonPhase(System, HomeBody(), WhenSeconds),
			FMath::RadiansToDegrees(
				LedgerSky::SolarDeclination(System, HomeBody(), WhenSeconds)));
	}

	// **The world for the body that is current, built here so it can be built
	// again.** T088 needs the ship to cross to a moon and land on it in one
	// session, and a world that can only be built during BeginPlay is a world
	// that needs a new process to change bodies. Everything below this line was
	// the tail of OnWorldBeginPlay and is unchanged; what is new is that it has
	// a name and can be called twice.
	BuildWorldFor(InWorld);
}

void ULedgerWorldBuilder::SwitchToBody(int32 BodyIndex)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !System.Bodies.IsValidIndex(BodyIndex)
		|| BodyIndex == HomeBody())
	{
		return;
	}

	UE_LOG(LogLedger, Log, TEXT("switching from body %d (%s) to %d (%s)"),
		HomeBody(), *System.Bodies[HomeBody()].Name,
		BodyIndex, *System.Bodies[BodyIndex].Name);

	// **Nothing is destroyed.** The planet, the atmosphere and the town are
	// actors in the level, and a crossing is those same actors told about a
	// different body: BuildWorldFor reconfigures each of them and the planet
	// grows its terrain again in place. Destroying them would be deleting part
	// of the map at runtime -- which is what this used to do, when they were
	// spawned rather than placed.

	// Built on the next tick, not this one. See PendingBody.
	PendingBody = BodyIndex;
}

void ULedgerWorldBuilder::BuildWorldFor(UWorld& InWorld)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// **Deferred, because everything below configures it before it may run.**
	//
	// SpawnActor dispatches BeginPlay immediately once the world is already
	// running, and only defers it during the world's own begin play. So the
	// first build worked -- the material was assigned before the planet woke --
	// and a rebuild after a body switch did not: the planet woke first, found
	// no surface material, fell back to an engine debug material meant for
	// visualising vertex colours, and the render thread dereferenced null a few
	// seconds later with a breadcrumb saying nothing more specific than
	// "SceneRender".
	//
	// Deferred spawning is the idiom for exactly this. FinishSpawning is below,
	// after the last thing that has to be true before the planet starts.
	//
	// **The planet in the level, when there is one.** ADR-0006: the world's
	// fixed actors are placed, so a person can open the map and select them,
	// and this code says how they behave. Spawning stays as the fallback for a
	// world with no level -- a test, or a map not yet rebuilt -- and that path
	// is still deferred, for the reason above.
	Planet = nullptr;
	bool bPlacedPlanet = false;
	for (TActorIterator<ALedgerPlanet> It(&InWorld); It; ++It)
	{
		Planet = *It;
		bPlacedPlanet = true;
		break;
	}
	if (Planet == nullptr)
	{
		Planet = InWorld.SpawnActorDeferred<ALedgerPlanet>(
			ALedgerPlanet::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	}

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

				// Stones. Until T434 this borrowed the settlement's tree --
				// an eighteen metre cone scaled down to the size of a cobble --
				// which from directly above rendered as a field of small black
				// shards, and is what the overhead capture in
				// docs/comparisons/near-field/ was actually showing.
				TArray<UStaticMesh*> ScatterMeshes;
				for (int32 Variant = 0; Variant < LedgerRock::Variants; ++Variant)
				{
					const FString Name = FString::Printf(TEXT("SM_Rock_%c"),
						static_cast<TCHAR>('A' + Variant));
					const FString Path = FString::Printf(TEXT("%s%s.%s"),
						LedgerMesh::MeshPackageRoot, *Name, *Name);
					if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
					{
						ScatterMeshes.Add(Mesh);
					}
					else
					{
						UE_LOG(LogLedger, Error,
							TEXT("no scatter mesh at %s. Run: "
							     "tools/generate_assets.py --only meshes"), *Path);
					}
				}

				// Lit, and not the engine default. Instanced components take
				// the mesh's own material slots, the bake assigns none, and the
				// default material is what those shards were being drawn with.
				// The tint carries the colour, not the mesh's vertex colours.
				//
				// M_Flat is Tint x VertexColour, and the stones were baked with
				// a warm grey per face -- which never arrives: an instanced
				// static mesh does not deliver the mesh's vertex colours to the
				// shader. White times a colour that is not there is white, and
				// the T437 rainforest capture has arctic boulders in it.
				// Darkening the bake changed nothing, which is what said so.
				//
				// 0.16 linear is rock. Per-stone variation has to come back
				// some other way -- per-instance custom data, or one material
				// per variant the way the settlement's two trees do it.
				Planet->SetScatterMeshes(ScatterMeshes,
					LedgerSurface::CreateFlatMaterial(
						Planet, FLinearColor(0.16f, 0.15f, 0.13f), 0.86f));
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

	// **The body's own numbers, not the planet actor's defaults.** T078.
	//
	// A moon is a body with a different radius and a different mass, and this
	// is the whole of what makes it one. Everything downstream -- the quadtree,
	// the streamer, the materials, the scatter -- reads the radius it is given
	// and does not care which row of the system description it came from.
	if (System.Bodies.IsValidIndex(HomeBody()))
	{
		const FLedgerBody& Home = System.Bodies[HomeBody()];
		Planet->Radius = Home.RadiusMetres * 100.0;

		// Relief scales with the body, but not in proportion: a small body has
		// weaker gravity holding its mountains down, so it carries relatively
		// higher ones. The square root is the usual rough scaling and it keeps
		// a moon from being either a billiard ball or a sea urchin.
		const double RadiusRatio = Home.RadiusMetres / 6.371e6;
		Planet->MaxElevation = 900000.0 * FMath::Sqrt(FMath::Max(RadiusRatio, 0.05));

		// A body with no air has no sea. Sea level below zero means the whole
		// surface is land, which is what an airless body is.
		const bool bHasAir = LedgerSky::RetainsAtmosphere(System, HomeBody(), WhenSeconds);
		Planet->SeaLevel = bHasAir ? 0.14 : -1.0;
		Planet->bHasAtmosphere = bHasAir;
	}

	// The season is a consequence of the orbit, not a switch. T073.
	//
	// Set before anything streams, because a patch carries the snow it was
	// generated with: a season that arrived after the first patches would give
	// a planet with two winters on it.
	if (System.Bodies.IsValidIndex(HomeBody()))
	{
		Planet->AxialTiltRadians = System.Bodies[HomeBody()].AxialTiltRadians;
		Planet->SeasonFromOrbit = LedgerSky::SeasonPhase(System, HomeBody(), WhenSeconds);
	}

	// Everything the planet needs to know is known. It may start.
	//
	// Three cases. Spawned here: finish spawning, which runs BeginPlay now.
	// Placed and not yet started -- the first build, which happens in the
	// world's own begin play before any actor's: nothing to do, its BeginPlay
	// comes next and finds everything set. Placed and already running -- a
	// crossing to another body: grow the terrain again in place.
	if (Planet != nullptr)
	{
		if (!bPlacedPlanet)
		{
			Planet->FinishSpawning(FTransform::Identity);
		}
		else if (Planet->HasActorBegunPlay())
		{
			Planet->Rebuild();
		}
	}

	// Only where there is air to draw. A sky on an airless moon is the single
	// most visible way to get this wrong, and it is one `if` -- and now the
	// `if` is the profile's own answer rather than a second opinion about it.
	const FLedgerAirProfile Air = LedgerAir::For(System, HomeBody(), WhenSeconds);
	const bool bAtmosphere = Air.HasAir();
	//
	// The actor is placed in the level and stays there on every body. On an
	// airless one ConfigureForAir hides the sky and the clouds -- the air is
	// what goes, not the part of the map that would draw it.
	Atmosphere = Placed<ALedgerAtmosphere>(InWorld, Params);
	if (Atmosphere != nullptr && !bAtmosphere)
	{
		Atmosphere->ConfigureForAir(Planet->Radius, Planet->MaxElevation, Air);
	}
	if (bAtmosphere)
	{
		if (Atmosphere != nullptr)
		{
			Atmosphere->ConfigureForAir(Planet->Radius, Planet->MaxElevation, Air);

			// And put the decks where the thermometer says, before anything is
			// drawn rather than on the first clock move.
			const double Latitude = FMath::Asin(
				FMath::Clamp(SiteDirection.GetSafeNormal().Z, -1.0, 1.0));
			const double Longitude = FMath::Atan2(SiteDirection.Y, SiteDirection.X);
			Atmosphere->SetDecks(LedgerCloud::DecksAt(
				System, HomeBody(), Air, Latitude, Longitude, WhenSeconds));
		}
	}

	if (System.Bodies.IsValidIndex(HomeBody()))
	{
		const FLedgerBody& Home = System.Bodies[HomeBody()];
		const double Surface = LedgerEphemeris::GravitationalConstant * Home.MassKg
			/ (Home.RadiusMetres * Home.RadiusMetres);

		// The ship weighs what this body makes it weigh. GM/r^2, in the
		// centimetres the flight model works in -- and no drag at all where
		// there is no air, which is most of what "landing on a moon" feels
		// like: nothing slows you down but the engine.
		if (APlayerController* Controller = InWorld.GetFirstPlayerController())
		{
			if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
			{
				Ship->SurfaceGravity = static_cast<float>(Surface * 100.0);
				Ship->AtmosphericDrag = bAtmosphere ? Ship->AtmosphericDrag : 0.0f;
			}
		}

		UE_LOG(LogLedger, Log,
			TEXT("body %d (%s): radius %.1f km, surface gravity %.2f m/s^2 (%.2f g), "
				 "escape %.0f m/s, %.0f K, atmosphere %s"),
			HomeBody(), LexToString(Home.Kind), Home.RadiusMetres / 1000.0,
			Surface, Surface / 9.80665, LedgerSky::EscapeVelocity(Home),
			LedgerSky::EquilibriumTemperatureKelvin(System, HomeBody(), WhenSeconds),
			bAtmosphere ? TEXT("yes") : TEXT("none"));
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
			Component->SetIntensity(
				static_cast<float>(SunIlluminanceLux(System, WhenSeconds)));

			// The colour is the star's surface temperature, so a cooler star
			// lights its planets redder without anybody picking a tint.
			Component->SetUseTemperature(true);
			if (System.Bodies.Num() > 0)
			{
				Component->SetTemperature(static_cast<float>(
					LedgerSky::StarTemperatureKelvin(System.Bodies[0])));
			}

			Component->SetAtmosphereSunLight(true);
			Component->SetDynamicShadowDistanceMovableLight(600000.0f);

			// **The planet is what makes it night, so the planet has to stop the
			// light.** Shadows reach six kilometres; the body is thirteen
			// thousand across. With the sun forty-two degrees below the horizon
			// the full hundred thousand lux was still arriving -- from beneath,
			// through the planet, on every surface further than six kilometres
			// from the nearest shadow caster -- and a building face glowed
			// white at what the ephemeris called the middle of the night.
			//
			// Not a switch on the site's clock, because a camera in orbit over
			// the dark side still sees the day side lit. The atmosphere already
			// knows the answer for every point: its transmittance along a ray
			// that goes through the ground is zero. Asking it per pixel rather
			// than once for the whole light is what makes the terminator a
			// place on the planet instead of a moment in the frame.
			Component->bPerPixelAtmosphereTransmittance = true;
			Component->MarkRenderStateDirty();
		}
	}

	Sky = Placed<ASkyLight>(InWorld, Params);
	if (Sky != nullptr)
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
			//
			// **A real-time capture happens at the actor's own position, and
			// this actor spawned at the world origin, which on this project is
			// the centre of the planet.** It was capturing a cubemap from six
			// thousand kilometres underground: solid rock in every direction,
			// so the capture was black and the sky contributed exactly nothing
			// to anything, for as long as there has been a sky light. Turning
			// direct lighting off rendered the entire world -- terrain, town,
			// trees -- as a pure black silhouette against a blue sky, which is
			// what found it. Tick moves it to the viewer; see KeepSkyWithViewer.
			// **A static capture, retaken as the viewer and the sun move.** The
			// real-time capture comes back black on this planet: every face the
			// sun did not reach rendered (0,0,0), with Lumen on and with it off,
			// and a magenta lower hemisphere did not show either -- while the
			// same light with bRealTimeCapture off lit every wall. So the
			// capture is taken the old way and KeepSkyWithViewer asks for it
			// again when the viewer or the sun has moved enough to matter.
			// `-skyrealtime` puts the real-time capture back, as the control.
			Component->bRealTimeCapture = FParse::Param(FCommandLine::Get(), TEXT("skyrealtime"));
			// The lower hemisphere from the capture rather than black: "lower"
			// is world -Z, which on a sphere is only down at one pole.
			Component->bLowerHemisphereIsBlack = false;
			// Physical. 3.2 was compensating for a capture that delivered nothing.
			Component->SetIntensity(1.0f);

			// **Control arms for a sky light that lights nothing.** Every face
			// the sun does not reach renders (0,0,0), with Lumen on and with it
			// off. `-skyprobe` paints the capture's lower hemisphere magenta: a
			// magenta wall means the light is applied and its sky half is black;
			// a black wall means the light is not reaching the surface at all.
			// `-skystatic` captures the scene the old way instead of in real time.
			if (FParse::Param(FCommandLine::Get(), TEXT("skyprobe")))
			{
				Component->bLowerHemisphereIsBlack = true;
				Component->LowerHemisphereColor = FLinearColor(1.0f, 0.0f, 1.0f);
			}
			Component->RecaptureSky();
			UE_LOG(LogLedger, Log,
				TEXT("sky light: affects world %d, visible %d, registered %d, intensity %.2f, "
					 "indirect %.2f, real-time %d, lower hemisphere black %d (%s), cast shadows %d"),
				static_cast<int32>(Component->bAffectsWorld), static_cast<int32>(Component->IsVisible()),
				static_cast<int32>(Component->IsRegistered()), Component->Intensity,
				Component->IndirectLightingIntensity, static_cast<int32>(Component->bRealTimeCapture),
				static_cast<int32>(Component->bLowerHemisphereIsBlack),
				*Component->LowerHemisphereColor.ToString(), static_cast<int32>(Component->CastShadows));
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
		// **The range moved with the light.** T076 made the sun about 108,000
		// lux instead of 11, which is four orders of magnitude, and an exposure
		// floor of 0.03 against that is a white frame at noon. These are scaled
		// by the same factor and then opened wider still at the dark end,
		// because the range this world spans is not daylight to shade: it is a
		// total eclipse and a starlit night at one end and full noon at the
		// other, which is more than a camera has and about what an eye has.
		// EV100, not luminance: see DefaultEngine.ini. -4 is a landscape under a
		// bright moon and 19 is sunlit snow, which is the span this world
		// actually contains. Quoting the bounds in stops means they survive the
		// next change to what the light is worth -- which is the whole reason
		// the earlier 0.03-to-8 luminance pair had to be rewritten when T076
		// moved the sun from 11 to 101,367.
		//
		// 19 rather than 17 as headroom, not as a fix: raising it changed no
		// pixel, which is how the real cause of the blown daylight frames was
		// found. The exposure was never clamped -- it was still adapting.
		// A bound that is not being hit cannot be the thing that is wrong.
		// **-8 rather than -4, and this one IS being hit.**
		//
		// -4 is a landscape under a bright moon, and a floor there is a floor
		// below which nothing gets any brighter -- so every frame taken between
		// dusk and dawn came back black, and was chased as a lighting bug, a
		// clock bug and a subsystem-ordering bug in turn. A starlit landscape
		// is nearer -8 and the Milky Way is -9; the span this world contains
		// runs from that to sunlit snow.
		//
		// The distinction from the ceiling above matters: 19 was headroom that
		// changed no pixel, and this is a bound that was clamping every night
		// in the project.
		Settings.bOverride_AutoExposureMinBrightness = true;
		Settings.AutoExposureMinBrightness = -8.0f;
		Settings.bOverride_AutoExposureMaxBrightness = true;
		Settings.AutoExposureMaxBrightness = 19.0f;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 0.1f;
		Settings.bOverride_AutoExposureSpeedUp = true;
		Settings.AutoExposureSpeedUp = 1.2f;
		Settings.bOverride_AutoExposureSpeedDown = true;
		Settings.AutoExposureSpeedDown = 0.8f;
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.35f;

		// **Where in the histogram to meter, which is a question only an
		// airless world forces you to answer.**
		//
		// Under air, the sky lights the shadows and the whole frame sits within
		// a few stops, so metering the middle of the histogram -- the default
		// tenth to ninetieth percentile -- is metering the scene. Without air a
		// shadow receives nothing at all: the frame is sunlit ground and pure
		// black in roughly equal parts, the middle of that histogram is the
		// black, and exposing for it puts every lit surface two stops over the
		// top. Landing on the moon produced exactly that -- white rectangles
		// where the buildings were.
		//
		// So on an airless body the meter is moved onto the lit part. This is
		// what a photographer does there too: sunny sixteen is a highlight
		// rule, not an average one, because on a world with no sky there is
		// nothing meaningful to average.
		if (!bAtmosphere)
		{
			Settings.bOverride_AutoExposureLowPercent = true;
			Settings.AutoExposureLowPercent = 70.0f;
			Settings.bOverride_AutoExposureHighPercent = true;
			Settings.AutoExposureHighPercent = 96.0f;
		}
	}

	// The site has to be chosen before the town can be built on it, and both
	// before the descent aims anywhere.
	ChooseSite();

	// **What the site got, in the two frames that argue about it.** The site is
	// picked for daylight, so these two must agree: the dot product against the
	// sun vector the picker used, and the solar altitude the sky reports for
	// the same direction. When a fixture insists a freshly chosen site is in
	// the dark, one of these is lying and this says which.
	UE_LOG(LogLedger, Log,
		TEXT("site: body %d, sun.site %.3f, solar altitude %.1f deg"),
		HomeBody(), FVector3d::DotProduct(SiteDirection.GetSafeNormal(), SunFacing),
		FMath::RadiansToDegrees(LedgerSky::SolarAltitude(
			System, HomeBody(), SiteDirection.GetSafeNormal(), WhenSeconds)));

	// Placed, like the planet; Build lays it out again for whichever body this
	// is, clearing what the last one had.
	Settlement = Placed<ALedgerSettlement>(InWorld, Params);
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

		// Three stones. Angular, mixed, and water-worn -- the one axis of
		// variety that reads at the size these are drawn at. T434.
		for (int32 Variant = 0; Variant < LedgerRock::Variants; ++Variant)
		{
			FLedgerMeshBuilder Builder;
			LedgerRock::Describe(Builder, 0x0C0B15u + Variant * 7919u,
				Variant / static_cast<double>(LedgerRock::Variants - 1));
			const FString Name = FString::Printf(TEXT("SM_Rock_%c"),
				static_cast<TCHAR>('A' + Variant));
			FString Line;
			// No Nanite, same reason as the trees: the colour is per-vertex and
			// Nanite does not carry mesh vertex colours to the material. At 320
			// triangles there is nothing for it to do anyway.
			if (LedgerMesh::Bake(Builder,
				FString(LedgerMesh::MeshPackageRoot) + Name, *Name, Line,
				/*bNanite*/ false) != nullptr)
			{
				++Saved;
			}
			Body += Line + TEXT("\n");
		}

		// The town. Two meshes where there was one procedural buffer rebuilt
		// every launch: a unit building every instance scales, and the pad at
		// its real size. No Nanite -- twenty-four triangles each -- but a
		// distance field and collision, which the procedural buffer had half
		// of and Lumen none.
		{
			FLedgerMeshBuilder Builder;
			const int32 RoofStartsAt = ALedgerSettlement::DescribeBuilding(Builder);
			FString Line;
			if (LedgerMesh::Bake(Builder,
				FString(LedgerMesh::MeshPackageRoot) + TEXT("SM_Building"),
				TEXT("SM_Building"), Line, /*bNanite*/ false, RoofStartsAt) != nullptr)
			{
				++Saved;
			}
			Body += Line + TEXT("\n");
		}
		{
			FLedgerMeshBuilder Builder;
			const int32 StripesStartAt = ALedgerSettlement::DescribePad(Builder);
			FString Line;
			if (LedgerMesh::Bake(Builder,
				FString(LedgerMesh::MeshPackageRoot) + TEXT("SM_Pad"),
				TEXT("SM_Pad"), Line, /*bNanite*/ false, StripesStartAt) != nullptr)
			{
				++Saved;
			}
			Body += Line + TEXT("\n");
		}

		constexpr int32 Expected = 5 + LedgerRock::Variants;
		Body += FString::Printf(TEXT("\n  %d of %d saved\n\n"), Saved, Expected);
		Body += FString::Printf(TEXT("VERDICT: %s\n"),
			Saved == Expected ? TEXT("PASS") : TEXT("FAIL"));

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
	const double GoldenAngle = LedgerPi * (3.0 - FMath::Sqrt(5.0));

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
		const double GoldenAngle = LedgerPi * (3.0 - FMath::Sqrt(5.0));
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
