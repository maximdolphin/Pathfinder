#include "LedgerFrontWatch.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerClimate.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerPrecipitationView.h"
#include "LedgerShip.h"
#include "LedgerSky.h"
#include "LedgerWeather.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr double FrontFirstSettle = 25.0;
	constexpr double FrontStepSettle = 12.0;

	constexpr double FrontCentimetresPerMetre = 100.0;
	constexpr double FrontEyeMetres = 25.0;

	/// How far up the snow frame sits above the freezing level. Far enough that
	/// nothing near the boundary is in frame, close enough that the ground is
	/// still under it.
	constexpr double FrontSnowMarginMetres = 900.0;

	struct FFrontStep
	{
		const TCHAR* What;
		double HoursFromPeak;
		/// Zero stands on the ground; otherwise metres above the freezing level.
		bool bAboveFreezing;
	};

	const FFrontStep FrontSteps[] =
	{
		{ TEXT("before"),  -18.0, false },
		{ TEXT("arriving"), -5.0, false },
		{ TEXT("rain"),      0.0, false },
		// The same instant, higher up. **The whole acceptance is the difference
		// between these two frames**, and taking them at the same moment is what
		// makes it a difference in altitude rather than in weather.
		{ TEXT("snow"),      0.0, true  },
		{ TEXT("after"),    18.0, false },
	};
}

bool ULedgerFrontWatch::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerFrontWatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerFrontWatch, STATGROUP_Tickables);
}

void ULedgerFrontWatch::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("front"));
	if (!bRunning)
	{
		return;
	}

	ULedgerWorldBuilder* Builder = InWorld.GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Builder == nullptr || Planet == nullptr)
	{
		bRunning = false;
		return;
	}

	System = Builder->GetSystem();
	Anchor = Builder->GetSiteDirection().GetSafeNormal();
	Home = Builder->GetHomeBodyIndex();
	Air = LedgerAir::For(System, Home, Builder->GetWhenSeconds());
	Latitude = FMath::Asin(FMath::Clamp(Anchor.Z, -1.0, 1.0));
	Longitude = FMath::Atan2(Anchor.Y, Anchor.X);

	const FLedgerClimate Climate = LedgerClimate::At(
		Anchor, Planet->TerrainParams(), Planet->SeasonPhase());
	SurfaceKelvin = Climate.TemperatureC + 273.15;
	DeclaredFreezingLevel = LedgerPrecip::FreezingLevelMetres(Air, SurfaceKelvin);

	// **Find the front rather than choose it.** Twenty days at half-hour steps:
	// a cell lives a few days and crosses in about one, so this cannot miss a
	// passage, and the deepest sample is the middle of the worst of it.
	const double Start = Builder->GetWhenSeconds();
	constexpr double Span = 20.0 * 86400.0;
	constexpr double Stride = 1800.0;
	PeakSeconds = Start;
	PeakAnomaly = 0.0;
	for (double At = Start; At <= Start + Span; At += Stride)
	{
		const double Anomaly = LedgerWeather::CellAnomalyPascals(
			System, Home, Latitude, Longitude, At);
		if (Anomaly < PeakAnomaly)
		{
			PeakAnomaly = Anomaly;
			PeakSeconds = At;
		}
	}

	UE_LOG(LogLedger, Log,
		TEXT("front: body %d (%s), site %.1f N %.1f E at %.1f C; deepest "
			 "anomaly %.0f Pa at t=%.0f s (%.1f days on), freezing level %.0f m"),
		Home, *System.Bodies[Home].Name,
		FMath::RadiansToDegrees(Latitude), FMath::RadiansToDegrees(Longitude),
		SurfaceKelvin - 273.15, PeakAnomaly, PeakSeconds,
		(PeakSeconds - Start) / 86400.0, DeclaredFreezingLevel);
}

