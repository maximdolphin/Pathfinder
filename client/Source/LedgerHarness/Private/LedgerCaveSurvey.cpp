#include "LedgerCaveSurvey.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerShip.h"
#include "UnrealClient.h"
#include "HAL/PlatformMisc.h"
#include "LedgerCaves.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/// The local lattice a passage is flood-filled on.
	///
	/// Eight metres, because the passage radius is fourteen and a lattice
	/// coarser than the passage cannot tell a tunnel from a chain of unconnected
	/// bubbles. 256 x 256 x 96 at that spacing is a box two kilometres square
	/// and 768 m deep, which is more than a shell's worth of rock.
	constexpr double CellMetres = 8.0;
	constexpr int32 BoxWide = 256;
	constexpr int32 BoxDeep = 96;

	struct FLocalFrame
	{
		FVector3d Up;
		FVector3d East;
		FVector3d North;
		double SurfaceMetres = 0.0;
	};

	FLocalFrame FrameAt(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params)
	{
		FLocalFrame Frame;
		Frame.Up = UnitSphere;
		Frame.East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), UnitSphere);
		if (Frame.East.IsNearlyZero())
		{
			Frame.East = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), UnitSphere);
		}
		Frame.East.Normalize();
		Frame.North = FVector3d::CrossProduct(UnitSphere, Frame.East).GetSafeNormal();
		Frame.SurfaceMetres = LedgerTerrain::Elevation(UnitSphere, Params) / 100.0;
		return Frame;
	}

	/// A point in the box, as a direction and an altitude.
	void BoxPoint(const FLocalFrame& Frame, const FLedgerTerrainParams& Params,
		int32 X, int32 Y, int32 Z, FVector3d& OutDirection, double& OutAltitude)
	{
		const double RadiusMetres = Params.Radius / 100.0;
		const double EastMetres = (X - BoxWide / 2) * CellMetres;
		const double NorthMetres = (Y - BoxWide / 2) * CellMetres;

		OutDirection = (Frame.Up
			+ Frame.East * (EastMetres / RadiusMetres)
			+ Frame.North * (NorthMetres / RadiusMetres)).GetSafeNormal();

		// Relative to the ground under the *centre* of the box, so the box is a
		// slab of rock rather than a shape that follows every hillside -- a
		// passage crossing a valley has to stay in the box.
		OutAltitude = Frame.SurfaceMetres + LedgerCaves::ShellAboveMetres - Z * CellMetres;
	}
}

bool ULedgerCaveSurvey::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerCaveSurvey::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!FParse::Param(FCommandLine::Get(), TEXT("caves")))
	{
		return;
	}

	bSurveyed = true;
	const bool bHolds = WriteSurvey();
	UE_LOG(LogLedger, Log, TEXT("cave survey: %s"),
		bHolds ? TEXT("VERDICT PASS") : TEXT("VERDICT FAIL"));

	// -cavephoto keeps the run alive to photograph the mouth it just measured.
	// A mesher nobody has looked at is the failure this project keeps writing
	// down: a report with a VERDICT line in it is not evidence that the thing
	// being measured is on screen.
	if (!FParse::Param(FCommandLine::Get(), TEXT("cavephoto")) || !bFound)
	{
		FPlatformMisc::RequestExit(false);
	}
}

namespace
{
	/// Framings of a cave mouth: standing outside it, at its lip, and inside
	/// looking back out at the daylight.
	struct FCaveShot
	{
		const TCHAR* Name;
		double EyeHeightMetres;
		double AlongMetres;
		double LookAtHeightMetres;
	};

	const FCaveShot CaveShots[] =
	{
		{ TEXT("cave-approach.png"),  12.0,  -90.0,   0.0 },
		{ TEXT("cave-mouth.png"),      3.0,  -25.0,  -6.0 },
		// Actually inside: below the lip, looking along the passage. The first
		// version put this one two metres above the ground thirty-five metres
		// away, which is a photograph of a desert with a slot in it -- the same
		// picture as the other two.
		{ TEXT("cave-inside.png"),   -18.0,    0.0, -30.0 },
	};

	constexpr double CaveSettleSeconds = 6.0;
}

void ULedgerCaveSurvey::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bSurveyed || !bFound || DeltaSeconds <= 0.0f)
	{
		return;
	}
	if (!FParse::Param(FCommandLine::Get(), TEXT("cavephoto")))
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr || Controller->GetPawn() == nullptr)
	{
		return;
	}

	if (Shot >= UE_ARRAY_COUNT(CaveShots))
	{
		UE_LOG(LogLedger, Log, TEXT("cave photographs: done"));
		FPlatformMisc::RequestExit(false);
		return;
	}

	Place();

	Settle += DeltaSeconds;
	if (Settle < CaveSettleSeconds)
	{
		return;
	}

	if (!bCaptured)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString(CaveShots[Shot].Name)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogLedger, Log, TEXT("cave photograph -> %s"), *Path);
		bCaptured = true;
		return;
	}

	++Shot;
	Settle = 0.0;
	bCaptured = false;
}

