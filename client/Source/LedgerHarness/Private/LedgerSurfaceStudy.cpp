// Photographs the ground from standing height, from a truck, and from a hill.

#include "LedgerSurfaceStudy.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "LedgerSky.h"
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
	//
	// Twenty degrees rather than eight. At eight the foreground sat in the
	// shadow of a rise a few hundred metres away -- correct, and a photograph
	// of a shadow. Twenty still rakes hard enough to throw the surface into
	// relief and clears the terrain's own shadowing.
	constexpr double StudySettleSeconds = 4.0;

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
		{ TEXT("surface-boots-low.png"),    1.7,     6.0, 20.0 },
		{ TEXT("surface-boots-high.png"),   1.7,     6.0, 62.0 },
		{ TEXT("surface-field-low.png"),   12.0,    90.0, 20.0 },
		{ TEXT("surface-field-high.png"),  12.0,    90.0, 62.0 },
		{ TEXT("surface-hill-low.png"),   140.0,  1400.0, 20.0 },
		{ TEXT("surface-hill-high.png"),  140.0,  1400.0, 62.0 },
	};

	// `-reference` (T435): the distances the photograph comparison is made at --
	// standing on the ground at 2 m, across it at 20 m, and out to 200 m -- all
	// from eye height and under one sun, so a reference photograph can be matched
	// to each by focal length, distance and sun angle alone.
	const FStudyShot ReferenceShots[] =
	{
		{ TEXT("2m.png"),   1.7,   2.0, 35.0 },
		{ TEXT("20m.png"),  1.7,  20.0, 35.0 },
		{ TEXT("200m.png"), 1.7, 200.0, 35.0 },
	};

	bool UseReferenceShots()
	{
		static const bool bReference = FParse::Param(FCommandLine::Get(), TEXT("reference"));
		return bReference;
	}
	const FStudyShot* StudyShotTable() { return UseReferenceShots() ? ReferenceShots : StudyShots; }
	int32 StudyShotCount() { return UseReferenceShots() ? UE_ARRAY_COUNT(ReferenceShots) : UE_ARRAY_COUNT(StudyShots); }
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
		StudyShotCount(), StudySettleSeconds);
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

	if (Index >= StudyShotCount())
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
	if (Settle < StudySettleSeconds)
	{
		return;
	}

	if (!bCaptured)
	{
		// The noise comparison writes alongside rather than over: the gate is a
		// side-by-side, and a comparison that overwrites one of its two halves
		// is not one.
		FString Prefix = FParse::Param(FCommandLine::Get(), TEXT("noisesurface"))
			? TEXT("noise-") : TEXT("");
		// `-studyname=` labels a reference run's files: reference-<name>-<distance>.
		FString StudyName;
		if (UseReferenceShots())
		{
			FParse::Value(FCommandLine::Get(), TEXT("studyname="), StudyName);
			Prefix += TEXT("reference-") + (StudyName.IsEmpty() ? FString(TEXT("site")) : StudyName) + TEXT("-");
		}
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				Prefix + FString(StudyShotTable()[Index].Name)));
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

	const FStudyShot& Current = StudyShotTable()[Index];

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
	FVector3d Up = (Site + Sideways * Offset).GetSafeNormal();
	// `-studyat=lat,lon` (degrees) stands the study somewhere chosen instead:
	// the sites the four-biome and cliff fixtures find, for T435's surfaces.
	FString At;
	if (FParse::Value(FCommandLine::Get(), TEXT("studyat="), At, false))
	{
		FString LatText, LonText;
		if (At.Split(TEXT(","), &LatText, &LonText))
		{
			const double Lat = FMath::DegreesToRadians(FCString::Atod(*LatText));
			const double Lon = FMath::DegreesToRadians(FCString::Atod(*LonText));
			Up = FVector3d(FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));

			// **The flattest dry ground within two kilometres.** A photograph of
			// a surface is taken standing on it, not on the side of a hill: the
			// first rainforest frame was a rock face, because the site the
			// biome fixture found was a slope steep enough to shed its soil.
			// Found once and kept, because Place runs every frame.
			static FString FoundFor;
			static FVector3d Found = FVector3d::UnitZ();
			// Not for scree, which lies on the slope under a face by definition: the
			// first search walked the cliff apron off onto flat sand. -studyasis keeps
			// the point it was given.
			static const bool bAsIs = FParse::Param(FCommandLine::Get(), TEXT("studyasis"));
			if (bAsIs)
			{
				FoundFor = At;
				Found = Up;
			}
			if (FoundFor != At)
			{
				const double Step = 1000.0 / Planet->Radius;
				auto Slope = [Planet, Step](const FVector3d& Point)
				{
					const FVector3d E = FVector3d::CrossProduct(FVector3d::UnitZ(), Point).GetSafeNormal();
					const FVector3d N = FVector3d::CrossProduct(Point, E);
					const double Dx = Planet->SurfaceRadiusAt((Point + E * Step).GetSafeNormal())
						- Planet->SurfaceRadiusAt((Point - E * Step).GetSafeNormal());
					const double Dy = Planet->SurfaceRadiusAt((Point + N * Step).GetSafeNormal())
						- Planet->SurfaceRadiusAt((Point - N * Step).GetSafeNormal());
					return FMath::Sqrt(Dx * Dx + Dy * Dy) / 2000.0;
				};
				const FVector3d E = FVector3d::CrossProduct(FVector3d::UnitZ(), Up).GetSafeNormal();
				const FVector3d N = FVector3d::CrossProduct(Up, E);
				Found = Up;
				double Flattest = Slope(Up);
				for (int32 Ring = 1; Ring <= 8; ++Ring)
				{
					for (int32 Bearing = 0; Bearing < 12; ++Bearing)
					{
						const double Angle = Bearing * UE_TWO_PI / 12.0;
						const FVector3d Point = (Up + (E * FMath::Cos(Angle) + N * FMath::Sin(Angle))
							* (Ring * 25000.0 / Planet->Radius)).GetSafeNormal();
						if (Planet->SurfaceRadiusAt(Point) <= Planet->Radius)
						{
							continue;
						}
						const double Here = Slope(Point);
						if (Here < Flattest)
						{
							Flattest = Here;
							Found = Point;
						}
					}
				}
				FoundFor = At;
				UE_LOG(LogLedger, Log, TEXT("surface study: flattest ground within 2 km at %.4f,%.4f, %.1f degrees"),
					FMath::RadiansToDegrees(FMath::Asin(Found.Z)), FMath::RadiansToDegrees(FMath::Atan2(Found.Y, Found.X)),
					FMath::RadiansToDegrees(FMath::Atan(Flattest)));
			}
			// **Daylight at the site, not at the town.** The clock is set for the
			// home site, and a study a quarter of the way round the planet was
			// photographed at dusk: every 20 m and 200 m frame of T435 had a sky near
			// black against photographs taken at midday. The next two days searched
			// ten minutes at a time for the sun closest to fifty degrees over this spot.
			static bool bClockSet = false;
			if (!bClockSet)
			{
				bClockSet = true;
				const FLedgerSystem& System = Builder->GetSystem();
				const int32 Home = Builder->GetHomeBodyIndex();
				const double Start = Builder->GetWhenSeconds();
				double Best = Start;
				double BestMiss = 1.0e9;
				for (double When = Start; When < Start + 2.0 * 86400.0; When += 600.0)
				{
					const double Miss = FMath::Abs(FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Found, When)) - 50.0);
					if (Miss < BestMiss)
					{
						BestMiss = Miss;
						Best = When;
					}
				}
				Builder->SetWhenSeconds(Best);
				UE_LOG(LogLedger, Log, TEXT("surface study: the clock set to t=%.0f s, the sun %.1f degrees over the site"),
					Best, FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Found, Best)));
			}
			Up = Found;
		}
	}
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

	// Rolled to the local vertical, not to world Z: at the equator world Z is
	// north, and a camera levelled to it lies on its side -- as the first
	// reference captures at 3 N did.
	const FRotator Look = FRotationMatrix::MakeFromXZ(Target - Eye, FVector(Up)).Rotator();

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
			// Not under -reference: a photograph is auto-exposed, and a brightness
			// pinned at 1.0 against a sun quoted in lux is a white frame -- all twelve
			// of the first reference captures were.
			if (UCameraComponent* Component = UseReferenceShots() ? nullptr : Camera->GetCameraComponent())
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

		// Elevation is varied; azimuth is kept exactly as the world chose it.
		//
		// Not a cosmetic choice. An earlier version picked its own azimuth --
		// due north of the site -- and every frame came back with the whole
		// world unlit, terrain and trees alike, while the sky rendered a
		// perfectly good midday blue. Rotating the light to the direction it
		// already had left the world lit, which ruled out the rotation itself
		// and left the direction. Keeping the world's azimuth and moving only
		// the sun's height is also the more honest study: it is the same place
		// on the same day at two times, rather than two different suns.
		const FVector3d WorldSun = Builder->GetSunFacing().GetSafeNormal();
		FVector3d Horizontal = WorldSun - Up * FVector3d::DotProduct(WorldSun, Up);
		if (Horizontal.IsNearlyZero())
		{
			Horizontal = North;
		}
		Horizontal.Normalize();

		const double Elevation = FMath::DegreesToRadians(Current.SunElevationDegrees);
		const FVector3d Direction =
			(Up * FMath::Sin(Elevation) + Horizontal * FMath::Cos(Elevation)).GetSafeNormal();
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
