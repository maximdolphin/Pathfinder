#include "LedgerWindProbe.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "LedgerAudio.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerPrecipitationView.h"
#include "LedgerSettlement.h"
#include "LedgerShip.h"
#include "LedgerWeather.h"
#include "LedgerWind.h"
#include "LedgerWorld.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/// Long enough for the world to finish streaming around the pad, for the
	/// ship to come to rest on it, and for every consumer to have ticked many
	/// times at the first moment.
	constexpr double ProbeSettleSeconds = 20.0;

	/// How long to wait for agreement before calling it a failure. Five times
	/// the acceptance, so a slow consumer is measured rather than cut off.
	constexpr double ProbeGiveUpSeconds = 5.0;

	constexpr double ProbeCentimetresPerMetre = 100.0;
	constexpr double ProbeEyeMetres = 25.0;

	/// How far the old reading has to be from the new field, in tolerances,
	/// for agreement afterwards to mean anything. A consumer that ignored the
	/// change entirely would still be sitting on its old reading, and this is
	/// what stops that from passing.
	constexpr double ProbeDistinctTolerances = 3.0;
}

bool ULedgerWindProbe::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerWindProbe::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerWindProbe, STATGROUP_Tickables);
}

const TCHAR* ULedgerWindProbe::ConsumerName(int32 Index)
{
	static const TCHAR* Names[Consumers] =
	{
		TEXT("flight"), TEXT("vegetation"), TEXT("particles"),
		TEXT("audio"), TEXT("settlement"),
	};
	return Names[FMath::Clamp(Index, 0, Consumers - 1)];
}

void ULedgerWindProbe::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("windprobe"));
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
	BeforeSeconds = Builder->GetWhenSeconds();
	Air = LedgerAir::For(System, Home, BeforeSeconds);

	// **The biggest change in the wind over the next three days** -- as a
	// vector, not as a turn. The first version picked the largest turn and
	// found twenty-six degrees, which it rightly refused to count; but a turn
	// is the wrong measure anyway, because what makes a stale consumer
	// detectable is how far the new wind is from the old one, and a gust that
	// halves in speed without turning is as good a test as a veer.
	const double Latitude = FMath::Asin(FMath::Clamp(Anchor.Z, -1.0, 1.0));
	const double Longitude = FMath::Atan2(Anchor.Y, Anchor.X);
	const FVector2D Start = LedgerWeather::WindAt(
		System, Home, Air, Latitude, Longitude, BeforeSeconds);
	AfterSeconds = BeforeSeconds + 3600.0;
	double Largest = -1.0;
	for (int32 Hour = 1; Hour <= 72; ++Hour)
	{
		const double At = BeforeSeconds + Hour * 3600.0;
		const FVector2D Then = LedgerWeather::WindAt(
			System, Home, Air, Latitude, Longitude, At);
		const double Change = (Then - Start).Size();
		if (Change > Largest)
		{
			Largest = Change;
			AfterSeconds = At;
			TurnDegrees = (Start.Size() > 0.1 && Then.Size() > 0.1)
				? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
					FVector2D::DotProduct(Start.GetSafeNormal(), Then.GetSafeNormal()),
					-1.0, 1.0)))
				: 0.0;
		}
	}

	UE_LOG(LogLedger, Log,
		TEXT("wind probe: switching from t=%.0f s to t=%.0f s, where the free wind "
			 "at the site has changed by %.1f m/s and turned %.0f degrees"),
		BeforeSeconds, AfterSeconds, Largest, TurnDegrees);
}

