#include "LedgerAirShow.h"
#include "LedgerCloud.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerSky.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr double AirShowFirstSettle = 25.0;
	/// Fourteen, not six: the sunset frame is twenty stops from the zenith one
	/// and auto-exposure travels at about one and a fifth a second.
	constexpr double AirShowStepSettle = 14.0;

	/// Two views, and they answer different questions.
	///
	/// From the ground the question is what the sky over your head looks like:
	/// how blue, how bright near the sun, how fast it darkens towards the
	/// zenith. From two radii out it is whether the limb has the right depth
	/// and colour and whether space behind it is black.
	struct FAirShowView
	{
		const TCHAR* What;
		double AltitudeInRadii;   // 0 stands on the ground
		double LookUp;            // ENU z-component of the aim
		float FieldOfView;
	};

	const FAirShowView AirShowViews[] =
	{
		{ TEXT("ground"), 0.0,  0.35, 70.0f },
		{ TEXT("zenith"), 0.0,  4.00, 70.0f },
		// **Towards the sun with the sun on the horizon.** This is the other
		// half of T090: the sky away from the sun is one colour and the sky
		// around it is another, and on a dusty world they are supposed to be
		// opposite ways round from ours.
		{ TEXT("sunset"), 0.0,  0.05, 40.0f },
		{ TEXT("orbit"),  2.0,  0.00, 50.0f },
	};

	constexpr double AirShowEyeMetres = 40.0;
}

bool ULedgerAirShow::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerAirShow::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerAirShow, STATGROUP_Tickables);
}

void ULedgerAirShow::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bClimb = FParse::Param(FCommandLine::Get(), TEXT("cloudclimb"));
	bRunning = bClimb || FParse::Param(FCommandLine::Get(), TEXT("airshow"));
	if (!bRunning)
	{
		return;
	}

	ULedgerWorldBuilder* Builder = InWorld.GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr)
	{
		bRunning = false;
		return;
	}
	System = Builder->GetSystem();
	Anchor = Builder->GetSiteDirection().GetSafeNormal();
	Home = Builder->GetHomeBodyIndex();
	Air = LedgerAir::For(System, Home, Builder->GetWhenSeconds());

	// Find sunset from the ephemeris, the same way the passage fixture finds a
	// moonrise: scan for the descending crossing and bisect it.
	NoonSeconds = Builder->GetWhenSeconds();
	const double Day = LedgerSky::SolarDaySeconds(System, Home, Anchor, NoonSeconds);
	if (Day > 0.0)
	{
		const int32 Samples = 2000;
		double Previous = LedgerSky::SolarAltitude(System, Home, Anchor, NoonSeconds);
		for (int32 Sample = 1; Sample <= Samples; ++Sample)
		{
			const double At = NoonSeconds + Day * Sample / Samples;
			const double Now = LedgerSky::SolarAltitude(System, Home, Anchor, At);
			if (Previous > 0.0 && Now <= 0.0)
			{
				double Low = At - Day / Samples;
				double High = At;
				for (int32 Step2 = 0; Step2 < 60; ++Step2)
				{
					const double Middle = (Low + High) * 0.5;
					(LedgerSky::SolarAltitude(System, Home, Anchor, Middle) > 0.0
						? Low : High) = Middle;
				}
				SunsetSeconds = (Low + High) * 0.5;
				break;
			}
			Previous = Now;
		}
	}

	UE_LOG(LogLedger, Log,
		TEXT("air show: body %d (%s), %s at %.0f Pa and %.1f K"),
		Home, *System.Bodies[Home].Name, LexToString(Air.Composition),
		Air.SurfacePressurePascals, Air.SurfaceTemperatureKelvin);
}

