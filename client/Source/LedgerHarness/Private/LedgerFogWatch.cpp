#include "LedgerFogWatch.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerFrames.h"
#include "LedgerGroundFog.h"
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
	constexpr double FogWatchFirstSettle = 25.0;
	constexpr double FogWatchStepSettle = 6.0;
	/// Eye height, metres. **Twenty rather than eight**, because the terrain query
	/// and the terrain the streamer actually builds are not the same surface at
	/// every level of detail, and a camera eight metres above a coarse estimate
	/// of a mountainside can be inside a fine one. A buried camera renders black
	/// and looks exactly like a lighting bug, which cost a round of chasing one.
	/// Sixty cleared the terrain and put the camera above most of an
	/// eighty-metre cold pool, which is not standing in fog either.
	/// **Different heights, for different reasons.**
	///
	/// In the valley the camera has to be inside the cold pool, which is eighty
	/// metres deep -- twenty is standing in it. On the ridge it has to clear the
	/// difference between the coarse terrain query and the terrain the streamer
	/// builds, which on a peak is tens of metres; twenty buried it and rendered
	/// black, which looks exactly like a lighting bug and cost a round of
	/// chasing one.
	constexpr double FogWatchValleyEyeMetres = 20.0;
	constexpr double FogWatchRidgeEyeMetres = 60.0;

	/// How far around the site the ground is sampled for a low point and a high
	/// one. The same extent the fog is laid over, so the valley the camera
	/// stands in is a valley the fog reaches.
	constexpr double FogWatchExtentMetres = 12000.0;
	constexpr int32 FogWatchSamples = 40;

	const TCHAR* FogWatchViews[] = { TEXT("valley"), TEXT("ridge"), TEXT("orbit") };
}

bool ULedgerFogWatch::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerFogWatch::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerFogWatch, STATGROUP_Tickables);
}

void ULedgerFogWatch::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("fogwatch"));
}

void ULedgerFogWatch::Prepare()
{
	// **Everything here waits for the first tick, and that is not fussiness.**
	//
	// World subsystems are initialised in no guaranteed order, so reading the
	// site direction in OnWorldBeginPlay can read it before the world builder
	// has chosen one. This fixture did, got a default straight-up anchor,
	// computed a sunrise for a place that does not exist, and photographed
	// three black frames while the log insisted the sun was three degrees up.
	// The log was right about its anchor and its anchor was wrong.
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr || Builder->GetPlanet() == nullptr)
	{
		return;
	}

	System = Builder->GetSystem();
	Anchor = Builder->GetSiteDirection().GetSafeNormal();
	Home = Builder->GetHomeBodyIndex();
	Air = LedgerAir::For(System, Home, Builder->GetWhenSeconds());

	// **Dawn is asked for, and so is the length of the night before it.** How
	// much fog there is depends on how long the ground has been radiating, so
	// the fixture cannot pick a time -- it has to find sunset, find the sunrise
	// after it, and take the gap.
	const double Start = Builder->GetWhenSeconds();
	const double Day = LedgerSky::SolarDaySeconds(System, Home, Anchor, Start);
	if (!(Day > 0.0))
	{
		bRunning = false;
		return;
	}

	double Sunset = -1.0;
	double Previous = LedgerSky::SolarAltitude(System, Home, Anchor, Start);
	for (int32 Sample = 1; Sample <= 4000; ++Sample)
	{
		const double At = Start + Day * 2.0 * Sample / 4000.0;
		const double Now = LedgerSky::SolarAltitude(System, Home, Anchor, At);
		if (Sunset < 0.0 && Previous > 0.0 && Now <= 0.0)
		{
			Sunset = At;
		}
		else if (Sunset > 0.0 && Previous <= 0.0 && Now > 0.0)
		{
			DawnSeconds = At;
			NightSeconds = At - Sunset;
			break;
		}
		Previous = Now;
	}

	if (DawnSeconds < 0.0)
	{
		UE_LOG(LogLedger, Warning, TEXT("fog watch: no sunrise found at this site"));
		bRunning = false;
		return;
	}

	// **Just after sunrise, not at it.** The first light that rakes along the
	// ground is what makes a fog bank look like one, and three degrees up is
	// still 87% of the fog by the burn-off curve.
	for (int32 Sample = 0; Sample < 2000; ++Sample)
	{
		const double At = DawnSeconds + Sample * Day / 2000.0;
		if (LedgerSky::SolarAltitude(System, Home, Anchor, At)
			> FMath::DegreesToRadians(3.0))
		{
			DawnSeconds = At;
			break;
		}
	}

	Weather = LedgerFog::At(Air, NightSeconds,
		LedgerSky::SolarAltitude(System, Home, Anchor, DawnSeconds));
	bPrepared = true;

	UE_LOG(LogLedger, Log,
		TEXT("fog watch: %.2f hours of darkness cooled the ground by %.1f K; "
			 "fog %s, %.0f m deep, visibility %.0f m"),
		NightSeconds / 3600.0, LedgerFog::NightCoolingKelvin(Air, NightSeconds),
		Weather.bForms ? TEXT("forms") : TEXT("does not form"),
		Weather.DepthMetres,
		3.0 / FMath::Max(Weather.ExtinctionPerMetre * Weather.Fraction, 1e-9));
}

