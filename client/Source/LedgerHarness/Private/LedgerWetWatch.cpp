#include "LedgerWetWatch.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerClimate.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerPrecipitationView.h"
#include "LedgerShip.h"
#include "LedgerSky.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr double WetFirstSettle = 25.0;
	constexpr double WetStepSettle = 8.0;
	constexpr double WetEyeMetres = 1.7;

	/// The sun has to be this high for the whole run, degrees. Wet ground is
	/// read by its sheen, and there is no sheen without a light to reflect.
	constexpr double WetMinSunDegrees = 15.0;

	struct FWetStep
	{
		const TCHAR* What;
		double MinutesFromStop;
	};

	const FWetStep WetSteps[] =
	{
		{ TEXT("raining"),  -15.0 },
		{ TEXT("stopped"),    0.0 },
		{ TEXT("20min"),     20.0 },
		{ TEXT("40min"),     40.0 },
		{ TEXT("60min"),     60.0 },
		{ TEXT("120min"),   120.0 },
	};
}

bool ULedgerWetWatch::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerWetWatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerWetWatch, STATGROUP_Tickables);
}

void ULedgerWetWatch::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("wetwatch"));
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
	Home = Builder->GetHomeBodyIndex();
	Air = LedgerAir::For(System, Home, Builder->GetWhenSeconds());

	// A kilometre off the town, as the surface study stands: the town itself is
	// buildings, and this is about the ground.
	const FVector3d Site = Builder->GetSiteDirection().GetSafeNormal();
	FVector3d Sideways = FVector3d::CrossProduct(Site, FVector3d::UpVector);
	if (Sideways.IsNearlyZero())
	{
		Sideways = FVector3d::CrossProduct(Site, FVector3d::ForwardVector);
	}
	Anchor = (Site + Sideways.GetSafeNormal() * (100000.0 / Planet->Radius)).GetSafeNormal();
	Latitude = FMath::Asin(FMath::Clamp(Anchor.Z, -1.0, 1.0));
	Longitude = FMath::Atan2(Anchor.Y, Anchor.X);
	SurfaceKelvin = LedgerClimate::At(
		Anchor, Planet->TerrainParams(), Planet->SeasonPhase()).TemperatureC + 273.15;

	auto Raining = [this](double When)
	{
		const FLedgerPrecipitation Falling = LedgerPrecip::At(
			System, Home, Air, Latitude, Longitude, 0.0, When, SurfaceKelvin);
		return Falling.Kind == ELedgerPrecipitation::Rain && Falling.IsFalling();
	};
	auto Lit = [this](double When)
	{
		return FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Anchor, When))
			> WetMinSunDegrees;
	};

	const double Start = Builder->GetWhenSeconds();
	constexpr double Span = 30.0 * 86400.0;
	constexpr double Stride = 300.0;
	bool bFound = false;
	for (double When = Start + Stride; When <= Start + Span && !bFound; When += Stride)
	{
		if (!Raining(When - Stride) || Raining(When) || !Lit(When) || !Lit(When + 7200.0))
		{
			continue;
		}
		bool bStaysDry = true;
		for (double Later = When; Later <= When + 7200.0 && bStaysDry; Later += Stride)
		{
			bStaysDry = !Raining(Later);
		}
		if (bStaysDry && LedgerPrecip::SurfaceWaterAt(System, Home, Air,
			Latitude, Longitude, When, SurfaceKelvin).PuddleLevel > 0.5)
		{
			StopSeconds = When;
			bFound = true;
		}
	}

	const FString Head = FString::Printf(
		TEXT("wet watch: site %.4f,%.4f at %.1f C, drying %.2f mm/h; rain stops %s at t=%.0f s (%.1f days on)"),
		FMath::RadiansToDegrees(Latitude), FMath::RadiansToDegrees(Longitude),
		SurfaceKelvin - 273.15, LedgerPrecip::DryingMillimetresPerHour(SurfaceKelvin),
		bFound ? TEXT("in daylight") : TEXT("NEVER in daylight in thirty days"),
		StopSeconds, (StopSeconds - Start) / 86400.0);
	UE_LOG(LogLedger, Log, TEXT("%s"), *Head);
	Lines.Add(TEXT("The rain stops, and the ground dries (T096)."));
	Lines.Add(TEXT(""));
	Lines.Add(Head);
	Lines.Add(TEXT(""));
	if (!bFound)
	{
		bRunning = false;
		Finish();
	}
}

