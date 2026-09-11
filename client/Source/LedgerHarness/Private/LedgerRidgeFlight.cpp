#include "LedgerRidgeFlight.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "LedgerAir.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerWeather.h"
#include "LedgerWind.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	struct FRidgeRun
	{
		const TCHAR* What;
		bool bWind;
		bool bFlown;
	};

	const FRidgeRun RidgeRuns[] =
	{
		{ TEXT("calm, flown"),     false, true  },
		{ TEXT("gale, hands off"), true,  false },
		{ TEXT("gale, flown"),     true,  true  },
	};

	/// Either side of the site along the wind, metres.
	constexpr double RidgeHalfTrackMetres = 12000.0;
	/// Above the highest ground on the track, metres.
	constexpr double RidgeClearanceMetres = 250.0;
	constexpr double RidgeSpeedMetresPerSecond = 60.0;
	constexpr double RidgeSettleSeconds = 15.0;
	constexpr double RidgeMaxSeconds = 480.0;

	/// Autopilot gains, per second squared and per second: firm enough to hold
	/// a few metres, soft enough not to ring.
	constexpr double RidgeHeightGain = 0.4;
	constexpr double RidgeDampingGain = 1.6;

	FVector3d RidgeAlong(const FVector3d& Up, const FVector3d& Direction, double Metres, double RadiusCm)
	{
		return (Up + Direction * (Metres * 100.0 / RadiusCm)).GetSafeNormal();
	}
}

double ULedgerRidgeFlight::FRun::Correlation() const
{
	if (Samples < 2)
	{
		return 0.0;
	}
	const double N = Samples;
	const double Cov = SumWV / N - (SumW / N) * (SumV / N);
	const double VarW = SumWW / N - FMath::Square(SumW / N);
	const double VarV = SumVV / N - FMath::Square(SumV / N);
	return VarW > 0.0 && VarV > 0.0 ? Cov / FMath::Sqrt(VarW * VarV) : 0.0;
}

bool ULedgerRidgeFlight::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerRidgeFlight::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerRidgeFlight, STATGROUP_Tickables);
}

void ULedgerRidgeFlight::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("ridgeflight"));
	if (!bRunning)
	{
		return;
	}
	ULedgerWorldBuilder* Builder = InWorld.GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Planet == nullptr)
	{
		bRunning = false;
		return;
	}
	System = Builder->GetSystem();
	Home = Builder->GetHomeBodyIndex();
	const double Start = Builder->GetWhenSeconds();
	const FLedgerAirProfile Air = LedgerAir::For(System, Home, Start);
	const double Radius = Planet->Radius;

	// **Rugged ground under a gale**, on a three-degree grid: relief across ten
	// kilometres, times the strongest wind at 1.5 km over the next five days.
	double BestScore = 0.0;
	for (int32 LatDegrees = -60; LatDegrees <= 60; LatDegrees += 3)
	{
		for (int32 LonDegrees = -180; LonDegrees < 180; LonDegrees += 3)
		{
			const double Lat = FMath::DegreesToRadians(static_cast<double>(LatDegrees));
			const double Lon = FMath::DegreesToRadians(static_cast<double>(LonDegrees));
			const FVector3d Up(FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));
			FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up).GetSafeNormal();
			const FVector3d North = FVector3d::CrossProduct(Up, East);
			double Low = TNumericLimits<double>::Max();
			double High = -TNumericLimits<double>::Max();
			for (int32 Arm = 0; Arm < 9; ++Arm)
			{
				const FVector3d Direction = Arm == 0 ? FVector3d::ZeroVector
					: ((Arm % 2) ? East : North) * ((Arm % 4) < 2 ? 1.0 : -1.0);
				const double Metres = (Arm == 0 ? 0.0 : (Arm < 5 ? 2500.0 : 5000.0));
				const double Ground = (Planet->SurfaceRadiusAt(RidgeAlong(Up, Direction, Metres, Radius)) - Radius) / 100.0;
				Low = FMath::Min(Low, Ground);
				High = FMath::Max(High, Ground);
			}
			if (Low <= 0.0 || High - Low < 400.0)
			{
				continue;
			}
			for (double When = Start; When <= Start + 5.0 * 86400.0; When += 6.0 * 3600.0)
			{
				const FVector2D Wind = LedgerWeather::WindAtAltitude(System, Home, Air, Lat, Lon, 1500.0, When);
				const double Score = (High - Low) * Wind.Size();
				if (Score > BestScore)
				{
					BestScore = Score;
					SiteUp = Up;
					WhenSeconds = When;
					ReliefMetres = High - Low;
					WindMetresPerSecond = Wind.Size();
					Downwind = (East * Wind.X + North * Wind.Y).GetSafeNormal();
				}
			}
		}
	}

	// The track: along the wind through the site, and a height that clears the
	// highest ground on it.
	StartUp = RidgeAlong(SiteUp, Downwind, -RidgeHalfTrackMetres, Radius);
	TrackMetres = 2.0 * RidgeHalfTrackMetres;
	double Highest = 0.0;
	for (double Metres = -RidgeHalfTrackMetres; Metres <= RidgeHalfTrackMetres; Metres += 200.0)
	{
		Highest = FMath::Max(Highest, (Planet->SurfaceRadiusAt(RidgeAlong(SiteUp, Downwind, Metres, Radius)) - Radius) / 100.0);
	}
	CruiseMetres = Highest + RidgeClearanceMetres;

	Lines.Add(TEXT("Across a mountain range in a gale (T098)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("site %.2f,%.2f: %.0f m of relief across ten kilometres, wind %.1f m/s at 1.5 km, t=%.0f s"),
		FMath::RadiansToDegrees(FMath::Asin(SiteUp.Z)), FMath::RadiansToDegrees(FMath::Atan2(SiteUp.Y, SiteUp.X)),
		ReliefMetres, WindMetresPerSecond, WhenSeconds));
	Lines.Add(FString::Printf(TEXT("track %.0f km downwind at %.0f m above the datum (%.0f m over the highest ground) and %.0f m/s"),
		TrackMetres / 1000.0, CruiseMetres, RidgeClearanceMetres, RidgeSpeedMetresPerSecond));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("ridge flight: %s"), *Line);
	}
}

