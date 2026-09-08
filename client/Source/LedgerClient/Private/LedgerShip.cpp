#include "LedgerShip.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "LedgerMeshBuilder.h"
#include "LedgerPlanet.h"
#include "LedgerLog.h"
#include "LedgerSurface.h"
#include "LedgerWorld.h"
#include "ProceduralMeshComponent.h"

namespace
{
	// A small courier: about 14 m nose to tail. Design §1.1 makes ship fidelity
	// an anti-goal, so this is a silhouette, not a model.
	constexpr float HullLength = 900.0f;
	constexpr float HullWidth = 190.0f;
	constexpr float HullHeight = 150.0f;

	const FColor HullColour(96, 100, 108, 255);
	const FColor PanelColour(70, 74, 82, 255);
	const FColor GlassColour(40, 62, 84, 255);
	const FColor EngineColour(52, 54, 58, 255);
}

ALedgerShip::ALedgerShip()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Hull = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Hull"));
	SetRootComponent(Hull);
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));
	Boom->SetupAttachment(Hull);
	// Far enough back that the hull is a ship in the frame rather than a wall
	// across the bottom of it.
	Boom->TargetArmLength = 5200.0f;
	Boom->SocketOffset = FVector(0.0f, 0.0f, 1500.0f);
	// The arm must not lag or collide: at orbital speeds a lagging camera swings
	// wildly, and a probe against streaming terrain snaps the view into the ground.
	Boom->bEnableCameraLag = false;
	Boom->bDoCollisionTest = false;
	Boom->bUsePawnControlRotation = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Boom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void ALedgerShip::BeginPlay()
{
	Super::BeginPlay();

	if (const UWorld* World = GetWorld())
	{
		if (const ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>())
		{
			Planet = Builder->GetPlanet();
		}
	}

	BuildHull();

	Underwater = LedgerSurface::CreateUnderwaterMaterial(this);
	if (Underwater != nullptr && Camera != nullptr)
	{
		// Added once at weight zero and re-weighted every frame. Adding and
		// removing the blendable instead would drop the material out of the
		// renderer's cache on every crossing, and the first frame back would
		// stall compiling it again.
		Camera->PostProcessSettings.AddBlendable(Underwater, 0.0f);
	}
}

void ALedgerShip::BuildHull()
{
	FLedgerMeshBuilder Builder;

	// Fuselage: three tapered sections so the silhouette has a nose rather than
	// being a brick.
	Builder.AddTaperedBox(
		FTransform(FVector(HullLength * 0.34f, 0.0f, 0.0f)),
		FVector2D(HullWidth * 0.72f, HullHeight * 0.72f),
		FVector2D(HullWidth * 0.18f, HullHeight * 0.22f),
		HullLength * 0.32f,
		HullColour);

	Builder.AddTaperedBox(
		FTransform(FVector(0.0f, 0.0f, 0.0f)),
		FVector2D(HullWidth, HullHeight),
		FVector2D(HullWidth * 0.72f, HullHeight * 0.72f),
		HullLength * 0.36f,
		HullColour);

	Builder.AddTaperedBox(
		FTransform(FVector(-HullLength * 0.34f, 0.0f, -HullHeight * 0.08f)),
		FVector2D(HullWidth * 0.82f, HullHeight * 0.6f),
		FVector2D(HullWidth, HullHeight),
		HullLength * 0.32f,
		PanelColour);

	// Canopy.
	Builder.AddTaperedBox(
		FTransform(FVector(HullLength * 0.16f, 0.0f, HullHeight * 0.86f)),
		FVector2D(HullWidth * 0.42f, HullHeight * 0.30f),
		FVector2D(HullWidth * 0.20f, HullHeight * 0.14f),
		HullLength * 0.26f,
		GlassColour);

	// Wings, swept back and angled down slightly.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float Sign = Side == 0 ? 1.0f : -1.0f;
		const FTransform WingTransform(
			FRotator(0.0f, Sign * 22.0f, Sign * -6.0f),
			FVector(-HullLength * 0.10f, Sign * HullWidth * 1.5f, -HullHeight * 0.10f));
		Builder.AddTaperedBox(
			WingTransform,
			FVector2D(HullWidth * 1.5f, HullHeight * 0.16f),
			FVector2D(HullWidth * 0.55f, HullHeight * 0.10f),
			HullLength * 0.42f,
			HullColour);

		// Engine nacelle under each wing.
		const FTransform NacelleTransform(
			FRotator(90.0f, 0.0f, 0.0f),
			FVector(-HullLength * 0.30f, Sign * HullWidth * 2.1f, -HullHeight * 0.28f));
		Builder.AddCylinder(NacelleTransform, HullHeight * 0.34f, HullHeight * 0.28f, HullLength * 0.34f, 10, EngineColour);
	}

	// Tail fin.
	Builder.AddTaperedBox(
		FTransform(
			FRotator(-16.0f, 0.0f, 0.0f),
			FVector(-HullLength * 0.42f, 0.0f, HullHeight * 1.15f)),
		FVector2D(HullWidth * 0.10f, HullHeight * 0.95f),
		FVector2D(HullWidth * 0.08f, HullHeight * 0.42f),
		HullLength * 0.24f,
		PanelColour);

	Builder.Upload(Hull, 0, /*bCreateCollision*/ false);

	if (UMaterialInterface* Material = LedgerSurface::CreateFlatMaterial(this, FLinearColor(0.34f, 0.36f, 0.40f), 0.42f))
	{
		Hull->SetMaterial(0, Material);
	}

	UE_LOG(LogLedger, Log, TEXT("ship hull: %d vertices"), Builder.VertexCount());
}

