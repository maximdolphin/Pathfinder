#include "LedgerStormFront.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerAudio.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerPrecipitationView.h"
#include "LedgerShip.h"
#include "LedgerSky.h"
#include "LedgerStorm.h"
#include "LedgerWeather.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr double FrontOutsideMetres = 15000.0;
	constexpr double FrontFlightMetres = 60000.0;
	constexpr double FrontSpeedMetresPerSecond = 200.0;
	constexpr double FrontAboveGroundMetres = 700.0;
	constexpr double FrontSettleSeconds = 20.0;
	constexpr double FrontOrbitMetres = 300000.0;

	/// How far you can see through rain, metres: Koschmieder's 3.9 over the
	/// extinction, with the extinction of rain going as its rate to the 0.63
	/// (about a quarter per kilometre at a millimetre an hour). Clear air is
	/// taken as fifty kilometres.
	double FrontVisibilityMetres(double RainMillimetresPerHour)
	{
		if (!(RainMillimetresPerHour > 0.0))
		{
			return 50000.0;
		}
		const double PerKilometre = 0.25 * FMath::Pow(RainMillimetresPerHour, 0.63);
		return FMath::Min(3.9 / PerKilometre * 1000.0, 50000.0);
	}

	FVector3d FrontAlong(const FVector3d& Up, const FVector3d& Direction, double Metres, double RadiusMetres)
	{
		const FVector3d Tangent = (Direction - Up * FVector3d::DotProduct(Direction, Up)).GetSafeNormal();
		const double Angle = Metres / RadiusMetres;
		return (Up * FMath::Cos(Angle) + Tangent * FMath::Sin(Angle)).GetSafeNormal();
	}
}

bool ULedgerStormFront::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerStormFront::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerStormFront, STATGROUP_Tickables);
}

void ULedgerStormFront::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("stormfront"));
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
	const double Radius = System.Bodies[Home].RadiusMetres;

	// The deepest daylit storm in ten days.
	double Deepest = 0.0;
	const double Start = Builder->GetWhenSeconds();
	for (double When = Start; When <= Start + 10.0 * 86400.0; When += 3600.0)
	{
		for (int32 Index = 0; Index < LedgerWeather::CellCount(); ++Index)
		{
			const FLedgerPressureCell Cell = LedgerWeather::CellAt(System, Home, Index, When);
			const FVector3d Up(FMath::Cos(Cell.LatitudeRadians) * FMath::Cos(Cell.LongitudeRadians),
				FMath::Cos(Cell.LatitudeRadians) * FMath::Sin(Cell.LongitudeRadians), FMath::Sin(Cell.LatitudeRadians));
			if (Cell.bLow && Cell.AnomalyPascals < Deepest
				&& FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Up, When)) > 30.0)
			{
				Deepest = Cell.AnomalyPascals;
				StormSeconds = When;
				StormUp = Up;
			}
		}
	}

	// Its severe edge, due east of the centre, found by bisecting the model.
	auto Severity = [&](double Metres)
	{
		const FVector3d At = FrontAlong(StormUp, FVector3d::UnitZ() ^ StormUp, Metres, Radius);
		return LedgerWeather::StormAt(System, Home, Air, FMath::Asin(FMath::Clamp(At.Z, -1.0, 1.0)),
			FMath::Atan2(At.Y, At.X), StormSeconds).Severity;
	};
	double Inside = 0.0;
	double Outside = 3000000.0;
	for (int32 Step = 0; Step < 50; ++Step)
	{
		const double Middle = (Inside + Outside) * 0.5;
		(Severity(Middle) > 0.0 ? Inside : Outside) = Middle;
	}
	const FVector3d East = (FVector3d::UnitZ() ^ StormUp).GetSafeNormal();
	EdgeUp = FrontAlong(StormUp, East, Inside, Radius);
	StartUp = FrontAlong(StormUp, East, Inside + FrontOutsideMetres, Radius);
	Inward = (StormUp - StartUp * FVector3d::DotProduct(StormUp, StartUp)).GetSafeNormal();

	Lines.Add(TEXT("Playable proof: fly into a storm front (T107)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("storm: a low %.0f Pa deep at %.2f,%.2f, t=%.0f s; its severe edge %.0f km from the centre"),
		-Deepest, FMath::RadiansToDegrees(FMath::Asin(StormUp.Z)), FMath::RadiansToDegrees(FMath::Atan2(StormUp.Y, StormUp.X)),
		StormSeconds, Inside / 1000.0));
	Lines.Add(FString::Printf(TEXT("flight: from %.0f km outside the edge, %.0f km in, %.0f m above the ground at %.0f m/s"),
		FrontOutsideMetres / 1000.0, FrontFlightMetres / 1000.0, FrontAboveGroundMetres, FrontSpeedMetresPerSecond));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("storm front: %s"), *Line);
	}
}