void ULedgerWindProbe::Read(FReading (&Out)[Consumers]) const
{
	UWorld* World = GetWorld();
	const ULedgerWind* Wind = World->GetSubsystem<ULedgerWind>();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	const APlayerController* Controller = World->GetFirstPlayerController();
	if (Wind == nullptr || Builder == nullptr || Controller == nullptr)
	{
		return;
	}

	FVector Eye = FVector::ZeroVector;
	FRotator Ignored = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(Eye, Ignored);

	// Flight: what the ship's drag was last computed against, where it is.
	if (const ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Out[0].Consumer = Ship->LastWindCmPerSecond() / ProbeCentimetresPerMetre;
		Out[0].Field = Wind->WindAtMetres(Ship->GetActorLocation());
		Out[0].bAvailable = true;
	}

	// Vegetation: the parameter collection the foliage material reads, which
	// the wind subsystem publishes for the viewer.
	if (UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(
		nullptr, TEXT("/Game/Materials/MPC_LedgerWind")))
	{
		if (UMaterialParameterCollectionInstance* Instance =
			World->GetParameterCollectionInstance(Collection))
		{
			FLinearColor Direction = FLinearColor::Black;
			float Speed = 0.0f;
			if (Instance->GetVectorParameterValue(TEXT("WindDirection"), Direction)
				&& Instance->GetScalarParameterValue(TEXT("WindSpeed"), Speed))
			{
				Out[1].Consumer = FVector3d(Direction.R, Direction.G, Direction.B) * Speed;
				Out[1].Field = Wind->WindAtMetres(Eye);
				Out[1].bAvailable = true;
			}
		}
	}

	// Particles: what the rain view last moved its drops by, at the eye.
	if (const ULedgerPrecipitationView* View = World->GetSubsystem<ULedgerPrecipitationView>())
	{
		Out[2].Consumer = View->LastWindMetres();
		Out[2].Field = Wind->WindAtMetres(Eye);
		Out[2].bAvailable = true;
	}

	// Audio: a speed rather than a vector, because loudness has no direction.
	if (const ULedgerAudio* Audio = World->GetSubsystem<ULedgerAudio>())
	{
		Out[3].Consumer = FVector3d(Audio->Heard().SpeedMetresPerSecond, 0.0, 0.0);
		Out[3].Field = FVector3d(Wind->SpeedAt(Eye), 0.0, 0.0);
		Out[3].bAvailable = true;
		Out[3].bSpeedOnly = true;
	}

	// Settlement: the vane, where it stands.
	if (const ALedgerSettlement* Town = Builder->GetSettlement())
	{
		Out[4].Consumer = Town->VaneWindMetres();
		Out[4].Field = Wind->WindAtMetres(Town->VaneLocation());
		Out[4].bAvailable = true;
	}
}

bool ULedgerWindProbe::Agrees(const FReading& Reading)
{
	// Within five per cent of the field or two tenths of a metre a second,
	// whichever is looser: tight enough that a consumer still holding the old
	// wind cannot pass, loose enough that one which read the field a frame
	// earlier, a few metres away, does.
	const double Tolerance = FMath::Max(0.05 * Reading.Field.Length(), 0.2);
	return Reading.bAvailable
		&& (Reading.Consumer - Reading.Field).Length() <= Tolerance;
}

void ULedgerWindProbe::Tick(float DeltaSeconds)
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
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	// The view: at the pad, above the town, level.
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Anchor);
	const FVector Eye = FVector(
		Centre + Anchor * (Ground + ProbeEyeMetres * ProbeCentimetresPerMetre));
	const FVector3d Toward = LedgerFrames::ToBody(
		{ Home, Anchor, FVector3d(1.0, 0.0, -0.1) }).Metres.GetSafeNormal();
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

		// **The ship to the pad, once, and then left to fly.** It spawns two
		// radii out over the lit side, where the weather is a long way away
		// and a change at the town barely reaches it -- the first version
		// measured its reading moving by less than a metre a second and would
		// have passed a ship that ignored the wind. On the pad it is in the
		// same air as everything else, and its own physics keeps reading it.
		if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
		{
			if (const ALedgerSettlement* Town = Builder->GetSettlement())
			{
				Ship->SetVelocity(FVector::ZeroVector);
				Ship->SetActorLocation(Town->GetPadLocation());
			}
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}

	if (Phase == 0)
	{
		Builder->SetWhenSeconds(BeforeSeconds);
		Settle += DeltaSeconds;
		if (Settle < ProbeSettleSeconds)
		{
			return;
		}
		Read(Before);

		// **The change.** One call, the same one a player's clock would make.
		Builder->SetWhenSeconds(AfterSeconds);
		SwitchedAt = FPlatformTime::Seconds();
		FramesSinceSwitch = 0;
		Phase = 1;
		UE_LOG(LogLedger, Log, TEXT("wind probe: the wind changed"));
		return;
	}

	if (Phase == 1)
	{
		++FramesSinceSwitch;
		const double Elapsed = FPlatformTime::Seconds() - SwitchedAt;
		FReading Now[Consumers];
		Read(Now);

		bool bAll = true;
		for (int32 Index = 0; Index < Consumers; ++Index)
		{
			if (Latency[Index] < 0.0 && Agrees(Now[Index]))
			{
				Latency[Index] = Elapsed;
				LatencyFrames[Index] = FramesSinceSwitch;
				After[Index] = Now[Index];
				UE_LOG(LogLedger, Log,
					TEXT("wind probe: %s agreed after %.3f s (%d frames)"),
					ConsumerName(Index), Elapsed, FramesSinceSwitch);
			}
			bAll = bAll && Latency[Index] >= 0.0;
			if (Latency[Index] < 0.0)
			{
				After[Index] = Now[Index];
			}
		}

		if (bAll || Elapsed > ProbeGiveUpSeconds)
		{
			bRunning = false;
			Report();
			FPlatformMisc::RequestExit(false);
		}
	}
}