void ULedgerAirShow::BuildClimb(ALedgerPlanet* Planet)
{
	// **From the lowest ground near the site, so the climb starts under every
	// deck.** The decks are altitudes above sea level and a site can stand above
	// one: this world's plateau is 2,365 m, over the whole cumulus deck, and a
	// climb from it photographs the ground four times and calls it three decks.
	// ponytail: 24 bearings on rings every 20 km out to 200 km -- coarse, and
	// enough, because what it is looking for is sea or a lowland, which are wide.
	FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Anchor);
	if (East.IsNearlyZero())
	{
		East = FVector3d::CrossProduct(FVector3d::UnitX(), Anchor);
	}
	East.Normalize();
	const FVector3d North = FVector3d::CrossProduct(Anchor, East);
	const double SiteRadius = Planet->SurfaceRadiusAt(Anchor);
	FVector3d Lowest = Anchor;
	double LowestRadius = SiteRadius;
	for (int32 Ring = 1; Ring <= 10; ++Ring)
	{
		const double Angle = Ring * 2000000.0 / Planet->Radius;
		for (int32 Bearing = 0; Bearing < 24; ++Bearing)
		{
			const double Theta = UE_TWO_PI * Bearing / 24.0;
			const FVector3d Try = (Anchor * FMath::Cos(Angle)
				+ (East * FMath::Cos(Theta) + North * FMath::Sin(Theta)) * FMath::Sin(Angle)).GetSafeNormal();
			const double Radius = Planet->SurfaceRadiusAt(Try);
			if (Radius < LowestRadius)
			{
				LowestRadius = Radius;
				Lowest = Try;
			}
		}
	}
	UE_LOG(LogLedger, Log, TEXT("cloud climb: the site's ground is %.0f m; climbing from %.0f m, %.0f km away"),
		(SiteRadius - Planet->Radius) / 100.0, (LowestRadius - Planet->Radius) / 100.0,
		FMath::Acos(FMath::Clamp(FVector3d::DotProduct(Anchor, Lowest), -1.0, 1.0)) * Planet->Radius / 100000.0);
	Anchor = Lowest;

	// **The climb, from the profile the decks are drawn from.** The same call
	// the world builder makes, at the same place and hour, so the altitudes
	// photographed are the ones the atmosphere was given.
	const double Latitude = FMath::Asin(FMath::Clamp(Anchor.Z, -1.0, 1.0));
	const double Longitude = FMath::Atan2(Anchor.Y, Anchor.X);
	const FLedgerCloudDecks Decks = LedgerCloud::DecksAt(
		System, Home, Air, Latitude, Longitude, NoonSeconds);
	auto Add = [this](const FString& Name, double Metres, double LookUp)
	{
		ClimbNames.Add(Name);
		ClimbMetres.Add(Metres);
		ClimbLookUp.Add(LookUp);
	};
	auto Deck = [this, &Add](const TCHAR* Name, const FLedgerCloudDeck& Layer)
	{
		ClimbDecks += FString::Printf(TEXT("%-8s %s"), Name, Layer.bPresent
			? *FString::Printf(TEXT("%.0f to %.0f m, cover %.2f"), Layer.BaseMetres, Layer.TopMetres, Layer.Coverage)
			: TEXT("absent")) + LINE_TERMINATOR;
		if (Layer.bPresent)
		{
			Add(FString::Printf(TEXT("below-%s"), Name), Layer.BaseMetres - 300.0, 0.25);
			Add(FString::Printf(TEXT("in-%s"), Name), 0.5 * (Layer.BaseMetres + Layer.TopMetres), 0.0);
			Add(FString::Printf(TEXT("above-%s"), Name), Layer.TopMetres + 300.0, -0.25);
		}
	};
	Deck(TEXT("cumulus"), Decks.Cumulus);
	Deck(TEXT("middle"), Decks.Middle);
	Deck(TEXT("cirrus"), Decks.Cirrus);
	Add(TEXT("orbit"), -1.0, 0.0);
	UE_LOG(LogLedger, Log, TEXT("cloud climb: %d views%s%s"),
		ClimbNames.Num(), LINE_TERMINATOR, *ClimbDecks);
}

