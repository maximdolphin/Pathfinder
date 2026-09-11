#include "LedgerCrossing.h"

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
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/// Long enough for auto-exposure to finish, which between a night on one
	/// world and a day on another is most of the range.
	constexpr double CrossingFirstSettle = 25.0;
	constexpr double CrossingStepSettle = 14.0;

	/// **The landing gets longer, because adaptation is not symmetric.**
	/// The world darkens by a stop and a quarter a second and brightens by
	/// eight tenths of one, and the last step is the one that goes from the
	/// unlit side of a descent to sunlight on bare regolith -- about twenty
	/// stops, downward, at the slow rate. Fourteen seconds photographs a frame
	/// still on its way there, which is a white rectangle. Thirty-two arrives.
	constexpr double CrossingLandedSettle = 32.0;

	/// A gravity of thrust, the same ship T087 priced the trip for.
	constexpr double CrossingAcceleration = 9.81;

	struct FCrossingStep
	{
		const TCHAR* What;

		/// Where along the crossing this is, 0 at the home stand-off and 1 at
		/// the moon. Negative means still on the ground at home.
		double Fraction;
	};

	const FCrossingStep CrossingSteps[] =
	{
		{ TEXT("watch"),    -1.0 },
		{ TEXT("departure"), 0.02 },
		{ TEXT("cruise"),    0.50 },
		{ TEXT("arrival"),   1.00 },
		// **A descent, not a teleport.** Dropping the camera from eight radii
		// straight onto the ground asks the streamer to build every level of
		// detail between them in one frame. A stop on the way down is what a
		// landing is anyway.
		//
		// 1.98 and not 1.5, because this number is the clock as well as the
		// altitude. Half a trip is two and three quarter hours, and the moon
		// turns in that time: the descent was photographing a landing site
		// that would not be in daylight for another two hours, so the frame
		// came back black while the one taken from the ground beneath it was
		// lit. Six minutes above the landing is a descent; a night apart from
		// it is a different photograph.
		{ TEXT("descent"),   1.98 },
		{ TEXT("landed"),    2.00 },
	};
}

bool ULedgerCrossing::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerCrossing::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerCrossing, STATGROUP_Tickables);
}

void ULedgerCrossing::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("crossing"));
}

void ULedgerCrossing::Prepare()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr || Builder->GetPlanet() == nullptr)
	{
		return;
	}

	System = Builder->GetSystem();
	Anchor = Builder->GetSiteDirection().GetSafeNormal();
	Home = Builder->GetHomeBodyIndex();
	// `-crossto=` overrides the destination, which is how "does this fail
	// because the far body is airless, or because it is a different body at
	// all" gets answered rather than argued about.
	Moon = LedgerBodies::FirstChildOfKind(System, Home, ELedgerBodyKind::Moon);
	int32 Override = INDEX_NONE;
	if (FParse::Value(FCommandLine::Get(), TEXT("crossto="), Override)
		&& System.Bodies.IsValidIndex(Override) && Override != Home)
	{
		Moon = Override;
	}
	if (Moon == INDEX_NONE)
	{
		UE_LOG(LogLedger, Warning, TEXT("crossing: body %d has no moon"), Home);
		bRunning = false;
		return;
	}

	DepartureSeconds = Builder->GetWhenSeconds();

	// **The trip is quoted before it is flown**, by the same map the Command
	// lens will use. Everything after this is the plan being carried out.
	TArray<FLedgerSite> Sites;
	LedgerMap::Sites(System, Sites);
	From = Sites[Home];
	From.AnchorDirection = Anchor;
	From.AltitudeMetres = System.Bodies[Home].RadiusMetres;
	To = Sites[Moon];
	To.AltitudeMetres = System.Bodies[Moon].RadiusMetres;

	QuotedSeconds = LedgerMap::TravelTimeSeconds(
		System, From, To, DepartureSeconds, CrossingAcceleration);
	QuotedDistance = LedgerMap::DistanceMetres(System, From, To, DepartureSeconds);

	ArrivedGravity = LedgerEphemeris::GravitationalConstant
		* System.Bodies[Moon].MassKg
		/ (System.Bodies[Moon].RadiusMetres * System.Bodies[Moon].RadiusMetres);
	ArrivedRadiusMetres = System.Bodies[Moon].RadiusMetres;
	bArrivedAirless =
		!LedgerSky::RetainsAtmosphere(System, Moon, DepartureSeconds);

	bPrepared = true;
	UE_LOG(LogLedger, Log,
		TEXT("crossing: %s to %s, %.0f km, quoted %.2f hours at a gravity; "
			 "the far end pulls %.2f m/s2 and holds %s air"),
		*System.Bodies[Home].Name, *System.Bodies[Moon].Name,
		QuotedDistance / 1000.0, QuotedSeconds / 3600.0, ArrivedGravity,
		bArrivedAirless ? TEXT("no") : TEXT("some"));
}