void ULedgerWindProbe::Report()
{
	TArray<FString> Lines;
	Lines.Add(TEXT("Five consumers, one wind change (T093)."));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("before           t=%.0f s"), BeforeSeconds));
	Lines.Add(FString::Printf(TEXT("after            t=%.0f s (the free wind at the site turned %.0f degrees)"),
		AfterSeconds, TurnDegrees));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("  consumer      before m/s        after m/s         field m/s          moved   agreed after"));

	auto Vector = [](const FVector3d& Value, bool bSpeedOnly)
	{
		return bSpeedOnly
			? FString::Printf(TEXT("%5.1f (speed)"), Value.X)
			: FString::Printf(TEXT("%5.1f %5.1f %5.1f"), Value.X, Value.Y, Value.Z);
	};

	// **Two things per consumer, and both have to hold.** It agreed with the
	// new field within a second -- and its reading from before the change was
	// far enough from that field that agreeing could not have happened by
	// standing still. The second is what the first version was missing.
	bool bPass = true;
	for (int32 Index = 0; Index < Consumers; ++Index)
	{
		const FReading& Was = Before[Index];
		const FReading& Is = After[Index];
		const double Tolerance = FMath::Max(0.05 * Is.Field.Length(), 0.2);
		const double Moved = (Is.Field - Was.Consumer).Length();
		const bool bDistinct = Moved >= ProbeDistinctTolerances * Tolerance;
		const bool bInTime = Latency[Index] >= 0.0 && Latency[Index] <= 1.0;
		bPass = bPass && bDistinct && bInTime;

		FString Agreed = TEXT("NEVER");
		if (!Is.bAvailable && !Was.bAvailable)
		{
			Agreed = TEXT("NOT PRESENT");
		}
		else if (Latency[Index] >= 0.0)
		{
			Agreed = FString::Printf(TEXT("%.3f s, %d frames"), Latency[Index], LatencyFrames[Index]);
		}

		Lines.Add(FString::Printf(TEXT("  %-12s %-17s %-17s %-17s %5.1f%s %s"),
			ConsumerName(Index),
			*Vector(Was.Consumer, Was.bSpeedOnly),
			*Vector(Is.Consumer, Is.bSpeedOnly),
			*Vector(Is.Field, Is.bSpeedOnly),
			Moved, bDistinct ? TEXT("  ") : TEXT("! "),
			*Agreed));
	}

	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(
		TEXT("every old reading was at least %.0f tolerances from the new field "
			 "(a '!' marks one that was not)"), ProbeDistinctTolerances));
	Lines.Add(FString::Printf(TEXT("all five followed the change within a second: %s"),
		bPass ? TEXT("yes") : TEXT("NO")));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("wind-probe.txt")));
	FFileHelper::SaveStringArrayToFile(Lines, *Path);
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("%s"), *Line);
	}
}