void ULedgerCaveSurvey::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder->GetPlanet();
	APlayerController* Controller = World->GetFirstPlayerController();

	const FCaveShot& Current = CaveShots[Shot];
	const FLedgerTerrainParams Params = Planet->TerrainParams();
	const FLocalFrame Frame = FrameAt(Mouth, Params);

	const double RadiusMetres = Params.Radius / 100.0;
	const FVector3d EyeDirection = (Frame.Up
		+ Frame.East * (Current.AlongMetres / RadiusMetres)).GetSafeNormal();
	const double EyeGround = LedgerTerrain::Elevation(EyeDirection, Params) / 100.0;

	const FVector Eye = FVector(Planet->GetActorLocation())
		+ FVector(EyeDirection * ((RadiusMetres + EyeGround + Current.EyeHeightMetres) * 100.0));
	const FVector Target = FVector(Planet->GetActorLocation())
		+ FVector(Frame.Up * ((RadiusMetres + Frame.SurfaceMetres
			+ Current.LookAtHeightMetres) * 100.0));

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		// Hidden rather than moved: a hull parked overhead casts a
		// hull-shaped shadow, which the biome captures learned the hard way.
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye + FVector(Frame.Up * 400000.0));
	}

	// Built from the local up, not from the world's.
	//
	// FVector::Rotation() gives a rotator with zero roll, which means the
	// camera's up is world Z projected -- and on a sphere world Z is only
	// vertical at one longitude. Looking east from fourteen degrees north put
	// the horizon down the side of the frame at sixty degrees. The biome
	// captures escaped this by looking north, where the projection happens to
	// land on the local up; that was luck, not correctness.
	const FRotator Look = FRotationMatrix::MakeFromXZ(
		Target - Eye, FVector(Frame.Up)).Rotator();
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Eye, Look, SpawnParams);
		if (Camera != nullptr)
		{
			if (UCameraComponent* Component = Camera->GetCameraComponent())
			{
				// Auto exposure left ON here, unlike every other fixture in
				// this harness. A cave is a dark room with a bright doorway and
				// the eye adapts walking into one; a pinned exposure would show
				// either a white doorway or a black passage, and the thing to
				// look at is whether the two meet.
				Component->PostProcessSettings.bOverride_AutoExposureBias = true;
				Component->PostProcessSettings.AutoExposureBias = 0.0f;
			}
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

TStatId ULedgerCaveSurvey::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerCaveSurvey, STATGROUP_Tickables);
}