void ULedgerCrossing::Place()
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
		bAimed ? Step - 1 : Step, 0, UE_ARRAY_COUNT(CrossingSteps) - 1);
	const FCrossingStep& Now = CrossingSteps[Which];

	// **The clock runs at the rate the trip takes.** T085's point: a crossing
	// is hours of simulated time and the ephemeris does not care how it is
	// asked, so the fixture moves the clock and everything else follows.
	// **The clock stops when the ship arrives.** Fractions past one are
	// altitude, not more travelling: the descent and the landing happen at the
	// far end, minutes apart, not another whole crossing later.
	//
	// It used to multiply straight through, which put the landing five and a
	// half hours after the arrival. The site is chosen for daylight at the
	// moment the world is built, and five and a half hours is most of a night
	// on a small fast moon -- so the fixture picked a sunlit place to land and
	// then photographed it after dark, twice.
	const double Elapsed = FMath::Clamp(Now.Fraction, 0.0, 1.0)
		* FMath::Min(QuotedSeconds, 1.0e6);
	Builder->SetWhenSeconds(DepartureSeconds + Elapsed);

	// **And at the far end the world becomes the moon.** Everything the home
	// body owned is destroyed and rebuilt for the new one, which is the whole
	// reason this fixture exists.
	if (Now.Fraction >= 1.0 && !bSwitched)
	{
		Builder->SwitchToBody(Moon);
		bSwitched = true;

		// **The camera stays.** The first version destroyed it here and let the
		// next tick spawn a replacement, which left the player controller with
		// a destroyed view target for at least one whole frame -- and the
		// renderer dereferenced it. That was the access violation on the render
		// thread whose breadcrumb said nothing more specific than "SceneRender".
		//
		// There is nothing body-specific about a camera anyway: it is a
		// position and a rotation in world space, and Place() sets both every
		// tick.
		return;
	}

	// The switch takes a frame to land. Until the builder has a planet again
	// there is nothing to stand on, and the settle clock should not be running.
	if (bSwitched && Builder->GetHomeBodyIndex() != Moon)
	{
		Settle = 0.0;
		return;
	}

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const FVector3d Up = bSwitched
		? Builder->GetSiteDirection().GetSafeNormal() : Anchor;
	const double Ground = Planet->SurfaceRadiusAt(Up);

	FVector Eye = FVector::ZeroVector;
	FVector3d Toward = FVector3d::UnitX();
	float FieldOfView = 60.0f;

	if (Now.Fraction < 0.0)
	{
		// Standing at home, looking at the moon that is about to be visited.
		Eye = FVector(Centre + Up * (Ground + 4000.0));
		TArray<FLedgerSkyBody> Seen;
		LedgerSky::VisibleBodies(System, Home, Up,
			Builder->GetWhenSeconds(), Seen);
		FVector3d Aim = FVector3d(1.0, 0.0, 0.2);
		for (const FLedgerSkyBody& Body : Seen)
		{
			if (Body.BodyIndex == Moon)
			{
				Aim = Body.DirectionInSurface.GetSafeNormal();
				break;
			}
		}
		Toward = LedgerFrames::ToBody({ Home, Up, Aim }).Metres.GetSafeNormal();
		FieldOfView = 25.0f;
	}
	else if (Now.Fraction <= 1.0)
	{
		// **Out along the route, looking back.** The ship climbs away from the
		// stand-off on the line the map quoted; the planet shrinking behind it
		// is the crossing happening.
		//
		// In radii of *whichever body is underneath*, which is not always the
		// home one: the last of these steps is taken after the switch, above a
		// moon a third the size. Measuring its distance in home radii put the
		// camera four times too far out and photographed the arrival as a
		// two-pixel crescent -- a picture of nothing, of the right thing.
		const int32 Under = Builder->GetHomeBodyIndex();
		const double UnderRadius = System.Bodies.IsValidIndex(Under)
			? System.Bodies[Under].RadiusMetres
			: System.Bodies[Home].RadiusMetres;
		// Eight radii at the far end rather than forty. The stand-off used to
		// be chosen so the whole disc cleared the frame; what it actually did
		// was make every one of these pictures a dot on a black field, which
		// is a photograph of the right thing that shows nothing.
		const double Height = UnderRadius * (0.2 + 8.0 * Now.Fraction);
		Eye = FVector(Centre + Up * (Ground + Height * 100.0));
		Toward = -Up;
		FieldOfView = 55.0f;
	}
	else if (Now.Fraction < 2.0)
	{
		// On the way down: high enough that the whole disc is still a horizon,
		// low enough that the streamer has seen the ground it is about to need.
		//
		// Tilted away from the sun for the same reason the landing is. Two
		// hundred kilometres up over an airless world, with the sun ahead and
		// low, every slope in the frame is presenting its unlit face and the
		// photograph is black -- which is what this step returned until it was
		// turned around.
		Eye = FVector(Centre + Up * (Ground + 2.0e7));
		const FVector3d Sun = LedgerSky::SunDirectionInSurface(
			System, Moon, Up, Builder->GetWhenSeconds());
		const FVector2D DescentAway = FVector2D(-Sun.X, -Sun.Y).GetSafeNormal();
		Toward = LedgerFrames::ToBody(
			{ Moon, Up, FVector3d(DescentAway.X, DescentAway.Y, -1.2) })
			.Metres.GetSafeNormal();
		FieldOfView = 65.0f;
	}
	else
	{
		// **Landed, with the sun behind the camera and the ground in front.**
		//
		// Two things about a world with no air, both learned by photographing
		// the wrong one. A small body's horizon is close and flat -- at thirty
		// metres up on a moon of two thousand kilometres it is eleven
		// kilometres away and a third of a degree below level -- so anything
		// aimed level or above is mostly sky. And there is no sky light: a
		// slope facing away from the sun receives nothing at all, so a camera
		// pointed *at* the sun photographs the unlit side of everything and
		// returns a black frame with a few rocks in it.
		//
		// So: away from the sun, tilted down. That is where the lit faces are,
		// and it is also the direction anyone standing on an airless surface
		// would look if they wanted to see where they were.
		Eye = FVector(Centre + Up * (Ground + 3000.0));
		const FVector3d Sun = LedgerSky::SunDirectionInSurface(
			System, Moon, Up, Builder->GetWhenSeconds());
		const FVector2D Away =
			FVector2D(-Sun.X, -Sun.Y).GetSafeNormal();
		Toward = LedgerFrames::ToBody(
			{ Moon, Up, FVector3d(Away.X, Away.Y, -0.18) }).Metres.GetSafeNormal();
		FieldOfView = 70.0f;
	}

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye);
	}

	const FRotator Look =
		FRotationMatrix::MakeFromXZ(FVector(Toward), FVector(Up)).Rotator();
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
		Camera->GetCameraComponent()->SetFieldOfView(FieldOfView);
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

