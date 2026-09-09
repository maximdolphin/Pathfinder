// The parts of the scripted flight that go somewhere specific to photograph a
// specific claim: the ridge sweep for the patch cache, the coast and the
// waterline crossing for the water, the transect for the bathymetric profile.
//
// Separate from the reentry because they are separate fixtures that happen to
// share a timeline. The reentry proves there is no seam between orbit and
// ground; these prove individual things about the terrain, and each one exists
// because a milestone gate asked a question no other view could answer.

#include "LedgerFlightHarness.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerTerrainMath.h"
#include "LedgerWorld.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// ------------------------------------------------------------ ridge sweep

namespace
{
	/// Half-length of the sweep line, how high above the ground it runs, and
	/// how long one out-and-back takes.
	///
	/// Three kilometres in ten seconds is 300 m/s — fast, and still an aircraft.
	/// The first attempt swept eighteen kilometres in four, which is Mach 26:
	/// it covered new ground the entire time, generated thirteen thousand
	/// patches, and measured the streamer's throughput rather than the cache.
	/// A cache can only be tested by a path that repeats.
	constexpr double SweepReach = 150000.0;    // 1.5 km each way
	constexpr double SweepAltitude = 90000.0;  // 900 m
	constexpr double SweepPeriod = 10.0;       // one out-and-back
	constexpr int32 SweepPasses = 3;
}

void ULedgerFlightHarness::BeginRidgeSweep()
{
	MarkPhase(TEXT("ridge sweep"));
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	if (World == nullptr || Ship == nullptr || Planet() == nullptr)
	{
		return;
	}

	Ship->SetFlightEnabled(false);
	Ship->SetVelocity(FVector::ZeroVector);

	SweepElapsed = 0.0;
	SweepStartBuilds = Planet()->GetStats().TotalBuilds;
	SweepStartHits = Planet()->GetStats().CacheHits;

	World->GetTimerManager().SetTimer(
		SweepStepTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::StepRidgeSweep),
		1.0f / 60.0f, true);
	World->GetTimerManager().SetTimer(
		SweepEndTimer,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::EndRidgeSweep),
		SweepPasses * SweepPeriod, false);

	UE_LOG(LogLedger, Log, TEXT("sweep: %d passes over %.1f km, from %lld patches built"),
		SweepPasses, SweepReach * 2.0 / 100000.0, SweepStartBuilds);
}

void ULedgerFlightHarness::StepRidgeSweep()
{
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet() == nullptr)
	{
		return;
	}

	SweepElapsed += 1.0 / 60.0;

	// A triangle wave, not a sine: constant ground speed, so each pass covers
	// the same patches at the same rate and the second one is comparable with
	// the first. A sine would dawdle at the ends and hurry through the middle.
	const double Phase = FMath::Fmod(SweepElapsed / SweepPeriod, 1.0);
	const double Along = (Phase < 0.5 ? Phase * 4.0 - 1.0 : 3.0 - Phase * 4.0);

	const FVector3d Up = Site();
	const FVector3d Track = FVector3d::CrossProduct(Up, FVector3d::UpVector).GetSafeNormal();

	// Offset along the surface, then re-normalised: a straight line in the
	// tangent plane leaves the sphere, and at nine kilometres that is already
	// several metres of altitude error.
	const FVector3d Direction = (Up + Track * (Along * SweepReach / Planet()->Radius)).GetSafeNormal();
	const double Ground = Planet()->SurfaceRadiusAt(Direction);

	Ship->SetActorLocation(Planet()->GetActorLocation() + FVector(Direction * (Ground + SweepAltitude)));

	const FVector Facing = FVector(Track * (Along >= 0.0 ? 1.0 : -1.0));
	const FRotator Attitude = FRotationMatrix::MakeFromXZ(Facing, FVector(Direction)).Rotator();
	Ship->SetActorRotation(Attitude);
	Controller->SetControlRotation(Attitude);
}