void ULedgerStormFront::Tick(float DeltaSeconds)
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
	const ULedgerAudio* Audio = World->GetSubsystem<ULedgerAudio>();
	const ULedgerPrecipitationView* Rain = World->GetSubsystem<ULedgerPrecipitationView>();
	if (Planet == nullptr || Ship == nullptr || Storms == nullptr)
	{
		return;
	}
	Builder->SetWhenSeconds(StormSeconds);
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	Clock += DeltaSeconds;

	// ---- stage 0: held at the start, through the ship's own camera, while the
	// world streams in
	if (Stage == 0)
	{
		Controller->SetViewTarget(Ship);
		Ship->SetActorHiddenInGame(false);
		Ship->SetFlightEnabled(false);
		Ship->RepairHull();
		const FVector3d Heading = Inward;
		Ship->SetActorLocation(FVector(Centre + StartUp * (Planet->SurfaceRadiusAt(StartUp) + FrontAboveGroundMetres * 100.0)));
		Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(Heading), FVector(StartUp)).Rotator());
		Ship->SetVelocity(FVector(Heading * FrontSpeedMetresPerSecond * 100.0));
		if (Clock < FrontSettleSeconds)
		{
			return;
		}
		Ship->SetFlightEnabled(true);
		Stage = 1;
		Clock = 0.0;
		return;
	}

	// ---- stage 1: the flight in
	if (Stage == 1)
	{
		const FVector3d Position = FVector3d(Ship->GetActorLocation()) - Centre;
		const FVector3d Up = Position.GetSafeNormal();
		const FVector3d Along = (Inward - Up * FVector3d::DotProduct(Inward, Up)).GetSafeNormal();
		const FVector3d Across = FVector3d::CrossProduct(Up, Along);
		const FVector3d Velocity = FVector3d(Ship->GetVelocity()) / 100.0;
		// The throttle and the height held; across the track is the air's.
		Ship->SetVelocity(FVector((Along * FrontSpeedMetresPerSecond + Across * FVector3d::DotProduct(Velocity, Across)) * 100.0));
		Ship->SetActorLocation(FVector(Centre + Up * (Planet->SurfaceRadiusAt(Up) + FrontAboveGroundMetres * 100.0)));
		Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(Along), FVector(Up)).Rotator());

		const double Travelled = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Up, StartUp), -1.0, 1.0))
			* Planet->Radius / 100.0;
		SinceSample += DeltaSeconds;
		SinceFrame += DeltaSeconds;
		if (SinceSample >= 0.5)
		{
			SinceSample = 0.0;
			const FVector3d Wind = Ship->LastWindCmPerSecond() / 100.0;
			WindMean = Track.Num() == 0 ? Wind : WindMean * 0.9 + Wind * 0.1;
			FSample Sample;
			Sample.Seconds = Clock;
			Sample.FromEdgeMetres = Travelled - FrontOutsideMetres;
			Sample.Severity = Storms->StormAt(Ship->GetActorLocation()).Severity;
			Sample.VisibilityMetres = FrontVisibilityMetres(Rain != nullptr ? Rain->Falling().RateMillimetresPerHour : 0.0);
			Sample.GustMetresPerSecond = (Wind - WindMean).Length();
			Sample.Loudness = Audio != nullptr ? Audio->Heard().Gain : 0.0;
			Sample.Thunder = Storms->ThunderCount();
			Track.Add(Sample);
		}
		if (SinceFrame >= 10.0)
		{
			SinceFrame = 0.0;
			FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
					FString::Printf(TEXT("stormfront-%02d.png"), Frame++))), false, false);
		}
		if (Travelled < FrontFlightMetres)
		{
			return;
		}
		Ship->SetFlightEnabled(false);
		Stage = 2;
		Clock = 0.0;
		return;
	}

	// ---- stage 2: the same place from orbit
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		Camera->GetCameraComponent()->SetFieldOfView(70.0f);
		Controller->SetViewTarget(Camera);
		Ship->SetActorHiddenInGame(true);
	}
	const FVector Eye = FVector(Centre + EdgeUp * (Planet->Radius + FrontOrbitMetres * 100.0));
	Camera->SetActorLocationAndRotation(Eye, (FVector(Centre) - Eye).Rotation());
	if (Clock < 15.0)
	{
		return;
	}
	FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("stormfront-orbit.png"))), false, false);
	bRunning = false;
	Finish();
}