void ULedgerAirShow::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}
	if (bClimb && ClimbNames.Num() == 0)
	{
		BuildClimb(Planet);
	}

	const int32 Which = FMath::Clamp(bAimed ? Step - 1 : Step, 0, ViewCount() - 1);
	FAirShowView View = AirShowViews[FMath::Min(Which, static_cast<int32>(UE_ARRAY_COUNT(AirShowViews)) - 1)];
	double ClimbMetresHere = -1.0;
	if (bClimb)
	{
		View.What = *ClimbNames[Which];
		ClimbMetresHere = ClimbMetres[Which];
		View.AltitudeInRadii = ClimbMetresHere < 0.0 ? 2.0 : 0.0;
		View.LookUp = ClimbLookUp[Which];
		View.FieldOfView = ClimbMetresHere < 0.0 ? 50.0f : 70.0f;
	}

	// The sunset frame is a different moment, not just a different aim.
	const bool bSunset = FString(View.What) == TEXT("sunset");
	if (bSunset && SunsetSeconds > 0.0)
	{
		Builder->SetWhenSeconds(SunsetSeconds);
	}
	else if (!bSunset)
	{
		Builder->SetWhenSeconds(NoonSeconds);
	}

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Anchor);
	const double Height = View.AltitudeInRadii > 0.0
		? Planet->Radius * View.AltitudeInRadii
		: AirShowEyeMetres * 100.0;
	// A climb step stands at its altitude above sea level, which is what the
	// deck profile gives -- and never below the ground at the site.
	const FVector Eye = ClimbMetresHere >= 0.0
		? FVector(Centre + Anchor * FMath::Max(Planet->Radius + ClimbMetresHere * 100.0,
			Ground + AirShowEyeMetres * 100.0))
		: FVector(Centre + Anchor * (Ground + Height));

	// **The sun's bearing, so every body is photographed with the light in the
	// same place.** Comparing three skies means comparing three skies, not
	// three sun angles.
	const FVector3d Sun =
		LedgerSky::SunDirectionInSurface(System, Home, Anchor, Builder->GetWhenSeconds());
	const FVector3d Bearing =
		FVector3d(Sun.X, Sun.Y, 0.0).GetSafeNormal();

	FVector3d Toward;
	if (View.AltitudeInRadii > 0.0)
	{
		// **Aim at the limb, and work out where that is.**
		//
		// The first version pointed 29 degrees below horizontal and photographed
		// nothing at all, because from two radii up the planet is not below the
		// horizon -- it is nearly underneath you. The angle from straight down
		// to the edge of the disc is asin(R / (R + h)), which is 19 degrees at
		// this altitude, so the limb sits 70 degrees below horizontal and not 29.
		const double Radii = 1.0 + View.AltitudeInRadii;
		const double ToLimb = FMath::Asin(FMath::Clamp(1.0 / Radii, -1.0, 1.0));
		// Three degrees short of the edge, so the frame holds the limb, the
		// atmosphere above it and the black behind that.
		const double Depression = LedgerPi * 0.5 - ToLimb - FMath::DegreesToRadians(3.0);
		const double Down = -FMath::Tan(Depression);
		Toward = LedgerFrames::ToBody(
			{ Home, Anchor, FVector3d(Bearing.X, Bearing.Y, Down) }).Metres
			.GetSafeNormal();
	}
	else
	{
		Toward = LedgerFrames::ToBody(
			{ Home, Anchor, FVector3d(Bearing.X, Bearing.Y, View.LookUp) }).Metres
			.GetSafeNormal();
	}

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
		Camera->GetCameraComponent()->SetFieldOfView(View.FieldOfView);
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