void ULedgerWetWatch::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	const int32 Which = FMath::Clamp(bAimed ? Step - 1 : Step, 0, UE_ARRAY_COUNT(WetSteps) - 1);
	Builder->SetWhenSeconds(StopSeconds + WetSteps[Which].MinutesFromStop * 60.0);

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const FVector Eye = FVector(
		Centre + Anchor * (Planet->SurfaceRadiusAt(Anchor) + WetEyeMetres * 100.0));

	// Towards the sun and down, so the wet ground is seen by the light it
	// reflects -- which is how anybody tells wet from dark -- with the horizon
	// just inside the top of the frame and the sun above it.
	// The sun as the rain stops, held for every frame: the same ground from the
	// same place, or the dry frame is a different picture and not a comparison.
	const FVector3d Sun = LedgerSky::SunDirectionInSurface(
		System, Home, Anchor, StopSeconds);
	const FVector2D Toward2 = FVector2D(Sun.X, Sun.Y).GetSafeNormal();
	const FVector3d Toward = LedgerFrames::ToBody(
		{ Home, Anchor, FVector3d(Toward2.X, Toward2.Y, -0.35) }).Metres.GetSafeNormal();

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye + FVector(Anchor * 400000.0));
	}

	const FRotator Look = FRotationMatrix::MakeFromXZ(FVector(Toward), FVector(Anchor)).Rotator();
	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, Look, SpawnParams);
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

void ULedgerWetWatch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	Place();

	Settle += DeltaSeconds;
	if (Settle < (Step == 0 && !bAimed ? WetFirstSettle : WetStepSettle))
	{
		return;
	}

	if (bAimed)
	{
		const int32 Which = Step - 1;
		const FWetStep& Now = WetSteps[Which];
		const double When = StopSeconds + Now.MinutesFromStop * 60.0;

		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("wet-%d-%s.png"), Which, Now.What))), false, false);

		// What the view published to the materials -- the number the picture
		// was drawn with -- beside what the model answers directly.
		const ULedgerPrecipitationView* View = GetWorld()->GetSubsystem<ULedgerPrecipitationView>();
		const FLedgerSurfaceWater Drawn = View != nullptr ? View->SurfaceWater() : FLedgerSurfaceWater();
		const FLedgerSurfaceWater Model = LedgerPrecip::SurfaceWaterAt(
			System, Home, Air, Latitude, Longitude, When, SurfaceKelvin);
		const FLedgerPrecipitation Falling = LedgerPrecip::At(
			System, Home, Air, Latitude, Longitude, 0.0, When, SurfaceKelvin);
		Seen.Add(Drawn);

		const FString Line = FString::Printf(
			TEXT("wet watch %d/%d: %-8s %+5.0f min, %s %.2f mm/h, water %.2f mm, wetness %.2f, puddles %.2f (model %.2f/%.2f), sun %.0f deg"),
			Step, static_cast<int32>(UE_ARRAY_COUNT(WetSteps)), Now.What, Now.MinutesFromStop,
			LexToString(Falling.Kind), Falling.RateMillimetresPerHour, Drawn.Millimetres,
			Drawn.Wetness, Drawn.PuddleLevel, Model.Wetness, Model.PuddleLevel,
			FMath::RadiansToDegrees(LedgerSky::SolarAltitude(System, Home, Anchor, When)));
		UE_LOG(LogLedger, Log, TEXT("%s"), *Line);
		Lines.Add(Line);

		bAimed = false;
		Settle = 0.0;
		return;
	}

	if (Step >= UE_ARRAY_COUNT(WetSteps))
	{
		bRunning = false;
		Finish();
		return;
	}

	++Step;
	bAimed = true;
	Settle = 0.0;
}

void ULedgerWetWatch::Finish()
{
	// Frame 1 is the moment it stops and frame 4 an hour later.
	const bool bWet = Seen.Num() > 4 && Seen[1].Wetness >= 0.99 && Seen[1].PuddleLevel > 0.0;
	const bool bDry = Seen.Num() > 4 && Seen[4].Wetness == 0.0 && Seen[4].PuddleLevel == 0.0;
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("wet with puddles as the rain stops: %s"), bWet ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("both gone an hour later: %s"), bDry ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bWet && bDry ? TEXT("PASS") : TEXT("FAIL")));
	for (int32 Index = Lines.Num() - 3; Index < Lines.Num(); ++Index)
	{
		UE_LOG(LogLedger, Log, TEXT("wet watch: %s"), *Lines[Index]);
	}

	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("wet.txt"))));
	FPlatformMisc::RequestExit(false);
}