void ALedgerShip::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (PlayerInputComponent == nullptr)
	{
		return;
	}

	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ALedgerShip::InputThrottle);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ALedgerShip::InputStrafe);
	PlayerInputComponent->BindAxis(TEXT("MoveUp"), this, &ALedgerShip::InputLift);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ALedgerShip::InputPitch);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ALedgerShip::InputYaw);
	PlayerInputComponent->BindAxis(TEXT("Roll"), this, &ALedgerShip::InputRoll);
}

void ALedgerShip::SetCameraBoom(float ArmLength, float HeightOffset)
{
	if (Boom != nullptr)
	{
		Boom->TargetArmLength = ArmLength;
		Boom->SocketOffset = FVector(0.0f, 0.0f, HeightOffset);
	}
}

FVector ALedgerShip::LocalUp() const
{
	if (Planet == nullptr)
	{
		return FVector::UpVector;
	}
	const FVector Radial = GetActorLocation() - Planet->GetActorLocation();
	return Radial.GetSafeNormal(0.0, FVector::UpVector);
}

double ALedgerShip::AltitudeMetres() const
{
	if (Planet == nullptr)
	{
		return 0.0;
	}
	const FVector3d Radial = FVector3d(GetActorLocation() - Planet->GetActorLocation());
	const double Distance = Radial.Length();
	if (Distance <= 0.0)
	{
		return 0.0;
	}
	return (Distance - Planet->SurfaceRadiusAt(Radial / Distance)) / 100.0;
}

void ALedgerShip::UpdateSubmersion(float DeltaSeconds)
{
	if (Planet == nullptr || Camera == nullptr || Underwater == nullptr)
	{
		return;
	}

	// The camera decides, not the hull. On a boom several metres behind the
	// ship the two cross the surface at visibly different moments, and the one
	// the player notices is the one they are looking through.
	const FVector3d Radial = FVector3d(Camera->GetComponentLocation() - Planet->GetActorLocation());
	const bool bSubmerged = Radial.Length() < Planet->Radius;

	// Eased rather than switched. A hard cut is a flash on the way out and a
	// pop on the way in, and no crossing speed makes either of them acceptable;
	// a fifth of a second of ramp reads as the surface passing the lens.
	Submersion = FMath::FInterpTo(Submersion, bSubmerged ? 1.0f : 0.0f, DeltaSeconds, 9.0f);

	FWeightedBlendables& Blendables = Camera->PostProcessSettings.WeightedBlendables;
	if (Blendables.Array.Num() > 0)
	{
		Blendables.Array[0].Weight = Submersion;
	}
}

