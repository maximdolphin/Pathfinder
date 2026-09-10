#include "LedgerSkyBodies.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "LedgerSurface.h"
#include "LedgerStarField.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerSky.h"
#include "LedgerWorld.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/// How far out the sky shell sits, centimetres. 500 km: well clear of the
	/// ground the flight reaches, and short enough to stay inside a depth range
	/// that behaves.
	constexpr double SkyShellCm = 5.0e7;

	/// The engine's unit sphere is 100 uu across, so radius 50.
	constexpr double EngineSphereRadiusCm = 50.0;

	/// Anything smaller than this across is a star, not a disc, and is left to
	/// the star field. About a fifth of what a sharp eye resolves.
	constexpr double SmallestInterestingRadians = 5.0e-5;
}

bool ULedgerSkyBodies::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerSkyBodies::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerSkyBodies, STATGROUP_Tickables);
}

void ULedgerSkyBodies::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
}

bool ULedgerSkyBodies::WorldPositionOf(int32 BodyIndex, FVector& Out) const
{
	const int32 Which = Bodies.IndexOfByKey(BodyIndex);
	if (Which == INDEX_NONE || !Spheres.IsValidIndex(Which)
		|| Spheres[Which] == nullptr)
	{
		return false;
	}
	Out = Spheres[Which]->GetComponentLocation();
	return true;
}