void ULedgerFrontWatch::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	const int32 Which = FMath::Clamp(
		bAimed ? Step - 1 : Step, 0, UE_ARRAY_COUNT(FrontSteps) - 1);
	const FFrontStep& Now = FrontSteps[Which];

	Builder->SetWhenSeconds(PeakSeconds + Now.HoursFromPeak * 3600.0);

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Anchor);
	const double Height = Now.bAboveFreezing
		? FMath::Max(DeclaredFreezingLevel + FrontSnowMarginMetres, FrontEyeMetres)
		: FrontEyeMetres;
	const FVector Eye = FVector(
		Centre + Anchor * (Ground + Height * FrontCentimetresPerMetre));

	// Away from the sun and level, so the drops are lit from behind the camera
	// and the frame is landscape rather than sky.
	const FVector3d Sun = LedgerSky::SunDirectionInSurface(
		System, Home, Anchor, Builder->GetWhenSeconds());
	const FVector2D Away = FVector2D(-Sun.X, -Sun.Y).GetSafeNormal();
	const FVector3d Toward = LedgerFrames::ToBody(
		{ Home, Anchor, FVector3d(Away.X, Away.Y, -0.08) }).Metres.GetSafeNormal();

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye);
	}

	const FRotator Look =
		FRotationMatrix::MakeFromXZ(FVector(Toward), FVector(Anchor)).Rotator();
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Eye, Look, SpawnParams);
		if (Camera != nullptr)
		{
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->GetCameraComponent()->SetFieldOfView(70.0f);
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

void ULedgerFrontWatch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}
	if (GetWorld()->GetSubsystem<ULedgerWorldBuilder>() == nullptr)
	{
		return;
	}

	Place();

	Settle += DeltaSeconds;
	if (Settle < (Step == 0 && !bAimed ? FrontFirstSettle : FrontStepSettle))
	{
		return;
	}

	if (bAimed)
	{
		const int32 Which = Step - 1;

		// **Where the light is, measured at the moment the picture is taken.**
		// A town that came back as black silhouettes under a white sky was
		// argued about as materials, normals, distance fields and mobility in
		// turn; this says what the frame was actually lit by. The camera is
		// meant to face away from the sun, so the angle between its view and
		// the sun should be near 180, and each directional light should point
		// the way the ephemeris does.
		if (ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>())
		{
			const double When = Builder->GetWhenSeconds();
			const FVector3d SunSurface =
				LedgerSky::SunDirectionInSurface(System, Home, Anchor, When);
			const FVector3d SunWorld =
				LedgerFrames::ToBody({ Home, Anchor, SunSurface }).Metres.GetSafeNormal();
			const FVector3d Forward = Camera != nullptr
				? FVector3d(Camera->GetActorForwardVector()) : FVector3d::ZeroVector;
			UE_LOG(LogLedger, Log,
				TEXT("front light: sun %.1f deg above the horizon, %.0f deg from "
					 "the camera's view (180 is behind it)"),
				FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Anchor, When)),
				FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
					FVector3d::DotProduct(Forward, SunWorld), -1.0, 1.0))));
			for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
			{
				const UDirectionalLightComponent* Light =
					Cast<UDirectionalLightComponent>(It->GetLightComponent());
				// A directional light shines along its forward vector, so the
				// direction towards its source is the negative of that.
				const FVector3d Towards = -FVector3d(It->GetActorForwardVector());
				UE_LOG(LogLedger, Log,
					TEXT("front light: %s points %.1f deg from the ephemeris sun, "
						 "intensity %.0f, visible %d, affects world %d"),
					*It->GetActorLabel(),
					FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
						FVector3d::DotProduct(Towards, SunWorld), -1.0, 1.0))),
					Light != nullptr ? Light->Intensity : -1.0f,
					Light != nullptr ? static_cast<int32>(Light->IsVisible()) : -1,
					Light != nullptr ? static_cast<int32>(Light->bAffectsWorld) : -1);
			}
		}

		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("front-%d-%s.png"),
					Which, FrontSteps[Which].What)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);

		// Recorded from the running subsystem, so the number in the table is
		// the number the picture was taken of.
		const ULedgerPrecipitationView* View =
			GetWorld()->GetSubsystem<ULedgerPrecipitationView>();
		const FLedgerPrecipitation Falling =
			View != nullptr ? View->Falling() : FLedgerPrecipitation();

		FMoment Moment;
		Moment.What = FrontSteps[Which].What;
		Moment.HoursFromPeak = FrontSteps[Which].HoursFromPeak;
		Moment.AnomalyPascals = LedgerWeather::CellAnomalyPascals(
			System, Home, Latitude, Longitude,
			PeakSeconds + Moment.HoursFromPeak * 3600.0);
		Moment.RateMillimetresPerHour = Falling.RateMillimetresPerHour;
		Moment.Kind = Falling.Kind;
		Moment.Drops = View != nullptr ? View->DrawnCount() : 0;
		Moments.Add(Moment);

		UE_LOG(LogLedger, Log,
			TEXT("front %d/%d: %s, %+.0f h, anomaly %.0f Pa, %s at %.2f mm/h, "
				 "%d drops"),
			Step, static_cast<int32>(UE_ARRAY_COUNT(FrontSteps)), Moment.What,
			Moment.HoursFromPeak, Moment.AnomalyPascals,
			LexToString(Moment.Kind), Moment.RateMillimetresPerHour,
			Moment.Drops);

		bAimed = false;
		Settle = 0.0;
		return;
	}

	if (Step >= UE_ARRAY_COUNT(FrontSteps))
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	++Step;
	bAimed = true;
	Settle = 0.0;
}