void ALedgerShip::ApplyInput(float DeltaSeconds)
{
	// Rotation is applied in the ship's own frame — this is 6-DOF, so there is
	// no world "up" to keep level against and no gimbal to lock.
	const FRotator Delta(
		PitchInput * PitchRate * DeltaSeconds,
		YawInput * YawRate * DeltaSeconds,
		RollInput * RollRate * DeltaSeconds);
	AddActorLocalRotation(Delta.Quaternion());

	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const FVector Up = GetActorUpVector();

	FVector Acceleration =
		Forward * ((ThrottleInput + AutoThrottle) * MainThrust)
		+ Right * (StrafeInput * ManoeuvringThrust)
		+ Up * (LiftInput * ManoeuvringThrust);

	Velocity += Acceleration * DeltaSeconds;
}

void ALedgerShip::Integrate(float DeltaSeconds)
{
	if (Planet == nullptr)
	{
		SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);
		return;
	}

	const FVector Centre = Planet->GetActorLocation();
	const FVector3d Radial = FVector3d(GetActorLocation() - Centre);
	const double Distance = FMath::Max(Radial.Length(), 1.0);
	const FVector3d Up = Radial / Distance;

	// Inverse square, so leaving is expensive near the ground and cheap once
	// you are up. Newton, not a constant.
	const double GravityHere = SurfaceGravity * FMath::Square(Planet->Radius / Distance);
	Velocity -= FVector(Up) * static_cast<float>(GravityHere) * DeltaSeconds;

	// Drag, exponential in altitude. Above a few scale heights this is zero and
	// the ship coasts; below it, the air is something you feel.
	const double SurfaceRadius = Planet->SurfaceRadiusAt(Up);
	const double Altitude = Distance - SurfaceRadius;
	if (Altitude < DragScaleHeight * 6.0)
	{
		const double Density = FMath::Exp(-FMath::Max(Altitude, 0.0) / DragScaleHeight);
		const float Damping = FMath::Clamp(
			1.0f - static_cast<float>(AtmosphericDrag * Density) * DeltaSeconds, 0.0f, 1.0f);
		Velocity *= Damping;
	}

	FVector NewLocation = GetActorLocation() + Velocity * DeltaSeconds;

	// Ground contact. A ray against the streaming collision would miss whenever
	// the patch under us has not cooked yet, so the *surface height function* is
	// the authority — it is the same function the mesh was built from, it is
	// always available, and it never disagrees with the geometry.
	const FVector3d NewRadial = FVector3d(NewLocation - Centre);
	const double NewDistance = FMath::Max(NewRadial.Length(), 1.0);
	const FVector3d NewUp = NewRadial / NewDistance;
	const double GroundRadius = Planet->SurfaceRadiusAt(NewUp) + 140.0; // landing gear

	if (NewDistance < GroundRadius)
	{
		NewLocation = Centre + FVector(NewUp * GroundRadius);

		// Kill the component of velocity into the ground, keep the rest, and
		// scrub the remainder off as friction.
		const FVector UpVector(NewUp);
		const float Into = FVector::DotProduct(Velocity, UpVector);
		if (Into < 0.0f)
		{
			Velocity -= UpVector * Into;
		}
		Velocity *= 0.86f;

		if (!bLanded)
		{
			bLanded = true;
			UE_LOG(LogLedger, Log, TEXT("ship down at %.0f m, %.0f m/s"),
				AltitudeMetres(), Velocity.Size() / 100.0f);
		}
	}
	else if (NewDistance > GroundRadius + 500.0)
	{
		bLanded = false;
	}

	SetActorLocation(NewLocation);
}

void ALedgerShip::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Planet == nullptr)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>())
			{
				Planet = Builder->GetPlanet();
			}
		}
	}

	if (DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		// Ahead of the flight-enabled test: the scripted sequence drives the
		// transform directly, and the camera still has to know when it is wet.
		UpdateSubmersion(DeltaSeconds);
	}

	if (!bFlightEnabled || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	ApplyInput(DeltaSeconds);
	Integrate(DeltaSeconds);

	// The camera boom follows the hull, so it needs no world-up reference and
	// stays correct upside down, in orbit, and everywhere between.
	if (Boom != nullptr)
	{
		Boom->SetRelativeRotation(FRotator::ZeroRotator);
	}
}