void ULedgerSkyBodies::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	if (Builder == nullptr)
	{
		return;
	}
	const FLedgerSystem& System = Builder->GetSystem();
	const int32 Home = Builder->GetHomeBodyIndex();
	if (!System.Bodies.IsValidIndex(Home))
	{
		return;
	}

	APlayerController* Controller = World->GetFirstPlayerController();
	if (Controller == nullptr)
	{
		return;
	}
	FVector Eye = FVector::ZeroVector;
	FRotator Ignored = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(Eye, Ignored);

	// Where the viewer is on the planet, as a direction from its centre. The
	// sky bodies are placed for THIS place, not for the town: on a planet this
	// size, a moon's altitude differs measurably between two sites.
	ALedgerPlanet* Planet = Builder->GetPlanet();
	const FVector3d Centre = Planet != nullptr
		? FVector3d(Planet->GetActorLocation()) : FVector3d::ZeroVector;
	const FVector3d Anchor = (FVector3d(Eye) - Centre).GetSafeNormal();
	if (Anchor.IsNearlyZero())
	{
		return;
	}

	TArray<FLedgerSkyBody> Seen;
	LedgerSky::VisibleBodies(System, Home, Anchor, Builder->GetWhenSeconds(), Seen);

	if (!bBuilt)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		if (Sphere == nullptr)
		{
			UE_LOG(LogLedger, Warning, TEXT("sky bodies: no engine sphere; nothing drawn"));
			bBuilt = true;
			return;
		}

		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Holder = World->SpawnActor<AActor>(AActor::StaticClass(), Params);
		if (Holder == nullptr)
		{
			bBuilt = true;
			return;
		}
		Holder->SetRootComponent(
			NewObject<USceneComponent>(Holder, TEXT("SkyBodiesRoot")));
		Holder->GetRootComponent()->RegisterComponent();

		for (const FLedgerSkyBody& Body : Seen)
		{
			// The star is the atmosphere's business -- it already draws a sun
			// disc, and a second one would be two suns half a degree apart.
			if (Body.BodyIndex == 0)
			{
				continue;
			}

			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Holder);
			Component->SetStaticMesh(Sphere);
			Component->SetupAttachment(Holder->GetRootComponent());
			Component->RegisterComponent();
			Component->SetMobility(EComponentMobility::Movable);

			// It hangs in the sky: it must not shadow the ground, and it must
			// not be shadowed by a mountain 500 km behind it. Its own eclipses
			// are T075's, and they are geometry rather than a shadow map.
			Component->SetCastShadow(false);
			Component->bReceivesDecals = false;
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			// A moon lights the ground it hangs over -- that is real, and T075
			// computes it -- but not by being fed to a cubemap capture as a
			// lump of geometry a hundred kilometres away.
			Component->bVisibleInRealTimeSkyCaptures = false;

			Spheres.Add(Component);
			Bodies.Add(Body.BodyIndex);
		}

		BuildStars(System, Home);

		UE_LOG(LogLedger, Log, TEXT("sky bodies: %d spheres for %d visible bodies"),
			Spheres.Num(), Seen.Num());
		for (const FLedgerSkyBody& Body : Seen)
		{
			UE_LOG(LogLedger, Log,
				TEXT("  body %d: %.4f deg across, %.1f%% lit, altitude %+.1f deg, "
					 "azimuth %+.1f deg"),
				Body.BodyIndex, FMath::RadiansToDegrees(Body.AngularRadiusRadians * 2.0),
				Body.IlluminatedFraction * 100.0,
				FMath::RadiansToDegrees(FMath::Asin(
					FMath::Clamp(Body.DirectionInSurface.Z, -1.0, 1.0))),
				FMath::RadiansToDegrees(FMath::Atan2(
					Body.DirectionInSurface.X, Body.DirectionInSurface.Y)));
		}
		if (System.Bodies.IsValidIndex(2))
		{
			UE_LOG(LogLedger, Log, TEXT("  moon month %.0f s"),
				FMath::Abs(System.Bodies[2].RotationPeriodSeconds));
		}
		bBuilt = true;
	}

	for (int32 Slot = 0; Slot < Spheres.Num(); ++Slot)
	{
		UStaticMeshComponent* Component = Spheres[Slot];
		if (Component == nullptr)
		{
			continue;
		}

		const FLedgerSkyBody* Body = Seen.FindByPredicate(
			[Index = Bodies[Slot]](const FLedgerSkyBody& B) { return B.BodyIndex == Index; });
		if (Body == nullptr || Body->AngularRadiusRadians < SmallestInterestingRadians)
		{
			Component->SetVisibility(false);
			continue;
		}

		// The direction comes back in east-north-up at the viewer's anchor, and
		// has to go back out through the same two frames it came in by, or a
		// moon due east appears due north.
		FLedgerSurfacePoint Local;
		Local.BodyIndex = Home;
		Local.AnchorDirection = Anchor;
		Local.Metres = Body->DirectionInSurface;
		const FVector3d InBody = LedgerFrames::ToBody(Local).Metres;

		// And stops there. The world's terrain is the body frame laid straight
		// onto world space -- the planet actor does not turn -- so a body-frame
		// direction already is a world direction. Rotating it into the system
		// frame as well would spin the sky by the planet's rotation twice.
		const FVector Place = Eye + FVector(InBody.GetSafeNormal() * SkyShellCm);

		// Scaled until it subtends the angle it really subtends. tan, not the
		// angle: at half a degree the difference is a part in 100,000, and at a
		// moon seen from low orbit it is not.
		const double WantedRadiusCm = SkyShellCm * FMath::Tan(Body->AngularRadiusRadians);
		const double Scale = WantedRadiusCm / EngineSphereRadiusCm;

		Component->SetVisibility(true);
		Component->SetWorldLocation(Place);
		Component->SetWorldScale3D(FVector(Scale));
	}

	// **The star field turns because the ground does.**
	//
	// The catalogue is in an inertial frame and the terrain is the body frame
	// laid onto world space, so the sky's rotation is the body's orientation
	// applied to the whole field. Rotating the holder does it for eight hundred
	// stars at the cost of one transform; rotating the instances would do it
	// eight hundred times a frame for the same picture.
	if (StarHolder != nullptr && System.Bodies.IsValidIndex(Home))
	{
		const FQuat4d Orientation = LedgerFrames::BodyOrientation(
			System.Bodies[Home], Builder->GetWhenSeconds());
		StarHolder->SetWorldLocationAndRotation(
			Eye, FQuat(Orientation.Inverse()));
	}
}

