#include "LedgerPrecipitationView.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "LedgerAir.h"
#include "LedgerClimate.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerSurface.h"
#include "LedgerWind.h"
#include "LedgerWorld.h"
#include "Materials/MaterialInterface.h"

namespace
{
	constexpr double PrecipCentimetresPerMetre = 100.0;

	/// Half the side of the box the particles live in, centimetres. Twenty
	/// metres each way: far enough that the near edge is out of frame and close
	/// enough that ten thousand drops is a downpour rather than a drizzle.
	constexpr double PrecipHalfBoxCm = 2000.0;

	/// The most particles that will ever be drawn, at the heaviest rate.
	constexpr int32 PrecipMaxDrops = 9000;

	/// How long a drop is smeared over, seconds. A streak is what a fall speed
	/// looks like through an exposure, so the length is the speed times this --
	/// nine metres a second over a twenty-fifth of a second is a thirty-six
	/// centimetre streak, which is what rain looks like in a photograph.
	constexpr double PrecipStreakSeconds = 0.04;

	/// Millimetres an hour that counts as the heaviest rendered.
	constexpr double PrecipFullScaleRate = 6.0;

	/// Wrap a coordinate into [-Half, Half) about a centre. **Modulo, not a
	/// respawn**: a drop that leaves the top arrives at the bottom with its
	/// horizontal position intact, so a gust moves the whole curtain rather
	/// than scattering it.
	double PrecipWrap(double Value, double Centre, double Half)
	{
		const double Span = Half * 2.0;
		double Offset = FMath::Fmod(Value - Centre + Half, Span);
		if (Offset < 0.0)
		{
			Offset += Span;
		}
		return Centre - Half + Offset;
	}
}

bool ULedgerPrecipitationView::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerPrecipitationView::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerPrecipitationView, STATGROUP_Tickables);
}

void ULedgerPrecipitationView::Rebuild(int32 Wanted)
{
	if (Drops == nullptr || Wanted == Drawn)
	{
		return;
	}

	Drops->ClearInstances();
	Positions.Reset(Wanted);
	Drawn = Wanted;
	if (Wanted <= 0)
	{
		return;
	}

	// Scattered once, then advected for ever. A fresh random position every
	// frame would be static on the screen, which is the classic way to make
	// rain look like film grain.
	FRandomStream Stream(20260911);
	for (int32 Index = 0; Index < Wanted; ++Index)
	{
		Positions.Add(FVector3d(
			Stream.FRandRange(-PrecipHalfBoxCm, PrecipHalfBoxCm),
			Stream.FRandRange(-PrecipHalfBoxCm, PrecipHalfBoxCm),
			Stream.FRandRange(-PrecipHalfBoxCm, PrecipHalfBoxCm)));
		Drops->AddInstance(FTransform::Identity, false);
	}
}

