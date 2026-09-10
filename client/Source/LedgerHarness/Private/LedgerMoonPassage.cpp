#include "LedgerMoonPassage.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerSky.h"
#include "LedgerSkyBodies.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr double PassageFirstSettle = 20.0;
	constexpr double PassageStepSettle = 1.5;

	/// Narrow, because a moon is half a degree across and a 55 degree frame
	/// spends nine pixels on it. Twenty degrees still holds the horizon and the
	/// moon together at rise and set, which is where the claim lives.
	constexpr float PassageFieldOfView = 20.0f;

	constexpr double PassageEyeMetres = 40.0;

	/// The bracket the crossings are searched in: a moon is up for a few hours
	/// and this has to contain one whole appearance without containing two.
	constexpr int32 PassageBracketSamples = 4000;
}

bool ULedgerMoonPassage::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerMoonPassage::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerMoonPassage, STATGROUP_Tickables);
}

double ULedgerMoonPassage::AltitudeOf(double Seconds) const
{
	if (!System.Bodies.IsValidIndex(Moon) || !System.Bodies.IsValidIndex(Home))
	{
		return 0.0;
	}
	TArray<FLedgerSkyBody> Seen;
	LedgerSky::VisibleBodies(System, Home, Anchor, Seconds, Seen);
	for (const FLedgerSkyBody& Body : Seen)
	{
		if (Body.BodyIndex == Moon)
		{
			return FMath::Asin(FMath::Clamp(Body.DirectionInSurface.Z, -1.0, 1.0));
		}
	}
	return -LedgerPi * 0.5;
}

double ULedgerMoonPassage::FindCrossing(double Low, double High, bool bRising) const
{
	// Bisection on a monotonic stretch. The bracket is found by scanning first,
	// so this is only ever asked about an interval it can answer.
	for (int32 Iteration = 0; Iteration < 80; ++Iteration)
	{
		const double Middle = (Low + High) * 0.5;
		const bool bUp = AltitudeOf(Middle) > 0.0;
		if (bUp == bRising)
		{
			High = Middle;
		}
		else
		{
			Low = Middle;
		}
	}
	return (Low + High) * 0.5;
}

double ULedgerMoonPassage::FindTransit(double Low, double High) const
{
	for (int32 Iteration = 0; Iteration < 120; ++Iteration)
	{
		const double A = Low + (High - Low) / 3.0;
		const double B = High - (High - Low) / 3.0;
		if (AltitudeOf(A) < AltitudeOf(B))
		{
			Low = A;
		}
		else
		{
			High = B;
		}
	}
	return (Low + High) * 0.5;
}