void ULedgerSkyBodies::BuildStars(const FLedgerSystem& System, int32 Home)
{
	if (bStarsBuilt || Holder == nullptr)
	{
		return;
	}
	bStarsBuilt = true;

	UStaticMesh* Sphere = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Material = LedgerSurface::CreateStarMaterial(Holder);
	if (Sphere == nullptr || Material == nullptr)
	{
		UE_LOG(LogLedger, Warning,
			TEXT("stars: no mesh or no material, so the night sky is empty"));
		return;
	}

	TArray<FLedgerCatalogueStar> Catalogue;
	LedgerStarField::Generate(System.Seed, StarCount, StarRadiusParsecs, Catalogue);

	TArray<FLedgerSkyStar> Seen;
	LedgerStarField::Visible(Catalogue, FVector3d::ZeroVector,
		LedgerStarField::NakedEyeMagnitude, Seen);
	if (Seen.Num() == 0)
	{
		return;
	}

	StarHolder = NewObject<USceneComponent>(Holder, TEXT("StarHolder"));
	StarHolder->SetupAttachment(Holder->GetRootComponent());
	StarHolder->RegisterComponent();

	Stars = NewObject<UInstancedStaticMeshComponent>(Holder);
	Stars->SetStaticMesh(Sphere);
	Stars->SetMaterial(0, Material);
	Stars->SetupAttachment(StarHolder);
	Stars->RegisterComponent();
	Stars->SetCastShadow(false);
	Stars->bReceivesDecals = false;
	Stars->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// **And it must not light anything.**
	//
	// The sky light captures the scene in real time from the viewer, and five
	// and a half thousand emissive spheres at six thousand nits is an enormous
	// amount of light to hand it -- the first night sky with a full catalogue
	// in it came back pure white, because the ambient had been computed from
	// the stars. Starlight is real and it is about a millilux; it is not
	// something a cubemap capture should be inferring.
	Stars->bVisibleInRealTimeSkyCaptures = false;
	Stars->bAffectDynamicIndirectLighting = false;
	Stars->bAffectDistanceFieldLighting = false;
	// Brightness and colour, four floats an instance.
	Stars->NumCustomDataFloats = 4;

	// A star is a point source and would be sub-pixel at any honest size, so
	// every star renderer ever written draws it bigger than it is. What stays
	// honest is the *ordering*: a brighter star is drawn brighter and bigger on
	// the same magnitude scale the eye uses, so the constellations keep the
	// shape the catalogue gave them.
	for (const FLedgerSkyStar& Star : Seen)
	{
		const double Brightness = FMath::Clamp(
			(LedgerStarField::NakedEyeMagnitude - Star.ApparentMagnitude) / 6.0,
			0.03, 1.0);
		const double RadiusCm = SkyShellCm
			* FMath::Tan(FMath::DegreesToRadians(0.010 + 0.045 * Brightness));

		FTransform Where;
		Where.SetLocation(FVector(Star.Direction.GetSafeNormal() * SkyShellCm));
		Where.SetScale3D(FVector(RadiusCm / EngineSphereRadiusCm));
		const int32 Instance = Stars->AddInstance(Where, false);

		// **Colour is temperature, roughly.** A blackbody's visible colour runs
		// from orange at three thousand kelvin through white near six to blue
		// past nine; this is a two-segment lerp through those, which is an
		// approximation and is enough to tell a red giant from a hot young star
		// in a sky. T077 already computed the temperatures.
		const double Kelvin = FMath::Clamp(Star.TemperatureKelvin, 2500.0, 12000.0);
		const FLinearColor Cool(1.00f, 0.72f, 0.45f);
		const FLinearColor Middle(1.00f, 0.97f, 0.93f);
		const FLinearColor Hot(0.72f, 0.80f, 1.00f);
		const FLinearColor Colour = Kelvin < 5800.0
			? FMath::Lerp(Cool, Middle,
				static_cast<float>((Kelvin - 2500.0) / 3300.0))
			: FMath::Lerp(Middle, Hot,
				static_cast<float>((Kelvin - 5800.0) / 6200.0));

		Stars->SetCustomDataValue(Instance, 0, static_cast<float>(Brightness), false);
		Stars->SetCustomDataValue(Instance, 1, Colour.R, false);
		Stars->SetCustomDataValue(Instance, 2, Colour.G, false);
		Stars->SetCustomDataValue(Instance, 3, Colour.B, false);
	}
	Stars->MarkRenderStateDirty();

	UE_LOG(LogLedger, Log,
		TEXT("stars: %d of %d catalogue entries are naked-eye, brightest "
			 "magnitude %.2f"),
		Seen.Num(), Catalogue.Num(),
		Seen.Num() > 0 ? Seen[0].ApparentMagnitude : 99.0);
}