void ULedgerPrecipitationView::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	const APlayerController* Controller =
		World != nullptr ? World->GetFirstPlayerController() : nullptr;
	ULedgerWorldBuilder* Builder =
		World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	const ULedgerWind* Wind =
		World != nullptr ? World->GetSubsystem<ULedgerWind>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Controller == nullptr || Builder == nullptr || Wind == nullptr
		|| Planet == nullptr)
	{
		return;
	}

	FVector Eye = FVector::ZeroVector;
	FRotator Ignored = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(Eye, Ignored);

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const FVector3d Offset = FVector3d(Eye) - Centre;
	const double DistanceCm = Offset.Length();
	if (!(DistanceCm > 0.0))
	{
		return;
	}
	const FVector3d Up = Offset / DistanceCm;
	const double Altitude = FMath::Max(
		(DistanceCm - Planet->SurfaceRadiusAt(Up)) / PrecipCentimetresPerMetre, 0.0);

	const FLedgerSystem& System = Builder->GetSystem();
	const int32 Home = Builder->GetHomeBodyIndex();
	const FLedgerAirProfile Air =
		LedgerAir::For(System, Home, Builder->GetWhenSeconds());

	const double Latitude = FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0));
	const double Longitude = FMath::Atan2(Up.Y, Up.X);

	// **The surface temperature where the viewer is, not the planet's datum.**
	// A pole and an equator share an atmosphere and not a freezing level, and
	// the latitude model is the terrain's, so it is fetched rather than
	// reinvented. This is the difference between snow at the ground here and
	// snow at the ground everywhere.
	const FLedgerClimate Climate = LedgerClimate::At(
		Up, Planet->TerrainParams(), Planet->SeasonPhase());
	const double SurfaceKelvin = Climate.TemperatureC + 273.15;

	Last = LedgerPrecip::At(System, Home, Air, Latitude, Longitude,
		Altitude, Builder->GetWhenSeconds(), SurfaceKelvin);

	const int32 Wanted = Last.IsFalling()
		? FMath::RoundToInt(PrecipMaxDrops * FMath::Min(
			Last.RateMillimetresPerHour / PrecipFullScaleRate, 1.0))
		: 0;

	if (Wanted > 0 && Drops == nullptr)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Holder = World->SpawnActor<AActor>(
			AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Holder == nullptr)
		{
			return;
		}
		Holder->SetRootComponent(
			NewObject<USceneComponent>(Holder, TEXT("PrecipitationRoot")));
		Holder->GetRootComponent()->RegisterComponent();

		UStaticMesh* Cube = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		// The star material, and not by accident: unlit emissive with a colour
		// and a brightness per instance is exactly what a drop wants, and a
		// second material that did the same thing would be a second thing to
		// keep in step with the exposure.
		UMaterialInterface* Material = LedgerSurface::CreateStarMaterial(Holder);
		if (Cube == nullptr || Material == nullptr)
		{
			UE_LOG(LogLedger, Warning,
				TEXT("precipitation: no mesh or no material, so nothing falls"));
			return;
		}

		Drops = NewObject<UInstancedStaticMeshComponent>(Holder);
		Drops->SetStaticMesh(Cube);
		Drops->SetMaterial(0, Material);
		Drops->SetupAttachment(Holder->GetRootComponent());
		Drops->RegisterComponent();
		Drops->SetCastShadow(false);
		Drops->bReceivesDecals = false;
		Drops->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Drops->bAffectDynamicIndirectLighting = false;
		Drops->bAffectDistanceFieldLighting = false;
		Drops->bVisibleInRealTimeSkyCaptures = false;
		Drops->NumCustomDataFloats = 4;
	}

	if (Drops == nullptr)
	{
		return;
	}

	if (Wanted != Drawn || Last.Kind != DrawnKind)
	{
		Rebuild(Wanted);
		DrawnKind = Last.Kind;
	}
	if (Drawn <= 0)
	{
		return;
	}

	// Rain is a grey that is nearly white against a dark sky and nearly dark
	// against a bright one, which an unlit emissive cannot do -- so it is given
	// the luminance of an overcast sky, a few thousand nits, and left there.
	// Snow is the same colour and much brighter, because a flake is a diffuse
	// reflector the size of a fingernail and a drop is a lens.
	const float Brightness = Last.Kind == ELedgerPrecipitation::Snow ? 1.2f : 0.5f;
	const FLinearColor Tint = Last.Kind == ELedgerPrecipitation::Dust
		? FLinearColor(0.65f, 0.50f, 0.35f)
		: FLinearColor(0.80f, 0.85f, 0.92f);

	// One wind query for the whole box. The box is forty metres across and the
	// wind field's shortest length scale is a storm, so asking per particle
	// would be ten thousand identical answers.
	const FVector3d Air3 = FVector3d(Wind->WindAt(Eye));
	const FVector3d Fall =
		-Up * (Last.FallSpeedMetresPerSecond * PrecipCentimetresPerMetre);
	const FVector3d Velocity = Air3 + Fall;

	const double Length = FMath::Max(
		Velocity.Length() * PrecipStreakSeconds, 2.0);
	const FVector3d Along = Velocity.GetSafeNormal();
	const FQuat4d Rotation = FRotationMatrix::MakeFromZ(FVector(Along)).ToQuat();

	// A cube is a hundred centimetres on a side, so the scale is the size in
	// centimetres over a hundred. Snow is a flake rather than a streak.
	const double Across = Last.Kind == ELedgerPrecipitation::Rain ? 1.2 : 3.0;
	const FVector3d Scale = FVector3d(
		Across / 100.0, Across / 100.0,
		Last.Kind == ELedgerPrecipitation::Rain ? Length / 100.0 : Across / 100.0);

	TArray<FTransform> Transforms;
	Transforms.Reserve(Drawn);
	for (int32 Index = 0; Index < Drawn; ++Index)
	{
		FVector3d& Where = Positions[Index];
		Where += Velocity * DeltaSeconds;
		Where.X = PrecipWrap(Where.X, Eye.X, PrecipHalfBoxCm);
		Where.Y = PrecipWrap(Where.Y, Eye.Y, PrecipHalfBoxCm);
		Where.Z = PrecipWrap(Where.Z, Eye.Z, PrecipHalfBoxCm);
		Transforms.Add(FTransform(
			FRotator(Rotation), FVector(Where), FVector(Scale)));
	}
	Drops->BatchUpdateInstancesTransforms(0, Transforms, true, true, false);

	for (int32 Index = 0; Index < Drawn; ++Index)
	{
		Drops->SetCustomDataValue(Index, 0, Brightness, false);
		Drops->SetCustomDataValue(Index, 1, Tint.R, false);
		Drops->SetCustomDataValue(Index, 2, Tint.G, false);
		Drops->SetCustomDataValue(Index, 3, Tint.B, Index == Drawn - 1);
	}

	SinceLog += DeltaSeconds;
	if (SinceLog > 5.0)
	{
		SinceLog = 0.0;
		UE_LOG(LogLedger, Log,
			TEXT("precipitation: %s at %.2f mm/h, %d drops, freezing level "
				 "%.0f m, falling %.1f m/s through %.1f m/s of wind"),
			LexToString(Last.Kind), Last.RateMillimetresPerHour, Drawn,
			Last.FreezingLevelMetres, Last.FallSpeedMetresPerSecond,
			Air3.Length() / PrecipCentimetresPerMetre);
	}
}
