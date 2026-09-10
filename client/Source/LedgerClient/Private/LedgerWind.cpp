#include "LedgerWind.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerAir.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerWeather.h"
#include "LedgerWorld.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace
{
	constexpr double WindCentimetresPerMetre = 100.0;

	/// The collection every material reads the wind from. One asset, four
	/// numbers, and the grass and the dust cannot disagree because there is
	/// nothing for them to disagree about.
	const TCHAR* WindCollectionPath = TEXT("/Game/Materials/MPC_LedgerWind");
}

bool ULedgerWind::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerWind::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerWind, STATGROUP_Tickables);
}

FVector3d ULedgerWind::WindAtMetres(const FVector& WorldPosition) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return FVector3d::ZeroVector;
	}
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Builder == nullptr || Planet == nullptr)
	{
		return FVector3d::ZeroVector;
	}

	const FLedgerSystem& System = Builder->GetSystem();
	const int32 Home = Builder->GetHomeBodyIndex();
	if (!System.Bodies.IsValidIndex(Home))
	{
		return FVector3d::ZeroVector;
	}

	const FLedgerAirProfile Air =
		LedgerAir::For(System, Home, Builder->GetWhenSeconds());
	if (!Air.HasAir())
	{
		// No air, no wind. An airless moon should not rustle.
		return FVector3d::ZeroVector;
	}

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const FVector3d Offset = FVector3d(WorldPosition) - Centre;
	const double DistanceCm = Offset.Length();
	if (!(DistanceCm > 0.0))
	{
		return FVector3d::ZeroVector;
	}
	const FVector3d Up = Offset / DistanceCm;

	// **Latitude and longitude in the body's own frame**, because that is the
	// frame the weather is computed in and the frame the ground is fixed in.
	// Reading them off the world position directly would be reading them in a
	// frame that turns.
	const double Latitude = FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0));
	const double Longitude = FMath::Atan2(Up.Y, Up.X);

	const double GroundCm = Planet->SurfaceRadiusAt(Up);
	const double AltitudeMetres =
		FMath::Max((DistanceCm - GroundCm) / WindCentimetresPerMetre, 0.0);

	const FVector2D Flat = LedgerWeather::WindAtAltitude(
		System, Home, Air, Latitude, Longitude, AltitudeMetres,
		Builder->GetWhenSeconds());

	// East, north and up at this place, turned into the world frame the same
	// way every other surface quantity in this project is.
	const FLedgerBodyPoint East =
		LedgerFrames::ToBody({ Home, Up, FVector3d(1.0, 0.0, 0.0) });
	const FLedgerBodyPoint North =
		LedgerFrames::ToBody({ Home, Up, FVector3d(0.0, 1.0, 0.0) });
	return East.Metres.GetSafeNormal() * Flat.X
		+ North.Metres.GetSafeNormal() * Flat.Y;
}

FVector ULedgerWind::WindAt(const FVector& WorldPosition) const
{
	return FVector(WindAtMetres(WorldPosition) * WindCentimetresPerMetre);
}

double ULedgerWind::SpeedAt(const FVector& WorldPosition) const
{
	return WindAtMetres(WorldPosition).Length();
}

void ULedgerWind::Publish()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (Collection == nullptr && !bLookedForCollection)
	{
		bLookedForCollection = true;
		Collection = LoadObject<UMaterialParameterCollection>(
			nullptr, WindCollectionPath);
		if (Collection == nullptr)
		{
			// **Not an error, and not silent either.** A clone without the
			// content pack has no collection asset; the flight model and
			// everything else that asks this subsystem directly still gets the
			// right answer, and only the materials go still. Saying so once is
			// better than a warning every frame or a mystery.
			UE_LOG(LogLedger, Log,
				TEXT("wind: no %s, so materials will not read the wind; "
					 "every other consumer is unaffected"),
				WindCollectionPath);
		}
	}
	if (Collection == nullptr)
	{
		return;
	}

	UMaterialParameterCollectionInstance* Instance =
		World->GetParameterCollectionInstance(Collection);
	if (Instance == nullptr)
	{
		return;
	}

	// Direction as a world vector and speed separately, because a material that
	// wants to bend grass needs the direction and one that wants to pick a
	// rustle needs the speed, and normalising in a shader is a waste.
	const FVector3d Direction = LastViewerWind.GetSafeNormal();
	Instance->SetVectorParameterValue(TEXT("WindDirection"),
		FLinearColor(
			static_cast<float>(Direction.X),
			static_cast<float>(Direction.Y),
			static_cast<float>(Direction.Z), 0.0f));
	Instance->SetScalarParameterValue(TEXT("WindSpeed"),
		static_cast<float>(LastViewerWind.Length()));
}

void ULedgerWind::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	const APlayerController* Controller =
		World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr)
	{
		return;
	}

	FVector Eye = FVector::ZeroVector;
	FRotator Ignored = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(Eye, Ignored);

	// **Every tick, unconditionally.** The acceptance is that every consumer
	// sees the same change within a second, and the only way to promise that is
	// for there to be one number, refreshed on a schedule nobody has to
	// coordinate with. A threshold on how much it had changed would be a second
	// thing to get wrong for no measured saving.
	LastViewerWind = WindAtMetres(Eye);
	Publish();

	const double Speed = LastViewerWind.Length();
	if (LastPublishedSpeed < 0.0 || FMath::Abs(Speed - LastPublishedSpeed) > 2.0)
	{
		UE_LOG(LogLedger, Log,
			TEXT("wind: %.1f m/s at the viewer, bearing %.0f degrees"),
			Speed, FMath::RadiansToDegrees(
				FMath::Atan2(LastViewerWind.Y, LastViewerWind.X)));
		LastPublishedSpeed = Speed;
	}
}
