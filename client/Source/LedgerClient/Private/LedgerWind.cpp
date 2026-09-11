#include "LedgerWind.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerAir.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerSky.h"
#include "LedgerWeather.h"
#include "LedgerWorld.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace
{
	constexpr double WindCentimetresPerMetre = 100.0;

	/// How far either side the ground's slope is measured, metres. Finer than
	/// a ridge and coarser than a boulder.
	constexpr double WindSlopeHalfMetres = 150.0;

	/// How deep the air a slope pushes around is, metres: the forced lift
	/// falls off by e over this above the ground. About a ridge's height.
	constexpr double WindTerrainDepthMetres = 1000.0;

	/// Thermals: columns this far apart over sunlit ground, this strong with
	/// the sun overhead, and gone above the top of the mixed layer.
	constexpr double WindThermalSpacingMetres = 1500.0;
	constexpr double WindThermalMetresPerSecond = 2.5;
	constexpr double WindThermalTopMetres = 1500.0;
	/// The collection every material reads the wind from. One asset, four
	/// numbers, and the grass and the dust cannot disagree because there is
	/// nothing for them to disagree about.
	const TCHAR* WindCollectionPath = TEXT("/Game/Materials/MPC_LedgerWind");

	/// **A test control, not a setting.** Multiplies the wind everywhere, for
	/// every consumer at once, because it is applied at the one place the wind
	/// comes from. It exists so a fixture can change the wind *without* moving
	/// the clock: T058's canopy photographs first changed the wind by jumping
	/// seventy-two hours, and the sun moved so far between the two frames that
	/// the lighting, not the leaves, was what differed.
	TAutoConsoleVariable<float> CVarLedgerWindScale(
		TEXT("Ledger.Wind.Scale"), 1.0f,
		TEXT("Multiplies the wind everywhere. 1 is the weather; 0 is calm. A test control."),
		ECVF_Default);
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
	const FVector3d EastDirection = East.Metres.GetSafeNormal();
	const FVector3d NorthDirection = North.Metres.GetSafeNormal();

	// **T098: the ground and the sun move the air up and down.** Air blowing
	// against a slope has to go over it, so the flow at the ground is forced
	// along the surface -- w = u . grad h -- rising on the windward face and
	// sinking in the lee, and dying away above the terrain. A ship crossing a
	// ridge in a gale is lifted and then dropped by the same field that bends
	// the grass, not by a number drawn for the occasion.
	auto GroundMetres = [Planet, &Up](const FVector3d& Along, double Metres)
	{
		return Planet->SurfaceRadiusAt(
			(Up + Along * (Metres * WindCentimetresPerMetre / Planet->Radius)).GetSafeNormal())
			/ WindCentimetresPerMetre;
	};
	const double SlopeEast = (GroundMetres(EastDirection, WindSlopeHalfMetres)
		- GroundMetres(EastDirection, -WindSlopeHalfMetres)) / (2.0 * WindSlopeHalfMetres);
	const double SlopeNorth = (GroundMetres(NorthDirection, WindSlopeHalfMetres)
		- GroundMetres(NorthDirection, -WindSlopeHalfMetres)) / (2.0 * WindSlopeHalfMetres);
	const double Forced = (Flat.X * SlopeEast + Flat.Y * SlopeNorth)
		* FMath::Exp(-AltitudeMetres / WindTerrainDepthMetres);

	// Thermals: sunlit ground warms the air over it and the air rises in
	// columns, as strongly as the sun is high, up to the top of the mixed
	// layer. ponytail: a fixed grid of columns that do not drift or sink back
	// between them; a thermal that moves with the wind when a glider needs to
	// find one twice.
	double Thermal = 0.0;
	const double Sun = LedgerSky::SolarAltitude(System, Home, Up, Builder->GetWhenSeconds());
	if (Sun > 0.0 && AltitudeMetres < WindThermalTopMetres)
	{
		const double BodyRadius = System.Bodies[Home].RadiusMetres;
		const double X = Longitude * BodyRadius * FMath::Cos(Latitude) / WindThermalSpacingMetres;
		const double Y = Latitude * BodyRadius / WindThermalSpacingMetres;
		Thermal = WindThermalMetresPerSecond * FMath::Sin(Sun)
			* FMath::Max(FMath::Cos(LedgerTwoPi * X) * FMath::Cos(LedgerTwoPi * Y), 0.0)
			* (1.0 - AltitudeMetres / WindThermalTopMetres);
	}

	return (EastDirection * Flat.X + NorthDirection * Flat.Y + Up * (Forced + Thermal))
		* static_cast<double>(CVarLedgerWindScale.GetValueOnGameThread());
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
	// The horizontal part: a cloud deck drifts and grass leans with the wind
	// across the ground, and a ridge lifting the viewer is not that.
	const FVector3d Across = LastViewerWind - LastViewerUp * FVector3d::DotProduct(LastViewerWind, LastViewerUp);
	const FVector3d Direction = Across.GetSafeNormal();
	Instance->SetVectorParameterValue(TEXT("WindDirection"),
		FLinearColor(
			static_cast<float>(Direction.X),
			static_cast<float>(Direction.Y),
			static_cast<float>(Direction.Z), 0.0f));
	Instance->SetScalarParameterValue(TEXT("WindSpeed"),
		static_cast<float>(Across.Length()));
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
	if (const ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>())
	{
		if (const ALedgerPlanet* Planet = Builder->GetPlanet())
		{
			LastViewerUp = (FVector3d(Eye) - FVector3d(Planet->GetActorLocation())).GetSafeNormal();
		}
	}
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