void ULedgerStormFront::Finish()
{
	// Entry: the first sample with any severity. Before and after: the samples
	// more than five seconds either side of it.
	int32 Entry = INDEX_NONE;
	for (int32 Index = 0; Index < Track.Num() && Entry == INDEX_NONE; ++Index)
	{
		Entry = Track[Index].Severity > 0.0 ? Index : INDEX_NONE;
	}
	auto Mean = [this, Entry](bool bAfter, TFunctionRef<double(const FSample&)> Of)
	{
		double Sum = 0.0;
		int32 Count = 0;
		for (int32 Index = 0; Index < Track.Num(); ++Index)
		{
			const double Offset = Track[Index].Seconds - Track[Entry].Seconds;
			if ((bAfter && Offset > 5.0) || (!bAfter && Offset < -5.0))
			{
				Sum += Of(Track[Index]);
				++Count;
			}
		}
		return Count > 0 ? Sum / Count : 0.0;
	};
	// When each quantity crosses halfway between its before and its after.
	auto Onset = [this](double Before, double After, TFunctionRef<double(const FSample&)> Of)
	{
		const double Half = (Before + After) * 0.5;
		for (const FSample& Sample : Track)
		{
			if ((After > Before) == (Of(Sample) > Half))
			{
				return Sample.Seconds;
			}
		}
		return -1.0;
	};

	bool bTogether = false;
	bool bChanged = false;
	if (Entry != INDEX_NONE)
	{
		auto Visibility = [](const FSample& S) { return S.VisibilityMetres; };
		auto Gust = [](const FSample& S) { return S.GustMetresPerSecond; };
		auto Loud = [](const FSample& S) { return S.Loudness + 0.05 * S.Thunder; };
		const double V0 = Mean(false, Visibility), V1 = Mean(true, Visibility);
		const double G0 = Mean(false, Gust), G1 = Mean(true, Gust);
		const double L0 = Mean(false, Loud), L1 = Mean(true, Loud);
		const double At = Track[Entry].Seconds;
		const double Tv = Onset(V0, V1, Visibility), Tg = Onset(G0, G1, Gust), Tl = Onset(L0, L1, Loud);
		bChanged = V1 < 0.7 * V0 && G1 > 2.0 * FMath::Max(G0, 0.1) && L1 > L0;
		bTogether = FMath::Abs(Tv - At) < 15.0 && FMath::Abs(Tg - At) < 15.0 && FMath::Abs(Tl - At) < 20.0;
		Lines.Add(FString::Printf(TEXT("entry at %.1f s, %.0f m past the start"), At, Track[Entry].FromEdgeMetres + FrontOutsideMetres));
		Lines.Add(FString::Printf(TEXT("visibility %.1f km before, %.1f km after; changed at %.1f s"), V0 / 1000.0, V1 / 1000.0, Tv));
		Lines.Add(FString::Printf(TEXT("gusts %.1f m/s before, %.1f m/s after; changed at %.1f s"), G0, G1, Tg));
		Lines.Add(FString::Printf(TEXT("loudness %.2f before, %.2f after (wind and thunder); changed at %.1f s"), L0, L1, Tl));
	}

	// The same storm from orbit: the published low the clouds are drawn from.
	const UWorld* World = GetWorld();
	const ULedgerStorm* Storms = World != nullptr ? World->GetSubsystem<ULedgerStorm>() : nullptr;
	double Nearest = 180.0;
	if (Storms != nullptr)
	{
		for (const FVector4d& Low : Storms->PublishedLows())
		{
			Nearest = FMath::Min(Nearest, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector3d::DotProduct(FVector3d(Low.X, Low.Y, Low.Z), StormUp), -1.0, 1.0))));
		}
	}
	const bool bSame = Nearest < 1.0;
	Lines.Add(FString::Printf(TEXT("from orbit: the clouds are drawn over a low %.3f degrees from the storm flown into"), Nearest));

	FString Table = TEXT("\n  s      edge m   severity  vis km  gust m/s  loud  thunder\n");
	for (int32 Index = 0; Index < Track.Num(); Index += 4)
	{
		const FSample& S = Track[Index];
		Table += FString::Printf(TEXT("%5.1f  %8.0f   %6.2f   %6.1f   %6.1f   %5.2f  %d\n"),
			S.Seconds, S.FromEdgeMetres, S.Severity, S.VisibilityMetres / 1000.0, S.GustMetresPerSecond, S.Loudness, S.Thunder);
	}
	Lines.Add(Table);
	Lines.Add(FString::Printf(TEXT("visibility, wind loading and audio all change on entry: %s"), bChanged ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("and together, within seconds of it: %s"), bTogether ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("the same storm is in the same place from orbit: %s"), bSame ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bChanged && bTogether && bSame ? TEXT("PASS") : TEXT("FAIL")));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("storm front: %s"), *Line);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("stormfront.txt"))));
	FPlatformMisc::RequestExit(false);
}
