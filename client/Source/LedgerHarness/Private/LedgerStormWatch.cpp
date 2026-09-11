#include "LedgerStormWatch.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerSky.h"
#include "LedgerStorm.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	enum class EStormStep : uint8 { Orbit, Inside, Flight };

	struct FStormStep
	{
		const TCHAR* What;
		EStormStep Kind;
		bool bStorm;
		double Seconds;
	};

	const FStormStep StormSteps[] =
	{
		{ TEXT("orbit-storm"), EStormStep::Orbit,  true,  25.0 },
		{ TEXT("orbit-calm"),  EStormStep::Orbit,  false, 12.0 },
		{ TEXT("inside"),      EStormStep::Inside, true,  45.0 },
		{ TEXT("fly-storm"),   EStormStep::Flight, true,  40.0 },
		{ TEXT("fly-calm"),    EStormStep::Flight, false, 40.0 },
	};

	/// Low orbit. From 1500 km the volumetric decks are past where they are
	/// drawn at all, and the first frame was bare ground.
	constexpr double StormOrbitMetres = 400000.0;
	constexpr double StormFlightMetres = 3000.0;
	constexpr double StormFlightSpeedCm = 12000.0;
	constexpr double StormMinSunDegrees = 25.0;

	FVector3d StormDirection(double Latitude, double Longitude)
	{
		const double CosLat = FMath::Cos(Latitude);
		return FVector3d(CosLat * FMath::Cos(Longitude), CosLat * FMath::Sin(Longitude),
			FMath::Sin(Latitude));
	}

	double StormLatitude(const FVector3d& Up) { return FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0)); }
	double StormLongitude(const FVector3d& Up) { return FMath::Atan2(Up.Y, Up.X); }

	FVector3d StormEast(const FVector3d& Up)
	{
		FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up);
		if (East.IsNearlyZero())
		{
			East = FVector3d::CrossProduct(FVector3d::UnitX(), Up);
		}
		return East.GetSafeNormal();
	}
}

double ULedgerStormWatch::FRun::GustRms() const
{
	if (Samples == 0)
	{
		return 0.0;
	}
	const FVector3d Mean = WindSum / Samples;
	return FMath::Sqrt(FMath::Max(WindSquares / Samples - Mean.SizeSquared(), 0.0));
}

bool ULedgerStormWatch::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerStormWatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerStormWatch, STATGROUP_Tickables);
}

void ULedgerStormWatch::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("stormwatch"));
	if (!bRunning)
	{
		return;
	}
	ULedgerWorldBuilder* Builder = InWorld.GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr || Builder->GetPlanet() == nullptr)
	{
		bRunning = false;
		return;
	}
	System = Builder->GetSystem();
	Home = Builder->GetHomeBodyIndex();
	Air = LedgerAir::For(System, Home, Builder->GetWhenSeconds());

	// **Found, not chosen**: the deepest low in ten days, hourly, whose centre
	// is under a sun high enough to photograph it by.
	const double Start = Builder->GetWhenSeconds();
	for (double When = Start; When <= Start + 10.0 * 86400.0; When += 3600.0)
	{
		for (int32 Index = 0; Index < LedgerWeather::CellCount(); ++Index)
		{
			const FLedgerPressureCell Cell = LedgerWeather::CellAt(System, Home, Index, When);
			if (!Cell.bLow || Cell.AnomalyPascals >= StormDepthPascals)
			{
				continue;
			}
			const FVector3d Up = StormDirection(Cell.LatitudeRadians, Cell.LongitudeRadians);
			if (FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Up, When))
				< StormMinSunDegrees)
			{
				continue;
			}
			StormDepthPascals = Cell.AnomalyPascals;
			StormRadiusMetres = Cell.RadiusMetres;
			StormSeconds = When;
			StormUp = Up;
		}
	}

	const double Latitude = StormLatitude(StormUp);
	const double Longitude = StormLongitude(StormUp);
	AtCentre = LedgerWeather::StormAt(System, Home, Air, Latitude, Longitude, StormSeconds);

	// Calm air at the same latitude and moment: the longitude where the cells
	// push the pressure least.
	double Quietest = TNumericLimits<double>::Max();
	for (int32 Degrees = 20; Degrees <= 340; Degrees += 5)
	{
		const double Other = Longitude + FMath::DegreesToRadians(static_cast<double>(Degrees));
		// In daylight too: the first calm frame was the night side, which is a
		// comparison of day with night and not of storm with calm.
		if (FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home,
			StormDirection(Latitude, Other), StormSeconds)) < StormMinSunDegrees)
		{
			continue;
		}
		const double Anomaly = FMath::Abs(LedgerWeather::CellAnomalyPascals(
			System, Home, Latitude, Other, StormSeconds));
		if (Anomaly < Quietest)
		{
			Quietest = Anomaly;
			CalmUp = StormDirection(Latitude, Other);
		}
	}
	AtCalm = LedgerWeather::StormAt(System, Home, Air,
		StormLatitude(CalmUp), StormLongitude(CalmUp), StormSeconds);

	Lines.Add(TEXT("A severe storm (T097)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("storm: a low %.0f Pa deep, %.0f km across, centre %.2f,%.2f at t=%.0f s (%.1f days on)"),
		-StormDepthPascals, StormRadiusMetres / 1000.0, FMath::RadiansToDegrees(Latitude),
		FMath::RadiansToDegrees(Longitude), StormSeconds, (StormSeconds - Start) / 86400.0));
	Lines.Add(FString::Printf(TEXT("at the centre: severity %.2f, %.1f flashes a minute, gusts %.1f m/s rms"),
		AtCentre.Severity, AtCentre.FlashesPerMinute, AtCentre.GustMetresPerSecond));
	Lines.Add(FString::Printf(TEXT("calm air at %.2f,%.2f: severity %.2f, anomaly %.0f Pa"),
		FMath::RadiansToDegrees(StormLatitude(CalmUp)), FMath::RadiansToDegrees(StormLongitude(CalmUp)),
		AtCalm.Severity, Quietest));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("storm watch: %s"), *Line);
	}
}