void ULedgerFogWatch::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	// **The clock is set every tick, not once at begin play.**
	// Setting it in OnWorldBeginPlay produced three black frames: the world
	// builder finishes its own setup afterwards and puts the sun back where it
	// started, so the fixture was photographing midnight while insisting it was
	// dawn. Every other fixture here re-asserts the time each tick for the same
	// reason.
	Builder->SetWhenSeconds(DawnSeconds);

	if (!bPlaced)
	{
		// Find the lowest and highest ground within the fog's reach. The camera
		// stands on each, which is the only way the two frames are of the same
		// weather at the same moment rather than of two different places.
		FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Anchor);
		if (East.IsNearlyZero())
		{
			East = FVector3d::CrossProduct(FVector3d::UnitX(), Anchor);
		}
		East.Normalize();
		const FVector3d North = FVector3d::CrossProduct(Anchor, East).GetSafeNormal();
		const double RadiusMetres = Planet->Radius / 100.0;

		// **The tenth percentile, not the minimum.**
		//
		// The single lowest sample is the bottom of the deepest crack, and it is
		// where the coarse terrain query and the streamed terrain disagree by
		// the most -- a camera placed there renders black because it is inside
		// a mountainside. It is also not what anyone means by a valley. A tenth
		// of the way up the distribution is low ground with room to stand on.
		TArray<TPair<double, FVector3d>> Sampled;
		Sampled.Reserve(FogWatchSamples * FogWatchSamples);
		RidgeMetres = -TNumericLimits<double>::Max();
		for (int32 Y = 0; Y < FogWatchSamples; ++Y)
		{
			for (int32 X = 0; X < FogWatchSamples; ++X)
			{
				const double Across = (X - (FogWatchSamples - 1) * 0.5)
					* FogWatchExtentMetres / FogWatchSamples;
				const double Along = (Y - (FogWatchSamples - 1) * 0.5)
					* FogWatchExtentMetres / FogWatchSamples;
				const FVector3d Direction =
					(Anchor * RadiusMetres + East * Across + North * Along)
					.GetSafeNormal();
				const double Surface = Planet->SurfaceRadiusAt(Direction) / 100.0;
				Sampled.Add({ Surface, Direction });
				if (Surface > RidgeMetres)
				{
					RidgeMetres = Surface;
					RidgeDirection = Direction;
				}
			}
		}

		// `-fogoff` lays no fog and changes nothing else, which is the A/B this
		// feature is judged by: the same site, the same instant, the same
		// camera, with and without.
		Sampled.Sort([](const TPair<double, FVector3d>& A,
			const TPair<double, FVector3d>& B) { return A.Key < B.Key; });
		const int32 Low = FMath::Clamp(Sampled.Num() / 10, 0, Sampled.Num() - 1);
		ValleyMetres = Sampled[Low].Key;
		ValleyDirection = Sampled[Low].Value;

		if (Weather.bForms && !FParse::Param(FCommandLine::Get(), TEXT("fogoff")))
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			Fog = World->SpawnActor<ALedgerGroundFog>(
				ALedgerGroundFog::StaticClass(), FVector::ZeroVector,
				FRotator::ZeroRotator, Params);
			if (Fog != nullptr)
			{
				CellsFilled = Fog->Configure(Planet, Anchor, Weather);
			}
		}
		UE_LOG(LogLedger, Log,
			TEXT("fog watch: valley %.0f m, ridge %.0f m, %.0f m of relief; "
				 "fog filled to %.0f m so the ridge is %.0f m clear"),
			ValleyMetres - Planet->Radius / 100.0,
			RidgeMetres - Planet->Radius / 100.0,
			RidgeMetres - ValleyMetres,
			(Fog != nullptr ? Fog->TopMetres() : ValleyMetres)
				- Planet->Radius / 100.0,
			RidgeMetres - (Fog != nullptr ? Fog->TopMetres() : ValleyMetres));
		bPlaced = true;
	}

	const int32 Which = FMath::Clamp(
		bAimed ? Step - 1 : Step, 0, UE_ARRAY_COUNT(FogWatchViews) - 1);
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());

	// Look towards the low ground, which is where the fog is, and towards the
	// sun's side so the light rakes through it.
	const FVector3d Sun = LedgerSky::SunDirectionInSurface(
		System, Home, Anchor, Builder->GetWhenSeconds());

	FVector Eye = FVector::ZeroVector;
	FVector3d Up = Anchor;
	FVector3d Toward = FVector3d::UnitX();
	float FieldOfView = 70.0f;

	if (Which == 0)
	{
		Up = ValleyDirection;
		Eye = FVector(Centre + ValleyDirection
			* ((ValleyMetres + FogWatchValleyEyeMetres) * 100.0));
		UE_LOG(LogLedger, Log,
			TEXT("fog watch: valley eye at %.0f m, ground under it %.0f m"),
			ValleyMetres + FogWatchValleyEyeMetres,
			Planet->SurfaceRadiusAt(ValleyDirection) / 100.0);
		Toward = LedgerFrames::ToBody(
			{ Home, ValleyDirection, FVector3d(Sun.X, Sun.Y, 0.06) }).Metres
			.GetSafeNormal();
	}
	else if (Which == 1)
	{
		Up = RidgeDirection;

		// **Can the ridge see the valley at all?** Measured, not assumed.
		//
		// This view was recorded as proving that fog volumes do not render at
		// twelve kilometres: the frame was "indistinguishable from the no-fog
		// control". It was also a shot from the top of a rounded ridge down at a
		// valley three kilometres lower, and a rounded ridge hides whatever is
		// below its own shoulder. So the line from the eye to the fog's top over
		// the valley is traced against the terrain, and the eye is raised until
		// nothing stands in it -- the ridge is still where the eye is, only
		// higher above it, which is what a lookout is.
		const FVector3d Target = Centre + ValleyDirection
			* ((Fog != nullptr ? Fog->TopMetres() : ValleyMetres + 50.0) * 100.0);
		static double LookoutMetres = -1.0;   // ponytail: one world per process
		if (LookoutMetres < 0.0)
		{
			for (int32 Raise = 0; Raise <= 16; ++Raise)
			{
				const double Height = FogWatchRidgeEyeMetres + Raise * 250.0;
				const FVector3d From = Centre + RidgeDirection
					* ((RidgeMetres + Height) * 100.0);
				double WorstAbove = -TNumericLimits<double>::Max();
				double WorstAt = 0.0;
				constexpr int32 Samples = 400;
				// The last few per cent are the valley floor itself, which the
				// line is meant to reach, so they are not an obstruction.
				for (int32 Index = 1; Index < Samples * 97 / 100; ++Index)
				{
					const double T = static_cast<double>(Index) / Samples;
					const FVector3d Point = From + (Target - From) * T;
					const FVector3d Offset = Point - Centre;
					const double Above = Planet->SurfaceRadiusAt(Offset.GetSafeNormal())
						- Offset.Length();
					if (Above > WorstAbove)
					{
						WorstAbove = Above;
						WorstAt = T * (Target - From).Length();
					}
				}
				UE_LOG(LogLedger, Log,
					TEXT("fog watch: from %.0f m above the ridge the line to the "
						 "valley %s -- terrain peaks %.0f m %s it, %.1f km out"),
					Height, WorstAbove > 0.0 ? TEXT("is BLOCKED") : TEXT("is clear"),
					FMath::Abs(WorstAbove) / 100.0,
					WorstAbove > 0.0 ? TEXT("above") : TEXT("below"),
					WorstAt / 100000.0);
				if (WorstAbove <= 0.0 || Raise == 16)
				{
					LookoutMetres = Height;
					UE_LOG(LogLedger, Log,
						TEXT("fog watch: ridge eye at %.0f m, %.0f m above the ridge's ground"),
						RidgeMetres + LookoutMetres, LookoutMetres);
					break;
				}
			}
		}

		Eye = FVector(Centre + RidgeDirection * ((RidgeMetres + LookoutMetres) * 100.0));

		// Straight at the fog's top over the valley, so the frame is about the
		// fog and not about how far down a fixed slope happens to land.
		Toward = (Target - FVector3d(Eye)).GetSafeNormal();
	}
	else
	{
		const double Height = Planet->Radius * 2.0;
		Eye = FVector(Centre + Anchor
			* (Planet->SurfaceRadiusAt(Anchor) + Height));
		const double ToLimb = FMath::Asin(1.0 / 3.0);
		const double Down = -FMath::Tan(
			LedgerPi * 0.5 - ToLimb - FMath::DegreesToRadians(3.0));
		Toward = LedgerFrames::ToBody(
			{ Home, Anchor, FVector3d(Sun.X, Sun.Y, Down) }).Metres.GetSafeNormal();
		FieldOfView = 50.0f;
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

void ULedgerFogWatch::Tick(float DeltaSeconds)
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
	if (Settle < (Step == 0 && !bAimed ? FogWatchFirstSettle : FogWatchStepSettle))
	{
		return;
	}

	if (bAimed)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("fogwatch-%s.png"), FogWatchViews[Step - 1])));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bAimed = false;
		Settle = 0.0;
		return;
	}

	if (Step >= UE_ARRAY_COUNT(FogWatchViews))
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	UE_LOG(LogLedger, Log,
		TEXT("fog watch %d/%d: %s, t=%.0f s, sun %.2f deg up, fog %.0f%%"),
		Step + 1, static_cast<int32>(UE_ARRAY_COUNT(FogWatchViews)),
		FogWatchViews[Step], DawnSeconds,
		FMath::RadiansToDegrees(LedgerSky::SolarAltitude(
			System, Home, Anchor, DawnSeconds)),
		Weather.Fraction * 100.0);
	++Step;
	bAimed = true;
	Settle = 0.0;
}