void ULedgerRidgeFlight::Tick(float DeltaSeconds)
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
	const ULedgerWind* Wind = World->GetSubsystem<ULedgerWind>();
	if (Planet == nullptr || Ship == nullptr || Wind == nullptr)
	{
		return;
	}
	Builder->SetWhenSeconds(WhenSeconds);
	const FRidgeRun& Now = RidgeRuns[Step];
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double Gravity = Ship->SurfaceGravity / 100.0;
	const double Thrust = Ship->ManoeuvringThrust / 100.0;
	const float Trim = static_cast<float>(Gravity / Thrust);

	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		Controller->SetViewTarget(Camera);
	}

	if (!bStarted)
	{
		bStarted = true;
		Clock = 0.0;
		bShot = false;
		if (IConsoleVariable* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("Ledger.Wind.Scale")))
		{
			Scale->Set(Now.bWind ? 1.0f : 0.0f, ECVF_SetByCode);
		}
		Ship->SetActorHiddenInGame(false);
		Ship->SetActorLocation(FVector(Centre + StartUp * (Planet->Radius + CruiseMetres * 100.0)));
		Ship->SetVelocity(FVector(Downwind * RidgeSpeedMetresPerSecond * 100.0));
		Ship->SetFlightEnabled(false);
		Ship->SetAutoLift(Trim);
		Ship->SetAutoStrafe(0.0f);
	}
	Clock += DeltaSeconds;

	const FVector3d Offset = FVector3d(Ship->GetActorLocation()) - Centre;
	const FVector3d Up = Offset.GetSafeNormal();
	const FVector3d StartAlong = (Downwind - StartUp * FVector3d::DotProduct(Downwind, StartUp)).GetSafeNormal();
	// Along the great circle through the start in the wind's direction.
	const FVector3d Normal = FVector3d::CrossProduct(StartUp, StartAlong).GetSafeNormal();
	const FVector3d Along = FVector3d::CrossProduct(Normal, Up).GetSafeNormal();
	const double AlongMetres = FMath::Atan2(FVector3d::DotProduct(Up, StartAlong), FVector3d::DotProduct(Up, StartUp))
		* Planet->Radius / 100.0;
	const double CrossMetres = FMath::Asin(FMath::Clamp(FVector3d::DotProduct(Up, Normal), -1.0, 1.0)) * Planet->Radius / 100.0;
	const double Height = Offset.Length() / 100.0 - Planet->Radius / 100.0;

	Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(Along), FVector(Up)).Rotator());
	Camera->SetActorLocationAndRotation(
		FVector(FVector3d(Ship->GetActorLocation()) - Along * 4000.0 + Up * 1200.0),
		FRotationMatrix::MakeFromXZ(FVector(Along - Up * 0.1), FVector(Up)).Rotator());

	// Held still until the ground under the start has streamed in.
	if (!Ship->IsFlightEnabled())
	{
		Ship->SetActorLocation(FVector(Centre + StartUp * (Planet->Radius + CruiseMetres * 100.0)));
		Ship->SetVelocity(FVector(Along * RidgeSpeedMetresPerSecond * 100.0));
		if (Clock < RidgeSettleSeconds)
		{
			return;
		}
		Ship->SetFlightEnabled(true);
		Clock = 0.0;
	}

	// The throttle holds the speed along the track; everything across it and
	// up and down is left to the air -- and, when flown, to the autopilot.
	const FVector3d Velocity = FVector3d(Ship->GetVelocity()) / 100.0;
	const FVector3d Across = FVector3d::CrossProduct(Up, Along);
	const double Climb = FVector3d::DotProduct(Velocity, Up);
	const double Drift = FVector3d::DotProduct(Velocity, Across);
	Ship->SetVelocity(FVector((Along * RidgeSpeedMetresPerSecond + Across * Drift + Up * Climb) * 100.0));

	FRun& Run = Runs[Step];
	float Lift = Trim;
	float Strafe = 0.0f;
	if (Now.bFlown)
	{
		const double Command = RidgeHeightGain * (CruiseMetres - Height) - RidgeDampingGain * Climb;
		Lift = static_cast<float>((Gravity + Command) / Thrust);
		Strafe = static_cast<float>((-RidgeDampingGain * Drift - RidgeHeightGain * CrossMetres) / Thrust);
	}
	Ship->SetAutoLift(Lift);
	Ship->SetAutoStrafe(Strafe);

	// The air where the ship is, from the field -- the vertical part is what a
	// ridge does.
	const double FieldUp = FVector3d::DotProduct(Wind->WindAtMetres(Ship->GetActorLocation()), Up);
	Run.UpdraughtMax = FMath::Max(Run.UpdraughtMax, FieldUp);
	Run.DowndraughtMax = FMath::Min(Run.DowndraughtMax, FieldUp);
	Run.WorstHeightMetres = FMath::Max(Run.WorstHeightMetres, FMath::Abs(Height - CruiseMetres));
	Run.WorstCrossMetres = FMath::Max(Run.WorstCrossMetres, FMath::Abs(CrossMetres));
	// In thrust as a multiple of the ship's weight, off the trim.
	Run.LiftSquares += FMath::Square((Lift - Trim) * Thrust / Gravity);
	Run.StrafeSquares += FMath::Square(Strafe * Thrust / Gravity);
	Run.SumW += FieldUp;
	Run.SumV += Climb;
	Run.SumWW += FieldUp * FieldUp;
	Run.SumVV += Climb * Climb;
	Run.SumWV += FieldUp * Climb;
	++Run.Samples;
	Run.Seconds = Clock;
	const bool bLow = Ship->AltitudeMetres() < 5.0;
	Run.bHitGround = Run.bHitGround || bLow;

	if (!bShot && AlongMetres > RidgeHalfTrackMetres && Now.bWind && Now.bFlown)
	{
		bShot = true;
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("ridge-crossing.png"))), false, false);
	}

	if (AlongMetres < TrackMetres && Clock < RidgeMaxSeconds && !bLow)
	{
		return;
	}

	Ship->SetFlightEnabled(false);
	const double LiftRms = FMath::Sqrt(Run.LiftSquares / FMath::Max(Run.Samples, 1));
	const double StrafeRms = FMath::Sqrt(Run.StrafeSquares / FMath::Max(Run.Samples, 1));
	const FString Line = FString::Printf(
		TEXT("%-16s %.0f s: height off by up to %.0f m, %.0f m across; corrections %.3f g up/down and %.3f g sideways rms; field %+.1f to %+.1f m/s vertical; ship follows it r=%.2f%s"),
		Now.What, Run.Seconds, Run.WorstHeightMetres, Run.WorstCrossMetres, LiftRms, StrafeRms,
		Run.DowndraughtMax, Run.UpdraughtMax, Run.Correlation(), Run.bHitGround ? TEXT(", HIT THE GROUND") : TEXT(""));
	UE_LOG(LogLedger, Log, TEXT("ridge flight: %s"), *Line);
	Lines.Add(Line);

	++Step;
	bStarted = false;
	if (Step >= UE_ARRAY_COUNT(RidgeRuns))
	{
		bRunning = false;
		if (IConsoleVariable* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("Ledger.Wind.Scale")))
		{
			Scale->Set(1.0f, ECVF_SetByCode);
		}
		Finish();
	}
}