bool ULedgerCaveSurvey::WriteSurvey()
{
	ULedgerWorldBuilder* Builder = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Planet == nullptr)
	{
		UE_LOG(LogLedger, Error, TEXT("cave survey: no planet"));
		return false;
	}

	const FLedgerTerrainParams Params = Planet->TerrainParams();

	FString Body;
	Body += TEXT("Caves, measured before anything is meshed (T056).\n\n");

	// ---- how much of the planet has caves under it -------------------------
	int32 Sampled = 0;
	int32 InCaveCountry = 0;
	double CountryArea = 0.0;
	double TotalArea = 0.0;
	for (int32 LatStep = -85; LatStep <= 85; LatStep += 1)
	{
		for (int32 LonStep = 0; LonStep < 360; LonStep += 1)
		{
			const double Lat = FMath::DegreesToRadians(static_cast<double>(LatStep));
			const double Lon = FMath::DegreesToRadians(static_cast<double>(LonStep));
			const FVector3d Point(
				FMath::Cos(Lat) * FMath::Cos(Lon),
				FMath::Cos(Lat) * FMath::Sin(Lon),
				FMath::Sin(Lat));
			const double Weight = FMath::Cos(Lat);

			++Sampled;
			TotalArea += Weight;
			if (LedgerCaves::MightContainCaves(Point.GetSafeNormal(), 0.0, Params))
			{
				++InCaveCountry;
				CountryArea += Weight;
			}
		}
	}

	Body += FString::Printf(
		TEXT("cave country: %.1f%% of the surface by area (%d of %d samples)\n\n"),
		100.0 * CountryArea / FMath::Max(1.0, TotalArea), InCaveCountry, Sampled);

	// ---- find a mouth ------------------------------------------------------
	//
	// A mouth is open cave at or just below the surface on land. Searched on a
	// coarse sweep first, because the field is empty nearly everywhere and a
	// fine search over the whole planet would be a hundred times the work for
	// the same answer.
	bool bFoundMouth = false;
	int32 MouthsSeen = 0;
	for (double Lat = -80.0; Lat <= 80.0 && MouthsSeen < 5000; Lat += 0.25)
	{
		for (double Lon = 0.0; Lon < 360.0; Lon += 0.25)
		{
			const double LatRadians = FMath::DegreesToRadians(Lat);
			const double LonRadians = FMath::DegreesToRadians(Lon);
			const FVector3d Point = FVector3d(
				FMath::Cos(LatRadians) * FMath::Cos(LonRadians),
				FMath::Cos(LatRadians) * FMath::Sin(LonRadians),
				FMath::Sin(LatRadians)).GetSafeNormal();

			if (!LedgerCaves::MightContainCaves(Point, 0.0, Params))
			{
				continue;
			}
			if (LedgerTerrain::Elevation(Point, Params) <= 0.0)
			{
				continue;
			}

			// Open right at the ground is what makes it a mouth rather than a
			// void: a passage sealed under a hillside is not somewhere anybody
			// can walk into.
			const double Ground = LedgerTerrain::Elevation(Point, Params) / 100.0;
			if (LedgerCaves::Density(Point, Ground, Ground, Params) > 0.0)
			{
				++MouthsSeen;

				// Counted wherever it is; photographed only where the sun is.
				// The first mouth this found was at -57 lat on the night side
				// and the photographs came back as three black frames, which is
				// a correct search and a useless picture. The count is a
				// property of the planet and stays whole-planet; the choice of
				// subject is a property of the camera.
				const FVector3d SunDirection = Builder->GetSunFacing().GetSafeNormal();
				if (!bFoundMouth && FVector3d::DotProduct(Point, SunDirection) > 0.35)
				{
					Mouth = Point;
					bFoundMouth = true;
					bFound = true;
				}
			}
		}
	}

	Body += FString::Printf(TEXT("surface mouths found in a quarter-degree sweep: %d\n"),
		MouthsSeen);

	if (!bFoundMouth)
	{
		Body += TEXT("\nNo mouth anywhere. Nothing to mesh and nothing to walk into.\n");
		Body += TEXT("\nVERDICT: FAIL\n");
		const FString EmptyPath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("caves.txt")));
		FFileHelper::SaveStringToFile(Body, *EmptyPath);
		return false;
	}

	const double MouthLat = FMath::RadiansToDegrees(
		FMath::Asin(FMath::Clamp(Mouth.Z, -1.0, 1.0)));
	const double MouthLon = FMath::RadiansToDegrees(FMath::Atan2(Mouth.Y, Mouth.X));
	Body += FString::Printf(TEXT("first mouth at %.2f lat, %.2f lon, ground %.0f m\n\n"),
		MouthLat, MouthLon, LedgerTerrain::Elevation(Mouth, Params) / 100.0);

	// ---- flood fill the passage --------------------------------------------
	//
	// The question the acceptance actually asks: does a mouth lead anywhere,
	// and does what it leads to come back out. A flood fill on a local lattice
	// answers both, and does it without a mesher, a collision shape or a pawn.
	const FLocalFrame Frame = FrameAt(Mouth, Params);
	const int32 Cells = BoxWide * BoxWide * BoxDeep;

	TArray<uint8> Open;
	Open.SetNumZeroed(Cells);
	int32 OpenCells = 0;
	for (int32 Z = 0; Z < BoxDeep; ++Z)
	{
		for (int32 Y = 0; Y < BoxWide; ++Y)
		{
			for (int32 X = 0; X < BoxWide; ++X)
			{
				FVector3d Direction;
				double Altitude = 0.0;
				BoxPoint(Frame, Params, X, Y, Z, Direction, Altitude);

				// Open means inside a cave AND under the ground. Above the
				// ground it is simply outdoors, and counting that as cave would
				// flood the whole sky.
				const double Ground = LedgerTerrain::Elevation(Direction, Params) / 100.0;
				const bool bUnderground = Altitude < Ground;
				if (bUnderground
					&& LedgerCaves::Density(Direction, Altitude, Ground, Params) > 0.0)
				{
					Open[(Z * BoxWide + Y) * BoxWide + X] = 1;
					++OpenCells;
				}
			}
		}
	}

	Body += FString::Printf(
		TEXT("local box %d x %d x %d cells at %.0f m: %d open (%.3f%% of the rock)\n"),
		BoxWide, BoxWide, BoxDeep, CellMetres, OpenCells, 100.0 * OpenCells / Cells);

	// Flood from every open cell adjacent to the mouth column.
	TArray<int32> Frontier;
	TArray<uint8> Seen;
	Seen.SetNumZeroed(Cells);
	for (int32 Z = 0; Z < BoxDeep; ++Z)
	{
		const int32 Index = (Z * BoxWide + BoxWide / 2) * BoxWide + BoxWide / 2;
		if (Open[Index] != 0 && Seen[Index] == 0)
		{
			Seen[Index] = 1;
			Frontier.Add(Index);
		}
	}

	int32 Reached = 0;
	int32 DeepestZ = 0;
	int32 MinX = BoxWide;
	int32 MaxX = 0;
	int32 MinY = BoxWide;
	int32 MaxY = 0;
	int32 SurfaceTouches = 0;
	TArray<int32> Breaches;

	while (Frontier.Num() > 0)
	{
		const int32 Index = Frontier.Pop(EAllowShrinking::No);
		++Reached;

		const int32 X = Index % BoxWide;
		const int32 Y = (Index / BoxWide) % BoxWide;
		const int32 Z = Index / (BoxWide * BoxWide);
		MinX = FMath::Min(MinX, X); MaxX = FMath::Max(MaxX, X);
		MinY = FMath::Min(MinY, Y); MaxY = FMath::Max(MaxY, Y);
		DeepestZ = FMath::Max(DeepestZ, Z);

		// A breach is an open cell whose cell above is outdoors: that is where
		// this passage meets the sky, and "out the other side" means finding
		// two of them a long way apart.
		if (Z > 0)
		{
			FVector3d Direction;
			double Altitude = 0.0;
			BoxPoint(Frame, Params, X, Y, Z - 1, Direction, Altitude);
			if (Altitude >= LedgerTerrain::Elevation(Direction, Params) / 100.0)
			{
				++SurfaceTouches;
				Breaches.Add(Index);
			}
		}

		for (int32 Axis = 0; Axis < 6; ++Axis)
		{
			static const int32 StepX[6] = { -1, 1, 0, 0, 0, 0 };
			static const int32 StepY[6] = { 0, 0, -1, 1, 0, 0 };
			static const int32 StepZ[6] = { 0, 0, 0, 0, -1, 1 };
			const int32 NextX = X + StepX[Axis];
			const int32 NextY = Y + StepY[Axis];
			const int32 NextZ = Z + StepZ[Axis];
			if (NextX < 0 || NextX >= BoxWide || NextY < 0 || NextY >= BoxWide
				|| NextZ < 0 || NextZ >= BoxDeep)
			{
				continue;
			}
			const int32 Next = (NextZ * BoxWide + NextY) * BoxWide + NextX;
			if (Open[Next] != 0 && Seen[Next] == 0)
			{
				Seen[Next] = 1;
				Frontier.Add(Next);
			}
		}
	}

	// The two breaches furthest apart: how far you could walk in one mouth and
	// out another without ever leaving the passage.
	double FurthestApart = 0.0;
	for (int32 First = 0; First < Breaches.Num(); ++First)
	{
		for (int32 Second = First + 1; Second < Breaches.Num(); ++Second)
		{
			const int32 AX = Breaches[First] % BoxWide;
			const int32 AY = (Breaches[First] / BoxWide) % BoxWide;
			const int32 BX = Breaches[Second] % BoxWide;
			const int32 BY = (Breaches[Second] / BoxWide) % BoxWide;
			FurthestApart = FMath::Max(FurthestApart,
				FVector2d(AX - BX, AY - BY).Length() * CellMetres);
		}
	}

	Body += FString::Printf(
		TEXT("the passage from that mouth: %d cells, %.0f m across, %.0f m deep\n"),
		Reached, FMath::Max(MaxX - MinX, MaxY - MinY) * CellMetres, DeepestZ * CellMetres);
	Body += FString::Printf(
		TEXT("it meets the sky at %d cells, and its two furthest mouths are %.0f m apart\n"),
		SurfaceTouches, FurthestApart);

	// ---- the acceptance, minus the walking ---------------------------------
	//
	// A passage you can enter and leave by a different mouth, at least a
	// hundred metres apart so it is a route rather than a dent in a hillside.
	const bool bThroughRoute = FurthestApart >= 100.0;
	Body += FString::Printf(
		TEXT("\nin one mouth and out another, at least 100 m apart: %s\n"),
		bThroughRoute ? TEXT("yes") : TEXT("NO"));
	Body += TEXT("walking it is M09's business; there is no pawn with legs yet.\n");
	Body += FString::Printf(TEXT("\nVERDICT: %s\n"), bThroughRoute ? TEXT("PASS") : TEXT("FAIL"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("caves.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("cave survey -> %s"), *Path);
	return bThroughRoute;
}