void ULedgerFrontWatch::Report()
{
	// The vertical profile, at the peak, through the model itself.
	const double Top = FMath::Max(Air.CloudTopMetres, 6000.0);
	for (int32 Index = 0; Index <= 12; ++Index)
	{
		const double Altitude = Top * Index / 12.0;
		const FLedgerPrecipitation At = LedgerPrecip::At(
			System, Home, Air, Latitude, Longitude, Altitude, PeakSeconds,
			SurfaceKelvin);
		Profile.Add({ Altitude, At.Kind });
	}

	// **Find the changeover, do not read it.** Bisecting what the model answers
	// is the only version of this that can fail: comparing the freezing level
	// against itself would agree for ever.
	double Low = 0.0;
	double High = Top;
	const bool bRainAtTheGround =
		LedgerPrecip::FrozenOrNot(Air, 0.0, SurfaceKelvin)
		== ELedgerPrecipitation::Rain;
	for (int32 Iteration = 0; Iteration < 80 && bRainAtTheGround; ++Iteration)
	{
		const double Middle = (Low + High) * 0.5;
		(LedgerPrecip::FrozenOrNot(Air, Middle, SurfaceKelvin)
			== ELedgerPrecipitation::Rain ? Low : High) = Middle;
	}
	BisectedBoundary = bRainAtTheGround ? (Low + High) * 0.5 : 0.0;

	TArray<FString> Lines;
	Lines.Add(TEXT("A low crosses a site (T095)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("body            %d (%s)"),
		Home, System.Bodies.IsValidIndex(Home) ? *System.Bodies[Home].Name : TEXT("?")));
	Lines.Add(FString::Printf(TEXT("site            %.1f N, %.1f E, %.1f C"),
		FMath::RadiansToDegrees(Latitude), FMath::RadiansToDegrees(Longitude),
		SurfaceKelvin - 273.15));
	Lines.Add(FString::Printf(TEXT("deepest anomaly %.0f Pa"), PeakAnomaly));
	Lines.Add(FString::Printf(TEXT("cloud           %.0f to %.0f m"),
		Air.CloudBaseMetres, Air.CloudTopMetres));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("  moment        hours   anomaly Pa   falling      mm/h   drops"));
	for (const FMoment& Moment : Moments)
	{
		Lines.Add(FString::Printf(TEXT("  %-12s %+6.0f %12.0f   %-10s %5.2f %7d"),
			Moment.What, Moment.HoursFromPeak, Moment.AnomalyPascals,
			LexToString(Moment.Kind), Moment.RateMillimetresPerHour,
			Moment.Drops));
	}

	Lines.Add(TEXT(""));
	Lines.Add(TEXT("  altitude m   falling"));
	for (const TPair<double, ELedgerPrecipitation>& Rung : Profile)
	{
		Lines.Add(FString::Printf(TEXT("%12.0f   %s"),
			Rung.Key, LexToString(Rung.Value)));
	}

	const double Apart = FMath::Abs(BisectedBoundary - DeclaredFreezingLevel);
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(
		TEXT("freezing level declared %.3f m, changeover found by bisection "
			 "%.3f m, %.2e apart"),
		DeclaredFreezingLevel, BisectedBoundary, Apart));
	Lines.Add(FString::Printf(TEXT("the boundary is where the lapse rate puts it: %s"),
		bRainAtTheGround && Apart < 0.001 ? TEXT("yes") : TEXT("NO")));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			TEXT("front.txt")));
	FFileHelper::SaveStringArrayToFile(Lines, *Path);

	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("%s"), *Line);
	}
}