void ULedgerFlightHarness::EndRidgeSweep()
{
	UWorld* World = GetWorld();
	if (World == nullptr || Planet() == nullptr)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(SweepStepTimer);

	const int64 Built = Planet()->GetStats().TotalBuilds - SweepStartBuilds;
	const int64 Reused = Planet()->GetStats().CacheHits - SweepStartHits;
	UE_LOG(LogLedger, Log,
		TEXT("sweep done: %lld patches generated, %lld served from cache (%.0f%% reused)"),
		Built, Reused,
		(Built + Reused) > 0 ? 100.0 * static_cast<double>(Reused) / static_cast<double>(Built + Reused) : 0.0);

	if (GEngine != nullptr)
	{
		GEngine->Exec(World, TEXT("Ledger.Terrain.Stats"));
	}

	// The numbers say the geometry was reused; this says it survived the round
	// trip through the component. A section put back with SetProcMeshSection
	// that had lost its stitching would show as cracks along the LOD seams.
	Capture(TEXT("terrain-sweep.png"));
}

void ULedgerFlightHarness::FrameCoast()
{
	MarkPhase(TEXT("coast"));
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Ship == nullptr || Controller == nullptr || Planet() == nullptr)
	{
		return;
	}

	const FLedgerTerrainParams Params = Planet()->TerrainParams();

	// Find a beach: land, in daylight, with open water within a few kilometres.
	// Scoring on "just above sea level with a deep neighbour" finds a shoreline
	// rather than a lake edge or a cliff over a shallow shelf.
	FVector3d Best = Site();
	FVector3d BestSeaward = FVector3d::ZeroVector;
	double BestScore = -MAX_dbl;

	constexpr int32 SurveySamples = 8192;
	const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));

	for (int32 Index = 0; Index < SurveySamples; ++Index)
	{
		const double Y = 1.0 - (static_cast<double>(Index) / (SurveySamples - 1)) * 2.0;
		const double RadiusAtY = FMath::Sqrt(FMath::Max(0.0, 1.0 - Y * Y));
		const double Theta = GoldenAngle * static_cast<double>(Index);
		const FVector3d Candidate = FVector3d(
			FMath::Cos(Theta) * RadiusAtY, Y, FMath::Sin(Theta) * RadiusAtY).GetSafeNormal();

		if (FVector3d::DotProduct(Candidate, Sun()) < 0.80)
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
	const double SurfaceRadius = Planet()->SurfaceRadiusAt(Best);

	// Sixty metres up and a little back from the waterline, looking out to sea
	// with the horizon in frame. Low enough that the surface reads as a surface.
	const FVector Location = Planet()->GetActorLocation()
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
	DumpBathymetry();

	FTimerHandle Shot;
	World->GetTimerManager().SetTimer(
		Shot,
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::Capture, TEXT("terrain-coast.png")),
		6.0f,
		false);
}