void ULedgerStormWatch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;
	const ULedgerStorm* Storms = World->GetSubsystem<ULedgerStorm>();
	if (Planet == nullptr || Controller == nullptr || Ship == nullptr || Storms == nullptr)
	{
		return;
	}

	const FStormStep& Now = StormSteps[Step];
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const FVector3d Site = Now.bStorm ? StormUp : CalmUp;
	Builder->SetWhenSeconds(StormSeconds);

	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		Controller->SetViewTarget(Camera);
	}

	if (!bStarted)
	{
		bStarted = true;
		Clock = 0.0;
		if (Now.Kind == EStormStep::Flight)
		{
			Heading = StormEast(Site);
			Ship->RepairHull();
			Ship->SetActorHiddenInGame(false);
			Ship->SetActorLocation(FVector(Centre + Site * (Planet->Radius + StormFlightMetres * 100.0)));
			Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(Heading), FVector(Site)).Rotator());
			Ship->SetVelocity(FVector(Heading * StormFlightSpeedCm));
			Ship->SetFlightEnabled(true);
		}
		else
		{
			Ship->SetFlightEnabled(false);
			Ship->SetActorHiddenInGame(true);
			Ship->SetActorLocation(FVector(Centre + Site * (Planet->Radius + 4.0e7)));
		}
		if (Now.Kind == EStormStep::Inside)
		{
			FlashesInside = Storms->FlashCount();
			ThundersInside = Storms->ThunderCount();
		}
	}
	Clock += DeltaSeconds;

	FVector Eye;
	FRotator Look;
	if (Now.Kind == EStormStep::Orbit)
	{
		Eye = FVector(Centre + Site * (Planet->Radius + StormOrbitMetres * 100.0));
		Look = (FVector(Centre) - Eye).Rotation();
		Camera->GetCameraComponent()->SetFieldOfView(60.0f);
	}
	else if (Now.Kind == EStormStep::Inside)
	{
		Eye = FVector(Centre + Site * (Planet->Radius + StormFlightMetres * 100.0));
		Look = FRotationMatrix::MakeFromXZ(
			FVector(StormEast(Site) + Site * 0.1), FVector(Site)).Rotator();
		Camera->GetCameraComponent()->SetFieldOfView(80.0f);
	}
	else
	{
		// Hold the height and the speed along the track, and leave the air the
		// rest: whatever it does across the track is the wind's doing.
		const FVector3d Offset = FVector3d(Ship->GetActorLocation()) - Centre;
		const FVector3d Up = Offset.GetSafeNormal();
		const FVector3d Along = (Heading - Up * FVector3d::DotProduct(Heading, Up)).GetSafeNormal();
		const FVector3d Across = FVector3d::CrossProduct(Up, Along);
		const double CrossSpeed = FVector3d::DotProduct(FVector3d(Ship->GetVelocity()), Across);
		Ship->SetVelocity(FVector(Along * StormFlightSpeedCm + Across * CrossSpeed));
		Ship->SetActorLocation(FVector(Centre + Up * (Planet->Radius + StormFlightMetres * 100.0)));

		FRun& Run = Runs[Now.bStorm ? 0 : 1];
		const FVector3d Wind = Ship->LastWindCmPerSecond() / 100.0;
		Run.WindSum += Wind;
		Run.WindSquares += Wind.SizeSquared();
		++Run.Samples;
		Run.CrossTrackMax = FMath::Max(Run.CrossTrackMax, FMath::Abs(CrossSpeed) / 100.0);
		Run.Hull = Ship->HullIntegrity();
		Run.Strikes = Ship->StrikesTaken();

		Eye = FVector(FVector3d(Ship->GetActorLocation()) - Along * 3000.0 + Up * 800.0);
		Look = FRotationMatrix::MakeFromXZ(FVector(Along), FVector(Up)).Rotator();
		Camera->GetCameraComponent()->SetFieldOfView(80.0f);
	}
	Camera->SetActorLocationAndRotation(Eye, Look);

	if (Clock < Now.Seconds)
	{
		return;
	}

	FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			FString::Printf(TEXT("storm-%d-%s.png"), Step, Now.What))), false, false);
	if (Now.Kind == EStormStep::Inside)
	{
		FlashesInside = Storms->FlashCount() - FlashesInside;
		ThundersInside = Storms->ThunderCount() - ThundersInside;
	}
	if (Now.Kind == EStormStep::Flight)
	{
		Ship->SetFlightEnabled(false);
		const FRun& Run = Runs[Now.bStorm ? 0 : 1];
		UE_LOG(LogLedger, Log,
			TEXT("storm watch: %s: hull %.2f, %d strikes, gusts %.1f m/s rms, cross-track up to %.1f m/s"),
			Now.What, Run.Hull, Run.Strikes, Run.GustRms(), Run.CrossTrackMax);
	}

	++Step;
	bStarted = false;
	if (Step >= UE_ARRAY_COUNT(StormSteps))
	{
		bRunning = false;
		Finish();
	}
}