void ULedgerFogWatch::Report()
{
	ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	const double Datum = Planet != nullptr ? Planet->Radius / 100.0 : 0.0;

	FString Body;
	Body += FString::Printf(TEXT("Fog with a top (T091).%s%s"),
		LINE_TERMINATOR, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("night            %.2f hours%s"),
		NightSeconds / 3600.0, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("ground cooled    %.1f K%s"),
		LedgerFog::NightCoolingKelvin(Air, NightSeconds), LINE_TERMINATOR);
	Body += FString::Printf(TEXT("dew point drop   %.1f K%s"),
		LedgerFog::DewPointDepressionKelvin(), LINE_TERMINATOR);
	Body += FString::Printf(TEXT("cold pool        %.0f m deep%s"),
		Weather.DepthMetres, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("visibility       %.0f m%s"),
		3.0 / FMath::Max(Weather.ExtinctionPerMetre * Weather.Fraction, 1e-9),
		LINE_TERMINATOR);
	Body += FString::Printf(TEXT("valley floor     %.0f m%s"),
		ValleyMetres - Datum, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("fog top          %.0f m%s"),
		(Fog != nullptr ? Fog->TopMetres() : 0.0) - Datum, LINE_TERMINATOR);
	Body += FString::Printf(TEXT("ridge            %.0f m, %.0f m above the fog%s"),
		RidgeMetres - Datum,
		RidgeMetres - (Fog != nullptr ? Fog->TopMetres() : RidgeMetres),
		LINE_TERMINATOR);
	Body += FString::Printf(TEXT("cells filled     %d%s"),
		CellsFilled, LINE_TERMINATOR);

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			TEXT("fog-watch.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("fog watch: wrote %s"), *Path);
}