void ULedgerFlightHarness::DumpBathymetry()
{
	if (Planet() == nullptr || CoastSeaward.IsNearlyZero())
	{
		return;
	}

	const FLedgerTerrainParams Params = Planet()->TerrainParams();

	// Out to half a radian, about 3,200 km — far enough to leave a shelf even
	// if the shelf is wide.
	constexpr int32 Steps = 96;
	constexpr double Arc = 0.5;

	// The coast search picked its seaward direction by probing six kilometres,
	// which is enough to tell water from land and not enough to tell a basin
	// from a lagoon. Re-pick it here against the far end of the line: a
	// transect down the length of an archipelago shows nothing but islands.
	const FVector3d Tangent = FVector3d::CrossProduct(CoastSite, FVector3d::UpVector).GetSafeNormal();
	const FVector3d Bitangent = FVector3d::CrossProduct(CoastSite, Tangent);

	FVector3d Seaward = CoastSeaward;
	double BestFar = MAX_dbl;
	for (int32 Probe = 0; Probe < 32; ++Probe)
	{
		const double Angle = (Probe / 32.0) * 2.0 * PI;
		const FVector3d Direction = Tangent * FMath::Cos(Angle) + Bitangent * FMath::Sin(Angle);

		// Score on the mean over the outer half, not the single far point, so a
		// direction is not chosen by one trench it happens to end in.
		double Sum = 0.0;
		for (int32 Step = Steps / 2; Step < Steps; ++Step)
		{
			const double Along = (static_cast<double>(Step) / (Steps - 1)) * Arc;
			Sum += LedgerTerrain::Elevation(
				(CoastSite + Direction * FMath::Tan(Along)).GetSafeNormal(), Params);
		}
		if (Sum < BestFar)
		{
			BestFar = Sum;
			Seaward = Direction;
		}
	}

	TArray<double> Depths;
	TArray<double> Offshore;
	Depths.Reserve(Steps);
	Offshore.Reserve(Steps);
	double Deepest = 0.0;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const double Along = (static_cast<double>(Step) / (Steps - 1)) * Arc;
		const FVector3d Sample = (CoastSite + Seaward * FMath::Tan(Along)).GetSafeNormal();
		const double Metres = LedgerTerrain::Elevation(Sample, Params) / 100.0;
		Depths.Add(Metres);
		Offshore.Add(LedgerTerrain::OffshoreParameter(Sample, Params));
		Deepest = FMath::Min(Deepest, Metres);
	}

	FString Out = TEXT("Depth transect from the town coast, along the direction with the\n");
	Out += TEXT("deepest far field, which on this planet crosses the landmass first.\n");
	Out += TEXT("Positive metres are land; the bar plots depth only.\n");
	Out += TEXT("t is the offshore parameter the profile is a function of: shelf below\n");
	Out += TEXT("0.10, continental slope to 0.26, abyssal plain beyond.\n");
	Out += FString::Printf(TEXT("planet radius %.0f km, deepest on this line %.0f m\n\n"),
		Planet()->Radius / 100000.0, -Deepest);
	Out += TEXT("    km      m      t   profile\n");

	constexpr int32 Columns = 64;
	for (int32 Step = 0; Step < Depths.Num(); ++Step)
	{
		const double Along = (static_cast<double>(Step) / (Steps - 1)) * Arc;
		const double Kilometres = Along * Planet()->Radius / 100000.0;
		const double Metres = Depths[Step];

		const int32 Filled = Deepest < 0.0
			? FMath::Clamp(FMath::RoundToInt((Metres / Deepest) * Columns), 0, Columns)
			: 0;

		Out += FString::Printf(TEXT("%6.0f %6.0f %6.3f   |%s%s\n"),
			Kilometres, Metres, Offshore[Step],
			*FString::ChrN(Filled, TEXT('#')),
			*FString::ChrN(Columns - Filled, TEXT('.')));
	}

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("bathymetry.txt")));
	FFileHelper::SaveStringToFile(Out, *Path);
	UE_LOG(LogLedger, Log, TEXT("bathymetry -> %s (deepest %.0f m)"), *Path, -Deepest);
}

void ULedgerFlightHarness::FrameUnderwater()
{
	MarkPhase(TEXT("underwater"));
	UWorld* World = GetWorld();
	ALedgerShip* Ship = GetShip();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (World == nullptr || Planet() == nullptr || Ship == nullptr || Controller == nullptr
		|| CoastSeaward.IsNearlyZero())
	{
		return;
	}

	// Walk seaward until there is enough water to stand a camera in. A fixed
	// distance worked while the seabed dropped away at the shoreline; over a
	// real shelf the first few kilometres are ankle deep.
	FVector3d Offshore = (CoastSite + CoastSeaward * 0.00095).GetSafeNormal();
	for (int32 Step = 1; Step <= 200; ++Step)
	{
		const FVector3d Candidate = (CoastSite + CoastSeaward * (0.00025 * Step)).GetSafeNormal();
		Offshore = Candidate;
		if (Planet()->Radius - Planet()->SurfaceRadiusAt(Candidate) > 1500.0)
		{
			break;
		}
	}
	const FVector Up(Offshore);
	const double Floor = Planet()->SurfaceRadiusAt(Offshore);
	const double Depth = Planet()->Radius - Floor;

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
	const FVector Location = Planet()->GetActorLocation()
		+ FVector(Offshore * (Planet()->Radius - CameraDepth)) - Look * 700.0;

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
		FTimerDelegate::CreateUObject(this, &ULedgerFlightHarness::Capture, TEXT("terrain-underwater.png")),
		4.0f,
		false);
}
