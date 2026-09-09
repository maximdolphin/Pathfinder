#include "LedgerCliffSite.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerClimate.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr double SettleSeconds = 6.0;

	/// The spacing slope is measured at: the finest the mesh resolves, a node
	/// edge of 305 m over 64 quads. A slope finer than that is a slope no
	/// vertex has, and reporting one would be reporting the height function
	/// rather than the ground.
	constexpr double SampleMetres = 4.8;

	/// Framings, as multiples of the drop rather than in metres.
	///
	/// A fixed distance frames one cliff and misses every other: 120 m back
	/// from a 374 m face is standing at the bottom of it looking at rock.
	struct FCliffShot
	{
		const TCHAR* Name;
		double BackDrops;
		double UpDrops;
	};

	const FCliffShot CliffShots[] =
	{
		// Across the face from below, so the foot of it is in frame -- which is
		// where the debris is, and the half of the acceptance a photograph of
		// the face alone would miss.
		// Close. The terrain material fades its detail to neutral by 600 m
		// (LedgerTerrainMaterial.cpp), so a shot framed 530 m off a 295 m face
		// photographs the fade rather than the rock -- which is what the first
		// framing did, and the reason the face came back smooth and white.
		{ TEXT("cliff-foot.png"),   0.30,  0.02 },
		{ TEXT("cliff-face.png"),   0.60,  0.18 },
		{ TEXT("cliff-wide.png"),   1.60,  0.55 },
	};

	FVector3d OnSphere(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat)).GetSafeNormal();
	}
}

bool ULedgerCliffSite::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerCliffSite::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("cliffsite"));
}

bool ULedgerCliffSite::FindFace()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr
		? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Planet == nullptr)
	{
		return false;
	}

	const FLedgerTerrainParams Params = Planet->TerrainParams();
	const FVector3d SunDirection = Builder->GetSunFacing().GetSafeNormal();
	const double Step = (SampleMetres * 100.0) / Params.Radius;

	double Steepest = 0.0;
	for (double Latitude = -80.0; Latitude <= 80.0; Latitude += 0.2)
	{
		for (double Longitude = 0.0; Longitude < 360.0; Longitude += 0.2)
		{
			const FVector3d Point = OnSphere(Latitude, Longitude);

			// Lit, because a black frame cannot tell rock from nothing -- three
			// captures were misread that way in one session before every
			// fixture started asking.
			if (FVector3d::DotProduct(Point, SunDirection) < 0.2)
			{
				continue;
			}
			const double Here = LedgerTerrain::Elevation(Point, Params);
			if (Here <= 0.0)
			{
				continue;
			}

			// Above freezing, so the face is rock rather than snow.
			//
			// The previous run found a lit 59-degree face at 1,900 m and sixty
			// north and photographed a white slope: on an ice cap the biome
			// tint is 0.85 and neither bedding nor scree can show through it.
			// The latitude band and the lapse rate, inline -- the full climate
			// call marches forty steps upwind and this runs on every candidate
			// on the planet.
			const double SinLat = FMath::Clamp(Point.Z, -1.0, 1.0);
			const double TemperatureC =
				FMath::Lerp(LedgerClimate::EquatorC, LedgerClimate::PoleC, SinLat * SinLat)
				- LedgerClimate::LapseRateCPerKm * (Here / 100000.0);
			if (TemperatureC < 6.0)
			{
				continue;
			}

			FVector3d East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Point);
			if (East.IsNearlyZero())
			{
				continue;
			}
			East.Normalize();
			const FVector3d North = FVector3d::CrossProduct(Point, East).GetSafeNormal();

			const double RiseEast = LedgerTerrain::Elevation(
				(Point * FMath::Cos(Step) + East * FMath::Sin(Step)).GetSafeNormal(),
				Params) - Here;
			const double RiseNorth = LedgerTerrain::Elevation(
				(Point * FMath::Cos(Step) + North * FMath::Sin(Step)).GetSafeNormal(),
				Params) - Here;

			const FVector2d Gradient(RiseEast, RiseNorth);
			const double Degrees = FMath::RadiansToDegrees(
				FMath::Atan2(Gradient.Length() / 100.0, SampleMetres));
			if (Degrees < 50.0)
			{
				continue;
			}

			// **How far it falls, not how sharply.** A gradient is scale-free
			// and the steepest thing on a planet is a kerb: the first version
			// of this search maximised the angle and found an 80.8-degree step
			// four metres above the sea -- correct, and a photograph of a
			// coastal terrace. A cliff is a face that is steep AND tall, so the
			// score is the drop over two hundred metres downhill and the angle
			// is only the gate.
			const FVector2d Fall = -Gradient.GetSafeNormal();

			// **And the face has to point at the sun, not just stand on the day
			// side.** A steep face turned away from it is in shadow at noon, and
			// the first cliff this found came back as a black silhouette against
			// a blue sky -- a correct search and, for the fourth time in one
			// session, a photograph of the dark. The surface normal is the
			// radial tilted by the gradient, which is already in hand.
			const FVector3d FaceNormal = (Point
				- East * (RiseEast / (SampleMetres * 100.0))
				- North * (RiseNorth / (SampleMetres * 100.0))).GetSafeNormal();
			if (FVector3d::DotProduct(FaceNormal, SunDirection) < 0.25)
			{
				continue;
			}
			const FVector3d Down = (East * Fall.X + North * Fall.Y).GetSafeNormal();

			const double WalkStep = (500.0) / Params.Radius;
			FVector3d Walk = Point;
			double Lowest = Here;
			for (int32 Along = 0; Along < 40; ++Along)
			{
				Walk = (Walk * FMath::Cos(WalkStep) + Down * FMath::Sin(WalkStep))
					.GetSafeNormal();
				Lowest = FMath::Min(Lowest, LedgerTerrain::Elevation(Walk, Params));
			}
			const double Fell = (Here - Lowest) / 100.0;
			if (Fell <= Steepest)
			{
				continue;
			}

			Steepest = Fell;
			SlopeDegrees = Degrees;
			Face = Point;
			AltitudeMetres = Here / 100.0;
			Downhill = Down;
		}
	}

	DropMetres = Steepest;
	return Steepest > 0.0;
}

void ULedgerCliffSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr || Controller->GetPawn() == nullptr)
	{
		return;
	}

	if (!bFound)
	{
		if (!FindFace())
		{
			bRunning = false;
			FPlatformMisc::RequestExit(false);
			return;
		}
		bFound = true;
		UE_LOG(LogLedger, Log, TEXT("cliff site: %.1f degrees, %.0f m drop, at %.0f m"),
			SlopeDegrees, DropMetres, AltitudeMetres);
	}

	if (Shot >= UE_ARRAY_COUNT(CliffShots))
	{
		FString Body;
		Body += TEXT("The steepest lit face on the planet (T054).\n\n");
		Body += FString::Printf(
			TEXT("%.1f degrees at the mesh's own %.1f m spacing, %.0f m above the sea,\n"
			     "falling %.0f m over the two hundred metres below it\n"),
			SlopeDegrees, SampleMetres, AltitudeMetres, DropMetres);
		Body += FString::Printf(TEXT("%.2f lat, %.2f lon\n\n"),
			FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Face.Z, -1.0, 1.0))),
			FMath::RadiansToDegrees(FMath::Atan2(Face.Y, Face.X)));
		Body += TEXT("cliff-face.png is the face, cliff-foot.png the ground under it,\n"
			"cliff-wide.png both in context.\n");
		Body += FString::Printf(TEXT("\na sixty-degree face exists: %s\n"),
			SlopeDegrees >= 60.0 ? TEXT("yes") : TEXT("NO"));
		Body += FString::Printf(TEXT("\nVERDICT: %s\n"),
			SlopeDegrees >= 60.0 ? TEXT("PASS") : TEXT("FAIL"));

		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("cliff.txt")));
		FFileHelper::SaveStringToFile(Body, *Path);
		UE_LOG(LogLedger, Log, TEXT("cliff site -> %s"), *Path);

		bRunning = false;
		FPlatformMisc::RequestExit(false);
		return;
	}

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
				FString(CliffShots[Shot].Name)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bCaptured = true;
		return;
	}

	++Shot;
	Settle = 0.0;
	bCaptured = false;
}

void ULedgerCliffSite::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder->GetPlanet();
	APlayerController* Controller = World->GetFirstPlayerController();

	const FCliffShot& Current = CliffShots[Shot];

	// Downhill from the face, so the camera looks back up at it with the foot
	// of the slope between.
	const double Scale = FMath::Max(60.0, DropMetres);
	const double BackArc = (Current.BackDrops * Scale * 100.0) / Planet->Radius;
	const FVector3d Eye3d =
		(Face * FMath::Cos(BackArc) + Downhill * FMath::Sin(BackArc)).GetSafeNormal();
	const double Ground = Planet->SurfaceRadiusAt(Eye3d);

	const FVector Eye = FVector(FVector3d(Planet->GetActorLocation())
		+ Eye3d * (Ground + Current.UpDrops * Scale * 100.0));
	const FVector Target = FVector(FVector3d(Planet->GetActorLocation())
		+ Face * Planet->SurfaceRadiusAt(Face));

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye + FVector(Eye3d * 400000.0));
	}

	// From the local up, not the world's: FVector::Rotation() has zero roll in
	// world terms, and on a sphere that puts the horizon down the side of the
	// frame everywhere but one longitude.
	const FRotator Look = FRotationMatrix::MakeFromXZ(
		Target - Eye, FVector(Eye3d)).Rotator();

	if (Camera == nullptr)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, Look, Params);
		if (Camera != nullptr)
		{
			if (UCameraComponent* Component = Camera->GetCameraComponent())
			{
				// Auto exposure left on, unlike the surface study.
				//
				// That study pins it because it compares the same ground under
				// two suns and auto exposure exists to cancel exactly that.
				// This one photographs a slope facing the sun head on, where a
				// pinned exposure clips it to white -- which it did, and the
				// face came back as a silhouette in the wrong direction.
				FPostProcessSettings& Post = Component->PostProcessSettings;
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
}

TStatId ULedgerCliffSite::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerCliffSite, STATGROUP_Tickables);
}