void ULedgerCrossing::Tick(float DeltaSeconds)
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
	if (!bPrepared)
	{
		Prepare();
		return;
	}

	Place();

	Settle += DeltaSeconds;
	const bool bLanding = bAimed && Step == UE_ARRAY_COUNT(CrossingSteps);
	const double Wanted = Step == 0 && !bAimed
		? CrossingFirstSettle
		: (bLanding ? CrossingLandedSettle : CrossingStepSettle);
	if (Settle < Wanted)
	{
		return;
	}

	if (bAimed)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("crossing-%d-%s.png"),
					Step - 1, CrossingSteps[Step - 1].What)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bAimed = false;
		Settle = 0.0;
		return;
	}

	if (Step >= UE_ARRAY_COUNT(CrossingSteps))
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	// **Where the sun is, said out loud at every step.** Three of these frames
	// came back black in a row and each one was argued about as a streaming
	// problem, a camera-aim problem and an exposure problem before anybody
	// asked the only question that mattered: whether the ground being
	// photographed was in daylight. It costs one line to never ask again.
	if (ULedgerWorldBuilder* Where = GetWorld()->GetSubsystem<ULedgerWorldBuilder>())
	{
		const int32 Under = Where->GetHomeBodyIndex();
		const FVector3d Local = bSwitched
			? Where->GetSiteDirection().GetSafeNormal() : Anchor;
		UE_LOG(LogLedger, Log,
			TEXT("crossing: on body %d the sun is %.1f degrees above the "
				 "local horizon"),
			Under, FMath::RadiansToDegrees(LedgerSky::SolarAltitude(
				System, Under, Local, Where->GetWhenSeconds())));
	}

	UE_LOG(LogLedger, Log, TEXT("crossing %d/%d: %s"),
		Step + 1, static_cast<int32>(UE_ARRAY_COUNT(CrossingSteps)),
		CrossingSteps[Step].What);
	++Step;
	bAimed = true;
	Settle = 0.0;
}

void ULedgerCrossing::Report()
{
	ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>();

	FString Body;
	Body += FString::Printf(TEXT("A moon, watched and then landed on (T088).%s%s"),
		LINE_TERMINATOR, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("from             %s%s"),
		*System.Bodies[Home].Name, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("to               %s%s"),
		*System.Bodies[Moon].Name, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("distance         %.0f km%s"),
		QuotedDistance / 1000.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("quoted           %.2f hours at a gravity%s"),
		QuotedSeconds / 3600.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("world switched   %s%s"),
		bSwitched ? TEXT("yes") : TEXT("NO"), LINE_TERMINATOR);
	Body += FString::Printf(TEXT("standing on      body %d, radius %.0f km%s"),
		Builder != nullptr ? Builder->GetHomeBodyIndex() : -1,
		ArrivedRadiusMetres / 1000.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("gravity there    %.2f m/s2 (%.3f g)%s"),
		ArrivedGravity, ArrivedGravity / 9.81, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("atmosphere       %s%s"),
		bArrivedAirless ? TEXT("none") : TEXT("some"), LINE_TERMINATOR);

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			TEXT("crossing.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log,
		TEXT("crossing: finished on body %d, %s the world switch"),
		Builder != nullptr ? Builder->GetHomeBodyIndex() : -1,
		bSwitched ? TEXT("after") : TEXT("WITHOUT"));
}