void ULedgerAirShow::Tick(float DeltaSeconds)
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
	if (bClimb && ClimbNames.Num() == 0)
	{
		return;
	}

	Settle += DeltaSeconds;
	// The first frame gets a long settle because auto-exposure is an
	// *adaptation*: photographing before it has finished photographs the
	// adaptation, which has cost this project two wrong conclusions already.
	if (Settle < (Step == 0 && !bAimed ? AirShowFirstSettle : AirShowStepSettle))
	{
		return;
	}

	if (bAimed)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("airshow-%d-%s.png"),
					Home, ViewName(Step - 1))));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bAimed = false;
		Settle = 0.0;
		return;
	}

	if (Step >= ViewCount())
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	UE_LOG(LogLedger, Log, TEXT("air show %d/%d: %s"),
		Step + 1, ViewCount(),
		ViewName(Step));
	// A climb step under the site's ground is photographed from the ground, so
	// say so: that frame is not at the altitude its name gives.
	if (bClimb && ClimbMetres[Step] >= 0.0)
	{
		const ALedgerPlanet* Planet = GetWorld()->GetSubsystem<ULedgerWorldBuilder>()->GetPlanet();
		const double GroundMetres = Planet != nullptr
			? (Planet->SurfaceRadiusAt(Anchor) - Planet->Radius) / 100.0 : 0.0;
		if (ClimbMetres[Step] < GroundMetres + AirShowEyeMetres)
		{
			UE_LOG(LogLedger, Warning, TEXT("air show: %s wants %.0f m, but the ground here is at %.0f m; photographed from %.0f m above it"),
				ViewName(Step), ClimbMetres[Step], GroundMetres, static_cast<double>(AirShowEyeMetres));
		}
	}
	++Step;
	bAimed = true;
	Settle = 0.0;
}

void ULedgerAirShow::Report()
{
	FString Body;
	Body += FString::Printf(
		TEXT("%s: an atmosphere from data alone (T089).%s%s"),
		*System.Bodies[Home].Name, LINE_TERMINATOR, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("composition      %s%s"),
		LexToString(Air.Composition), LINE_TERMINATOR);
	Body += FString::Printf(TEXT("surface          %.0f Pa at %.1f K%s"),
		Air.SurfacePressurePascals, Air.SurfaceTemperatureKelvin, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("scale height     %.2f km%s"),
		Air.ScaleHeightMetres / 1000.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("lapse rate       %.2f K/km%s"),
		Air.LapseRateKelvinPerMetre * 1000.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("top of air       %.1f km%s"),
		Air.TopMetres / 1000.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("Rayleigh /km     %.4f blue, %.4f green, %.4f red%s"),
		Air.RayleighPerMetre.X * 1000.0, Air.RayleighPerMetre.Y * 1000.0,
		Air.RayleighPerMetre.Z * 1000.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("aerosols /km     %.5f over %.2f km%s"),
		Air.MiePerMetre * 1000.0, Air.MieScaleHeightMetres / 1000.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("ozone /km        %.5f%s"),
		Air.OzoneAbsorptionPerMetre * 1000.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("clouds           %s%s"),
		Air.bHasClouds
			? *FString::Printf(TEXT("%.1f to %.1f km"),
				Air.CloudBaseMetres / 1000.0, Air.CloudTopMetres / 1000.0)
			: TEXT("none"),
		LINE_TERMINATOR);

	if (bClimb)
	{
		Body += FString(TEXT("cloud decks at the site, from the profile:")) + LINE_TERMINATOR + ClimbDecks;
	}
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			FString::Printf(TEXT("airshow-%d.txt"), Home)));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("air show: wrote %s"), *Path);
}

int32 ULedgerAirShow::ViewCount() const
{
	return bClimb ? ClimbNames.Num() : static_cast<int32>(UE_ARRAY_COUNT(AirShowViews));
}

const TCHAR* ULedgerAirShow::ViewName(int32 Index) const
{
	return bClimb ? *ClimbNames[Index] : AirShowViews[Index].What;
}