void ULedgerRidgeFlight::Finish()
{
	const FRun& Calm = Runs[0];
	const FRun& Loose = Runs[1];
	const FRun& Flown = Runs[2];
	auto Rms = [](const FRun& Run) { return FMath::Sqrt(Run.LiftSquares / FMath::Max(Run.Samples, 1)); };

	// Needs input: hands off, the gale moves the ship a long way off its height
	// (or into the ground); flown, the autopilot works far harder than in calm.
	const bool bNeedsInput = (Loose.WorstHeightMetres > 50.0 || Loose.bHitGround)
		&& Rms(Flown) > 5.0 * FMath::Max(Rms(Calm), 1.0e-4);
	// From the field: the loose ship climbs and sinks with the field's vertical
	// wind, and there was a vertical wind to follow.
	const bool bFromField = Loose.Correlation() > 0.7 && Loose.UpdraughtMax - Loose.DowndraughtMax > 1.0;
	const bool bHeld = !Flown.bHitGround && !Calm.bHitGround;

	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("crossing in the gale needs control input: %s"), bNeedsInput ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("the forces come from the wind field: %s"), bFromField ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("flown, the ship clears the range: %s"), bHeld ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bNeedsInput && bFromField && bHeld ? TEXT("PASS") : TEXT("FAIL")));
	for (int32 Index = Lines.Num() - 5; Index < Lines.Num(); ++Index)
	{
		UE_LOG(LogLedger, Log, TEXT("ridge flight: %s"), *Lines[Index]);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("ridge.txt"))));
	FPlatformMisc::RequestExit(false);
}