void ULedgerStormWatch::Finish()
{
	const UWorld* World = GetWorld();
	const ULedgerStorm* Storms = World != nullptr ? World->GetSubsystem<ULedgerStorm>() : nullptr;

	// Which published low, if any, is this storm. The clouds are drawn from
	// that list, so a match is the orbit view and the flight agreeing on where
	// the storm is by construction rather than by coincidence.
	int32 Match = INDEX_NONE;
	double Nearest = TNumericLimits<double>::Max();
	if (Storms != nullptr)
	{
		const TArray<FVector4d>& Lows = Storms->PublishedLows();
		for (int32 Index = 0; Index < Lows.Num(); ++Index)
		{
			const double Degrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector3d::DotProduct(FVector3d(Lows[Index].X, Lows[Index].Y, Lows[Index].Z), StormUp),
				-1.0, 1.0)));
			if (Degrees < Nearest)
			{
				Nearest = Degrees;
				Match = Index;
			}
		}
	}

	const bool bDangerous = Runs[0].Hull < 0.95 && Runs[1].Hull >= 1.0;
	const bool bDrawn = Match != INDEX_NONE && Nearest < 1.0;
	Lines.Add(FString::Printf(TEXT("the clouds draw it: published low %d, %.3f degrees from the centre"),
		Match, Nearest));
	Lines.Add(FString::Printf(TEXT("inside the cloud for %.0f s: %d flashes drawn, %d thunderclaps heard"),
		StormSteps[2].Seconds, FlashesInside, ThundersInside));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FRun& Run = Runs[Index];
		Lines.Add(FString::Printf(
			TEXT("%s: %.0f s at %.0f m and %.0f m/s, hull %.2f, %d strikes, gusts %.1f m/s rms, cross-track up to %.1f m/s"),
			Index == 0 ? TEXT("through the storm") : TEXT("through calm air "),
			StormSteps[3 + Index].Seconds, StormFlightMetres, StormFlightSpeedCm / 100.0,
			Run.Hull, Run.Strikes, Run.GustRms(), Run.CrossTrackMax));
	}
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("dangerous to fly through, and calm air is not: %s"),
		bDangerous ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("drawn from orbit in the same place: %s"), bDrawn ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bDangerous && bDrawn ? TEXT("PASS") : TEXT("FAIL")));
	for (int32 Index = 5; Index < Lines.Num(); ++Index)
	{
		UE_LOG(LogLedger, Log, TEXT("storm watch: %s"), *Lines[Index]);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("storm.txt"))));
	FPlatformMisc::RequestExit(false);
}