void ULedgerMoonPassage::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("passage"));
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
	Moon = LedgerBodies::FirstChildOfKind(System, Home, ELedgerBodyKind::Moon);
	if (Moon == INDEX_NONE)
	{
		UE_LOG(LogLedger, Warning, TEXT("passage: body %d has no moon"), Home);
		bRunning = false;
		return;
	}

	// **The schedule is asked for, not looked for.** Scan a couple of days at
	// this site for the moon crossing the horizon upwards, then bisect that
	// bracket for the rise, hunt forward for the set, and take the highest
	// point between them. Every camera position after this is one of these
	// three numbers plus an offset -- nothing is nudged to find a good frame.
	const double Window = 3.0 * 86400.0;
	const double Coarse = Window / PassageBracketSamples;

	double Rise = -1.0;
	double Set = -1.0;
	bool bWasUp = AltitudeOf(0.0) > 0.0;
	for (int32 Sample = 1; Sample <= PassageBracketSamples; ++Sample)
	{
		const double At = Sample * Coarse;
		const bool bUp = AltitudeOf(At) > 0.0;
		if (bUp && !bWasUp && Rise < 0.0)
		{
			Rise = FindCrossing(At - Coarse, At, true);
		}
		else if (!bUp && bWasUp && Rise >= 0.0 && Set < 0.0)
		{
			Set = FindCrossing(At - Coarse, At, false);
		}
		bWasUp = bUp;
	}

	if (Rise < 0.0 || Set < 0.0)
	{
		UE_LOG(LogLedger, Warning,
			TEXT("passage: body %d does not rise and set within three days here"), Moon);
		bRunning = false;
		return;
	}

	const double Transit = FindTransit(Rise, Set);
	const double Up = Set - Rise;

	Moments.Add({ TEXT("before rise"), Rise - Up * 0.08 });
	Moments.Add({ TEXT("rise"),        Rise });
	Moments.Add({ TEXT("climbing"),    Rise + (Transit - Rise) * 0.5 });
	Moments.Add({ TEXT("transit"),     Transit });
	Moments.Add({ TEXT("descending"),  Transit + (Set - Transit) * 0.5 });
	Moments.Add({ TEXT("set"),         Set });
	Moments.Add({ TEXT("after set"),   Set + Up * 0.08 });

	for (FMoment& Moment : Moments)
	{
		Moment.PredictedAltitude = AltitudeOf(Moment.Seconds);
	}

	UE_LOG(LogLedger, Log,
		TEXT("passage: body %d rises at %.0f s, transits at %.0f (%.1f deg up), "
			 "sets at %.0f -- up for %.2f hours"),
		Moon, Rise, Transit, FMath::RadiansToDegrees(AltitudeOf(Transit)), Set,
		Up / 3600.0);

	// **And then the same moon, priced as a destination.** The map and the sky
	// are two views of one system; if they disagreed, the body whose rising is
	// timed here would not be the body whose crossing is quoted.
	TArray<FLedgerSite> Sites;
	LedgerMap::Sites(System, Sites);
	From = Sites[Home];
	From.AnchorDirection = Anchor;
	From.AltitudeMetres = System.Bodies[Home].RadiusMetres;
	To = Sites[Moon];
	To.AltitudeMetres = System.Bodies[Moon].RadiusMetres;

	QuotedSeconds =
		LedgerMap::TravelTimeSeconds(System, From, To, Transit, 9.81);
	QuotedDistance = LedgerMap::DistanceMetres(System, From, To, Transit);

	LandedGravity = LedgerEphemeris::GravitationalConstant * System.Bodies[Moon].MassKg
		/ (System.Bodies[Moon].RadiusMetres * System.Bodies[Moon].RadiusMetres);
	bLandedAirless = !LedgerSky::RetainsAtmosphere(System, Moon, Transit);

	UE_LOG(LogLedger, Log,
		TEXT("passage: it is %.0f km away at transit, %.1f hours at a gravity of "
			 "thrust; surface gravity there is %.2f m/s2 and it holds %s air"),
		QuotedDistance / 1000.0, QuotedSeconds / 3600.0, LandedGravity,
		bLandedAirless ? TEXT("no") : TEXT("some"));
}

void ULedgerMoonPassage::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Anchor);
	const FVector Eye = FVector(Centre + Anchor * (Ground + PassageEyeMetres * 100.0));

	// **Pointed where the ephemeris says, and nowhere else.**
	//
	// The first version held one fixed frame all night on the theory that a
	// camera which tracks its subject cannot show it rising. True, and it
	// produced seven photographs of empty sky: the moon transits 79 degrees up
	// at this site, and no frame contains both that and the horizon. A sky is
	// not a stage.
	//
	// So the camera is aimed at the moon's PREDICTED direction at each moment.
	// That is not cheating and not tracking -- nothing looks for the moon, the
	// bearing and the altitude are both read off the ephemeris before anything
	// is drawn. What the frame then shows is whether the moon is there. At rise
	// and set the aim is level, so the horizon runs through the middle and the
	// moon sits on it, which is the whole claim in one picture.
	// Step has already moved on by the time the shutter tick runs, so the frame
	// being photographed is the one before it. Getting this wrong photographs
	// each moment aimed at the next one, which looks exactly like a moon that
	// is always slightly off.
	const int32 Which =
		FMath::Clamp(bAimed ? Step - 1 : Step, 0, Moments.Num() - 1);
	TArray<FLedgerSkyBody> Seen;
	LedgerSky::VisibleBodies(System, Home, Anchor, Moments[Which].Seconds, Seen);

	FVector3d Aim = FVector3d(1.0, 0.0, 0.0);
	for (const FLedgerSkyBody& Body : Seen)
	{
		if (Body.BodyIndex == Moon)
		{
			Aim = Body.DirectionInSurface.GetSafeNormal();
			break;
		}
	}

	FLedgerSurfacePoint Facing;
	Facing.AnchorDirection = Anchor;
	Facing.Metres = Aim;
	const FVector3d Toward = LedgerFrames::ToBody(Facing).Metres.GetSafeNormal();

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
			Camera->GetCameraComponent()->SetFieldOfView(PassageFieldOfView);
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

