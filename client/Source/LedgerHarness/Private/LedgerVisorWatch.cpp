#include "LedgerVisorWatch.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerClimate.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerPrecipitation.h"
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
	struct FVisorStep
	{
		const TCHAR* What;
		double SpeedMetresPerSecond;
		bool bWipers;
		bool bHeater;
		double Seconds;
		/// Above the ground. The fog is photographed three kilometres up, where
		/// the air -- and so the glass -- is twenty degrees colder than the
		/// cabin's dew point allows.
		double HeightMetres;
	};

	const FVisorStep VisorSteps[] =
	{
		{ TEXT("slow"),   15.0,  false, false, 30.0, 150.0 },
		{ TEXT("fast"),   150.0, false, false, 20.0, 150.0 },
		{ TEXT("fogged"), 15.0,  true,  false, 60.0, 3000.0 },
		{ TEXT("heated"), 15.0,  true,  true,  30.0, 3000.0 },
	};
}

bool ULedgerVisorWatch::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerVisorWatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerVisorWatch, STATGROUP_Tickables);
}

void ULedgerVisorWatch::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("visorwatch"));
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
	Air = LedgerAir::For(System, Home, Builder->GetWhenSeconds());
	// **Where it is raining, rather than waiting for rain to come to the town.**
	// The first version walked thirty days at the town and never found a
	// daylit shower over a millimetre an hour, so every frame was dry. Rain is
	// heaviest under the deepest low, so the site is the first daylit low in
	// ten days deep enough to rain hard, warm enough underneath that it is rain.
	const double Start = Builder->GetWhenSeconds();
	RainSeconds = -1.0;
	double Latitude = 0.0;
	double Longitude = 0.0;
	double Kelvin = 0.0;
	for (double When = Start; When < Start + 10.0 * 86400.0 && RainSeconds < 0.0; When += 3600.0)
	{
		TArray<FLedgerPressureCell> Lows;
		LedgerWeather::DeepestLows(System, Home, When, 4, Lows);
		for (const FLedgerPressureCell& Low : Lows)
		{
			const FVector3d Up(FMath::Cos(Low.LatitudeRadians) * FMath::Cos(Low.LongitudeRadians),
				FMath::Cos(Low.LatitudeRadians) * FMath::Sin(Low.LongitudeRadians), FMath::Sin(Low.LatitudeRadians));
			const double Ground = LedgerClimate::At(Up, Planet->TerrainParams(), Planet->SeasonPhase()).TemperatureC + 273.15;
			const FLedgerPrecipitation Falling = LedgerPrecip::At(System, Home, Air,
				Low.LatitudeRadians, Low.LongitudeRadians, 0.0, When, Ground);
			if (Falling.Kind == ELedgerPrecipitation::Rain && Falling.RateMillimetresPerHour > 2.0
				&& Planet->SurfaceRadiusAt(Up) > Planet->Radius
				&& FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Up, When)) > 20.0)
			{
				RainSeconds = When;
				Anchor = Up;
				Latitude = Low.LatitudeRadians;
				Longitude = Low.LongitudeRadians;
				Kelvin = Ground;
				break;
			}
		}
	}
	Lines.Add(TEXT("Rain on the canopy, and fog cleared by the heater (T101)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("site %.2f,%.2f at %.1f C; rain in daylight at t=%.0f s"),
		FMath::RadiansToDegrees(Latitude), FMath::RadiansToDegrees(Longitude), Kelvin - 273.15, RainSeconds));
	UE_LOG(LogLedger, Log, TEXT("visor watch: %s"), *Lines.Last());
	if (RainSeconds < 0.0)
	{
		bRunning = false;
		Finish();
	}
}

void ULedgerVisorWatch::Tick(float DeltaSeconds)
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
	if (Planet == nullptr || Ship == nullptr)
	{
		return;
	}
	Builder->SetWhenSeconds(RainSeconds);
	const FVisorStep& Now = VisorSteps[Step];

	// Through the ship's own camera, at the front of it, where the canopy is.
	if (Clock == 0.0 && Step == 0)
	{
		Controller->SetViewTarget(Ship);
		Ship->SetCameraBoom(-200.0f, 120.0f);
		Ship->SetActorHiddenInGame(false);
	}
	Ship->SetFlightEnabled(false);
	Ship->SetWipers(Now.bWipers);
	Ship->SetCanopyHeater(Now.bHeater);

	// Flown by hand along a line east of the town, level at a fixed height.
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Anchor).GetSafeNormal();
	Along += Now.SpeedMetresPerSecond * DeltaSeconds;
	const FVector3d Up = (Anchor + East * (Along * 100.0 / Planet->Radius)).GetSafeNormal();
	const FVector3d Heading = (East - Up * FVector3d::DotProduct(East, Up)).GetSafeNormal();
	Ship->SetActorLocation(FVector(Centre + Up * (Planet->SurfaceRadiusAt(Up) + Now.HeightMetres * 100.0)));
	Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(Heading - Up * 0.1), FVector(Up)).Rotator());
	Ship->SetVelocity(FVector(Heading * Now.SpeedMetresPerSecond * 100.0));

	Clock += DeltaSeconds;
	if (Clock < Now.Seconds)
	{
		return;
	}

	FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			FString::Printf(TEXT("visor-%d-%s.png"), Step, Now.What))), false, false);
	const FLedgerCanopy& Canopy = Ship->CanopyState();
	Water[Step] = Canopy.Water;
	Flow[Step] = Canopy.Flow;
	Fog[Step] = Canopy.Fog;
	const FString Line = FString::Printf(
		TEXT("%-7s %3.0f m/s, wipers %s, heater %s: water %.2f running %+.2f, fog %.2f, ice %.2f, glass %.1f C"),
		Now.What, Now.SpeedMetresPerSecond, Now.bWipers ? TEXT("on ") : TEXT("off"), Now.bHeater ? TEXT("on ") : TEXT("off"),
		Canopy.Water, Canopy.Flow, Canopy.Fog, Canopy.Ice, Canopy.GlassKelvin - 273.15);
	UE_LOG(LogLedger, Log, TEXT("visor watch: %s"), *Line);
	Lines.Add(Line);

	++Step;
	Clock = 0.0;
	if (Step >= UE_ARRAY_COUNT(VisorSteps))
	{
		bRunning = false;
		Finish();
	}
}

void ULedgerVisorWatch::Finish()
{
	const bool bStreaks = Water[0] > 0.2 && Flow[0] < 0.0;
	const bool bBackward = Water[1] > 0.1 && Flow[1] > 0.0;
	const bool bClears = Fog[2] > 0.5 && Fog[3] < 0.1;
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("through rain the canopy streaks: %s"), bStreaks ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("faster, the streaks run backward: %s"), bBackward ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("the heater clears the fog: %s"), bClears ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bStreaks && bBackward && bClears ? TEXT("PASS") : TEXT("FAIL")));
	for (int32 Index = Lines.Num() - 4; Index < Lines.Num(); ++Index)
	{
		UE_LOG(LogLedger, Log, TEXT("visor watch: %s"), *Lines[Index]);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("visor.txt"))));
	FPlatformMisc::RequestExit(false);
}
