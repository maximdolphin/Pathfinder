#include "LedgerHorizon.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerTerrainMath.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/// The distance the acceptance names.
	constexpr double RangeKilometres = 80.0;

	/// Long enough for ground eighty kilometres out to have streamed in. It is
	/// coarse ground, so this is about the streamer reaching that far rather
	/// than about how much there is.
	constexpr double HorizonSettleSeconds = 25.0;

	/// Eye height for the shot. High enough that the peak is not hidden by the
	/// curve, low enough that this is a view from the ground rather than from
	/// an aircraft: at 80 km the horizon drop is about 500 m, so standing at
	/// 600 puts the base of the range roughly on the horizon line, which is
	/// where a silhouette is a silhouette.
	constexpr double ViewMetres = 600.0;

	FVector3d HorizonOnSphere(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat)).GetSafeNormal();
	}
}

bool ULedgerHorizon::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerHorizon::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerHorizon, STATGROUP_Tickables);
}

void ULedgerHorizon::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("horizon"));
}

void ULedgerHorizon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	if (!bFound)
	{
		if (!FindRange())
		{
			bRunning = false;
			FPlatformMisc::RequestExit(false);
			return;
		}
		bFound = true;
	}

	Place();

	Settle += DeltaSeconds;
	if (Settle < HorizonSettleSeconds)
	{
		return;
	}

	if (!bCaptured)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				TEXT("horizon-80km.png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bCaptured = true;
		return;
	}

	bRunning = false;
	Report();
	FPlatformMisc::RequestExit(false);
}

bool ULedgerHorizon::FindRange()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr
		? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Planet == nullptr)
	{
		return false;
	}

	const FLedgerTerrainParams Params = Planet->TerrainParams();
	const FVector3d SunDirection = Builder->GetSunFacing().GetSafeNormal();

	// The highest lit land on the planet. Lit, because every fixture in this
	// repository that forgot photographed the night side at least once.
	double Best = 0.0;
	for (double Latitude = -80.0; Latitude <= 80.0; Latitude += 0.5)
	{
		for (double Longitude = 0.0; Longitude < 360.0; Longitude += 0.5)
		{
			const FVector3d Point = HorizonOnSphere(Latitude, Longitude);
			if (FVector3d::DotProduct(Point, SunDirection) <= 0.35)
			{
				continue;
			}
			const double Height = LedgerTerrain::Elevation(Point, Params) / 100.0;
			if (Height > Best)
			{
				Best = Height;
				Peak = Point;
			}
		}
	}

	if (Best <= 0.0)
	{
		UE_LOG(LogLedger, Error, TEXT("horizon: no lit land found"));
		return false;
	}
	PeakMetres = Best;

	// Eighty kilometres away, along whatever tangent points at the sun -- so
	// the range is lit from the side rather than flat-on, which is the light
	// a silhouette reads best in.
	FVector3d Away = FVector3d::CrossProduct(Peak, SunDirection);
	if (Away.IsNearlyZero())
	{
		Away = FVector3d::CrossProduct(Peak, FVector3d(0.0, 0.0, 1.0));
	}
	Away.Normalize();

	const double Arc = (RangeKilometres * 1000.0) / (Planet->Radius / 100.0);
	Viewpoint = (Peak * FMath::Cos(Arc) + Away * FMath::Sin(Arc)).GetSafeNormal();

	UE_LOG(LogLedger, Log,
		TEXT("horizon: highest lit land %.0f m, viewpoint %.0f km away"),
		PeakMetres, RangeKilometres);
	return true;
}

void ULedgerHorizon::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder->GetPlanet();
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	const FVector3d Origin = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Viewpoint);

	// The ship is the streaming anchor and stands where the camera does, so the
	// streamer builds around the viewpoint rather than around the town.
	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(FVector(Origin + Viewpoint * (Ground + ViewMetres * 100.0)));
	}

	const FVector Eye = FVector(Origin + Viewpoint * (Ground + ViewMetres * 100.0));
	const FVector Target = FVector(Origin + Peak * Planet->SurfaceRadiusAt(Peak));
	const FRotator Look =
		FRotationMatrix::MakeFromXZ(Target - Eye, FVector(Viewpoint)).Rotator();

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
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

void ULedgerHorizon::Report()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder->GetPlanet();

	// How much of the peak stands above the horizon at this distance, which is
	// the arithmetic the whole task turns on. The drop is R(1 - cos(arc)).
	const double RadiusMetres = Planet->Radius / 100.0;
	const double Arc = (RangeKilometres * 1000.0) / RadiusMetres;
	const double Drop = RadiusMetres * (1.0 - FMath::Cos(Arc));
	const double Proud = PeakMetres - Drop;

	// And what that subtends, which is what "reads as terrain" has to beat.
	const double Radians = Proud / (RangeKilometres * 1000.0);
	const double Pixels = Radians * (1920.0 / (2.0 * FMath::Tan(FMath::DegreesToRadians(45.0))));

	FString Body;
	Body += TEXT("A mountain range eighty kilometres away (T059).\n\n");
	Body += FString::Printf(TEXT("highest lit land          %8.0f m\n"), PeakMetres);
	Body += FString::Printf(TEXT("horizon drop at %.0f km    %8.0f m\n"), RangeKilometres, Drop);
	Body += FString::Printf(TEXT("standing proud of it      %8.0f m  %s\n"),
		Proud, Proud > 0.0 ? TEXT("") : TEXT("BELOW THE HORIZON -- nothing to photograph"));
	Body += FString::Printf(TEXT("subtends                  %8.0f px at 1920 wide, 90 degrees\n\n"),
		Pixels);

	Body += TEXT("The verdict on whether it reads as terrain rather than as a gradient\n");
	Body += TEXT("is in horizon-80km.png and is a person's. What is decided here is\n");
	Body += TEXT("whether there is anything there to look at, which on the planet this\n");
	Body += TEXT("task was blocked against there was not: 1,372 m of land against a\n");
	Body += TEXT("500 m horizon drop is a bump on the curve.\n\n");
	Body += FString::Printf(TEXT("VERDICT: %s\n"),
		Pixels >= 20.0 ? TEXT("PASS -- there is a silhouette") : TEXT("FAIL"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("horizon.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("horizon -> %s (%.0f px of silhouette)"), *Path, Pixels);
}