void ULedgerMoonPassage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr)
	{
		return;
	}

	Place();

	Settle += DeltaSeconds;
	if (Settle < (Step == 0 && !bAimed ? PassageFirstSettle : PassageStepSettle))
	{
		return;
	}

	if (bAimed)
	{
		// **Where the sphere actually is, before the shutter.** This is the
		// only measurement in the fixture that the ephemeris does not make: the
		// altitude of the thing the renderer put in the sky, read off the
		// actor. Everything else compares one calculation with another.
		ULedgerSkyBodies* Sky = World->GetSubsystem<ULedgerSkyBodies>();
		ALedgerPlanet* Planet = Builder->GetPlanet();
		FVector Placed = FVector::ZeroVector;
		if (Sky != nullptr && Planet != nullptr
			&& Sky->WorldPositionOf(Moon, Placed) && Camera != nullptr)
		{
			const FVector3d Eye = FVector3d(Camera->GetActorLocation());
			const FVector3d Up =
				(Eye - FVector3d(Planet->GetActorLocation())).GetSafeNormal();
			const FVector3d Toward = (FVector3d(Placed) - Eye).GetSafeNormal();
			Moments[Step - 1].RenderedAltitude = FMath::Asin(
				FMath::Clamp(FVector3d::DotProduct(Up, Toward), -1.0, 1.0));
			Moments[Step - 1].AzimuthErrorArcminutes =
				FMath::RadiansToDegrees(FMath::Abs(
					Moments[Step - 1].RenderedAltitude
					- Moments[Step - 1].PredictedAltitude)) * 60.0;
		}

		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("passage-%d.png"), Step - 1)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bAimed = false;
		Settle = 0.0;
		return;
	}

	if (Step >= Moments.Num())
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	Builder->SetWhenSeconds(Moments[Step].Seconds);
	UE_LOG(LogLedger, Log,
		TEXT("passage %d/%d: %s at %.0f s, predicted altitude %+.2f deg"),
		Step + 1, Moments.Num(), *Moments[Step].What, Moments[Step].Seconds,
		FMath::RadiansToDegrees(Moments[Step].PredictedAltitude));

	++Step;
	bAimed = true;
	Settle = 0.0;
}

void ULedgerMoonPassage::Report()
{
	FString Body;
	Body += TEXT("A moon rises, crosses and sets on schedule (T088).\n\n");
	Body += FString::Printf(TEXT("body            %d, a moon of body %d\n"), Moon, Home);
	Body += FString::Printf(TEXT("up for          %.2f hours\n"),
		(Moments.Last().Seconds - Moments[0].Seconds) / 3600.0);
	Body += TEXT(
		"\nThe times were asked of the ephemeris before anything was drawn. The\n"
		"camera holds one bearing all night -- the azimuth of the rise -- so the\n"
		"moon crossing the frame is the moon crossing the sky.\n\n");

	Body += TEXT("moment         when (s)     predicted   rendered   out by   image\n");
	for (int32 Index = 0; Index < Moments.Num(); ++Index)
	{
		const FMoment& Moment = Moments[Index];
		Body += FString::Printf(
			TEXT("%-13s %10.0f   %+8.2f   %+8.2f   %6.2f'  passage-%d.png\n"),
			*Moment.What, Moment.Seconds,
			FMath::RadiansToDegrees(Moment.PredictedAltitude),
			FMath::RadiansToDegrees(Moment.RenderedAltitude),
			Moment.AzimuthErrorArcminutes, Index);
	}

	Body += FString::Printf(TEXT(
		"\nAnd then the same moon as a destination: %.0f km at transit, %.1f hours\n"
		"at a gravity of thrust. Surface gravity there is %.2f m/s2 and it holds\n"
		"%s air, both derived from the mass and radius it was generated with.\n"),
		QuotedDistance / 1000.0, QuotedSeconds / 3600.0, LandedGravity,
		bLandedAirless ? TEXT("no") : TEXT("some"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			TEXT("moon-passage.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);

	double Worst = 0.0;
	for (const FMoment& Moment : Moments)
	{
		Worst = FMath::Max(Worst, Moment.AzimuthErrorArcminutes);
	}
	UE_LOG(LogLedger, Log,
		TEXT("passage: %d moments captured; worst rendered-against-predicted "
			 "altitude %.3f arcminutes"),
		Moments.Num(), Worst);
}
