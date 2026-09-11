#include "LedgerShip.h"

#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "LedgerEnvironment.h"
#include "LedgerShipSystems.h"
#include "LedgerShipSystemsComponent.h"
#include "LedgerPrecipitation.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "LedgerMeshBuilder.h"
#include "LedgerFlightModel.h"
#include "LedgerAir.h"
#include "LedgerPlanet.h"
#include "LedgerStorm.h"
#include "LedgerLog.h"
#include "LedgerSurface.h"
#include "LedgerWind.h"
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

	// T100: the glow of the air the hull is heating, drawn from the flux and
	// the skin temperature and from nothing else. ponytail: a light at the
	// nose, not a plasma sheath; a mesh when the glow has to have a shape.
	EntryGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("EntryGlow"));
	EntryGlow->SetupAttachment(Hull);
	EntryGlow->SetRelativeLocation(FVector(600.0f, 0.0f, 0.0f));
	EntryGlow->SetIntensityUnits(ELightUnits::Candelas);
	EntryGlow->SetIntensity(0.0f);
	EntryGlow->SetAttenuationRadius(6000.0f);
	EntryGlow->SetCastShadows(false);

	Systems = CreateDefaultSubobject<ULedgerShipSystemsComponent>(TEXT("Systems"));

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void ALedgerShip::BeginPlay()
{
	Super::BeginPlay();

	// **The demister on from the start (M5P).** The canopy model leaves the heater
	// off, and a cold day then fogs the glass from the cabin's own moisture --
	// which the visor draws as a five-tap blur washed towards white. The first
	// playtest saw exactly that at the pad, doubled trees in a grey veil, and
	// called it blurry. A pilot switches the demister off, not on; the visor
	// fixture still sets it per step.
	Canopy.bHeater = true;
	// And the glass at the cabin's temperature, not whatever is outside. The
	// canopy model starts the glass at the outside air, which for a ship that
	// spawns in orbit is space: it fogged solid in the first second and took
	// fifty to clear, and that was the grey blur in the owner's first screenshot.
	Canopy.GlassKelvin = 295.0;

	if (const UWorld* World = GetWorld())
	{
		if (const ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>())
		{
			Planet = Builder->GetPlanet();
		}
	}

	LoadDefinition();
	BuildHull();
	// The kit is drawn at the courier size; a bigger ship is the same art scaled.
	if (ShipDefinition.Components.Num() > 0 && Hull != nullptr)
	{
		Hull->SetRelativeScale3D(FVector(
			static_cast<float>(ShipDefinition.Hull.LengthMetres / 9.0),
			static_cast<float>(ShipDefinition.Hull.WidthMetres / 1.9),
			static_cast<float>(ShipDefinition.Hull.HeightMetres / 1.5)));
	}

	Underwater = LedgerSurface::CreateUnderwaterMaterial(this);
	if (Underwater != nullptr && Camera != nullptr)
	{
		// Added once at weight zero and re-weighted every frame. Adding and
		// removing the blendable instead would drop the material out of the
		// renderer's cache on every crossing, and the first frame back would
		// stall compiling it again.
		Camera->PostProcessSettings.AddBlendable(Underwater, 0.0f);
	}

	// T101: the canopy, at full weight from the start. Its own parameters say
	// how much of it shows, and at zero it passes the scene straight through.
	//
	// `-novisor` leaves it out of the post chain altogether: the control frame
	// T441 measures against. The acceptance asks for the playtest's captures to
	// hold their edge contrast within 10% of a static frame from the same place,
	// and "the same place with the canopy gone" is the only version of that
	// which attributes the difference to the canopy rather than to the weather,
	// the hour or where the ship happened to be (M5P).
	if (FParse::Param(FCommandLine::Get(), TEXT("novisor")))
	{
		UE_LOG(LogLedger, Log, TEXT("canopy: the visor is out of the post chain (-novisor)"));
	}
	else if (UMaterialInterface* VisorMaterial = LedgerSurface::CreateVisorMaterial(this))
	{
		Visor = UMaterialInstanceDynamic::Create(VisorMaterial, this);
		if (Visor != nullptr && Camera != nullptr)
		{
			Camera->PostProcessSettings.AddBlendable(Visor, 1.0f);
		}
	}
}

// The hull's shape, separated from the component it usually goes into.
//
// Static, and taking a builder rather than filling a member, because the same
// geometry now has two destinations: a procedural mesh component at runtime, and
// a saved static mesh asset at bake time. ADR-0006 -- a hull that exists only as
// a runtime mesh gets no Nanite, no LOD chain and no distance field, and the way
// to avoid describing the shape twice is to describe it once.
void ALedgerShip::DescribeHull(FLedgerMeshBuilder& Builder)
{

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

}

void ALedgerShip::BuildHull()
{
	FLedgerMeshBuilder Builder;
	DescribeHull(Builder);
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
	PlayerInputComponent->BindAction(TEXT("FlightMode"), IE_Pressed, this, &ALedgerShip::InputNextMode);
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
	if (Systems != nullptr && Systems->IsConfigured() && ShipDefinition.Nozzles.Num() > 0)
	{
		ApplyThrust(DeltaSeconds);
		return;
	}

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

	// T131: what the systems can give -- power reaching the thrusters, their
	// wear, and fuel in the tanks -- shares every push the pilot asks for.
	const bool bSystems = Systems != nullptr && Systems->IsConfigured();
	const float MainShare = bSystems ? static_cast<float>(Systems->MainThrustShare()) : 1.0f;
	const float SideShare = bSystems ? static_cast<float>(Systems->ManoeuvringThrustShare()) : 1.0f;
	const float Main = (ThrottleInput + AutoThrottle) * MainThrust * MainShare;
	const FVector Side = Right * ((StrafeInput + AutoStrafe) * ManoeuvringThrust * SideShare)
		+ Up * ((LiftInput + AutoLift) * ManoeuvringThrust * SideShare);
	FVector Acceleration = Forward * Main + Side;
	if (bSystems)
	{
		// Thrust as force: the acceleration asked for, on the mass there is.
		const double Mass = Systems->MassKg();
		Systems->Burned(FMath::Abs(Main) / 100.0 * Mass, Side.Size() / 100.0 * Mass, DeltaSeconds);
	}

	Velocity += Acceleration * DeltaSeconds;
}

void ALedgerShip::ApplyThrust(float DeltaSeconds)
{
	// T132 to T134. The mode reads the stick as a force and a torque; the
	// allocator and the rigid body make what they can of it and are not told
	// which mode asked. What the ship then does is what its nozzles made.
	const FLedgerShipState& State = Systems->GetState();
	const FLedgerMassProperties Mass = LedgerShipSystems::MassProperties(ShipDefinition, State);
	if (Mass.MassKg <= 0.0 || DeltaSeconds <= 0.0f)
	{
		return;
	}
	FLedgerMotion Flight;
	Flight.Spin = Spin;
	Flight.Spin.Orientation = GetActorQuat();
	Flight.Velocity = FVector3d(Velocity) / 100.0;
	FLedgerStick Stick;
	Stick.Turn = FVector3d(PitchInput + AutoTurn.X, YawInput + AutoTurn.Y, RollInput + AutoTurn.Z);
	Stick.Push = FVector3d(ThrottleInput + AutoThrottle, StrafeInput + AutoStrafe, LiftInput + AutoLift);
	// T135: the air first, so the rate hold knows what the wings are already
	// doing and the nozzles give only the rest. The stick moves the surfaces.
	FLedgerCommand Air;
	LastAero = FLedgerAeroState();
	const UWorld* World = GetWorld();
	const ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	if (ShipDefinition.Aero.bWinged && Builder != nullptr && Planet != nullptr)
	{
		const FLedgerAirProfile AirProfile = LedgerAir::For(Builder->GetSystem(), Builder->GetHomeBodyIndex(), Builder->GetWhenSeconds());
		const double AboveDatum = (FVector3d(GetActorLocation()) - FVector3d(Planet->GetActorLocation())).Length() / 100.0
			- Planet->Radius / 100.0;
		const double BellyArea = ShipDefinition.Flight.BallisticKgPerM2 > 0.0 ? Mass.MassKg / ShipDefinition.Flight.BallisticKgPerM2 : 0.0;
		Air = LedgerFlight::Aerodynamics(ShipDefinition.Aero, BellyArea, Flight, FVector3d(LastWind) / 100.0,
			LedgerAir::DensityAt(AirProfile, AboveDatum), Stick.Turn, &LastAero);
	}
	FLedgerCommand Command = LedgerFlight::Control(FlightMode, Stick, FLedgerHandling::From(ShipDefinition.Flight),
		Mass, Flight, DeltaSeconds, Air.Torque);
	// **Coupled holds against gravity (M5P).** The drift hold closes vertical
	// drift in proportion to it and knew nothing of weight, so with the stick
	// centred the ship settled where the hold matched g: the first playtest sank
	// from 42 m to the pad at a steady 2.5 m/s, which is g over the hold's 4 per
	// second. The weight is now asked for outright, in the body frame the
	// command is in, and the hold only has the rest to do.
	if (FlightMode == ELedgerFlightMode::Coupled && Planet != nullptr)
	{
		const FVector3d Radial = FVector3d(GetActorLocation()) - FVector3d(Planet->GetActorLocation());
		const double Distance = FMath::Max(Radial.Length(), 1.0);
		const double GravityHere = SurfaceGravity / 100.0 * FMath::Square(Planet->Radius / Distance);
		FVector3d Feed = Flight.Spin.Orientation.UnrotateVector(Radial / Distance * GravityHere) * Mass.MassKg;
		// Less what the air already holds up across the nose. At 150 m/s the
		// wings carry about the weight themselves, and cancelling it twice had
		// the fourth playtest climbing from 260 m to 655 m with the hold fighting
		// both. Along the nose the air is drag, and that stays the pilot's.
		Feed.Y -= Air.Force.Y;
		Feed.Z -= Air.Force.Z;
		Command.Force += Feed;
	}

	// Each nozzle limited by what its thruster can give -- power, wear, fuel.
	const int32 MainEngine = ShipDefinition.FindComponent(TEXT("main_engine"));
	TArray<double> Limits;
	for (const FLedgerNozzle& Nozzle : ShipDefinition.Nozzles)
	{
		Limits.Add(Nozzle.ThrustNewtons * (Nozzle.Component == MainEngine
			? Systems->MainThrustShare() : Systems->ManoeuvringThrustShare()));
	}
	LastAllocation = LedgerFlight::Push(ShipDefinition.Nozzles, Limits, Mass, Command, Flight, DeltaSeconds, Air);
	double MainNewtons = 0.0;
	double SideNewtons = 0.0;
	for (int32 Index = 0; Index < ShipDefinition.Nozzles.Num(); ++Index)
	{
		(ShipDefinition.Nozzles[Index].Component == MainEngine ? MainNewtons : SideNewtons) += LastAllocation.ThrustNewtons[Index];
	}
	Systems->Burned(MainNewtons, SideNewtons, DeltaSeconds);

	Spin = Flight.Spin;
	SetActorRotation(FQuat(Spin.Orientation));
	Velocity = FVector(Flight.Velocity * 100.0);
}

void ALedgerShip::Integrate(float DeltaSeconds)
{
	if (Planet == nullptr)
	{
		SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);
		return;
	}

	// The pawn's job here is translation, not physics. It hands the model where
	// it is, how fast, and what it needs to know about the planet; the model
	// integrates and hands back a position. Everything that used to be in this
	// function is in LedgerFlight, where it can be tested without a world.
	FLedgerGravityField Field;
	Field.Centre = FVector3d(Planet->GetActorLocation());
	Field.Radius = Planet->Radius;
	Field.SurfaceGravity = SurfaceGravity;
	Field.DragScaleHeight = DragScaleHeight;
	Field.AtmosphericDrag = AtmosphericDrag;
	Field.BallisticKgPerM2 = BallisticCoefficient;
	// T135: a winged ship has its drag from the air model, which knows which
	// way it faces; the prototype's fraction-a-second bleed would not let it
	// glide, and the entry drag is its belly.
	if (ShipDefinition.Aero.bWinged && ShipDefinition.Nozzles.Num() > 0 && Systems != nullptr && Systems->IsConfigured())
	{
		Field.AtmosphericDrag = 0.0;
		Field.BallisticKgPerM2 = 0.0;
	}

	// The terrain's own height function, not a collision trace. A trace would
	// miss wherever the patch underneath has not cooked yet, which is exactly
	// when a ship is moving fast enough to need the answer.
	ALedgerPlanet* Body = Planet;
	Field.SurfaceRadiusAt = [Body](const FVector3d& Direction)
	{
		return Body->SurfaceRadiusAt(Direction);
	};

	// **The one wind, asked for once.** T093. The flight model does not know
	// where weather comes from and does not need to; it is handed what the air
	// is doing here, from the same subsystem the grass reads.
	if (const UWorld* World = GetWorld())
	{
		// The body's own air at the datum, for the entry drag; none on a body
		// without any.
		if (const ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>())
		{
			Field.SeaLevelDensity = LedgerAir::DensityAt(LedgerAir::For(Builder->GetSystem(),
				Builder->GetHomeBodyIndex(), Builder->GetWhenSeconds()), 0.0);
		}
		if (const ULedgerWind* Wind = World->GetSubsystem<ULedgerWind>())
		{
			Field.WindCmPerSecond = FVector3d(Wind->WindAt(GetActorLocation()));
			// T097: and the storm's gusts on top, from the same weather.
			if (const ULedgerStorm* Storms = World->GetSubsystem<ULedgerStorm>())
			{
				Field.WindCmPerSecond += FVector3d(Storms->GustAt(GetActorLocation()));
			}
			LastWind = Field.WindCmPerSecond;
		}
	}

	FLedgerFlightState State;
	State.Position = FVector3d(GetActorLocation());
	State.Velocity = FVector3d(Velocity);
	State.bLanded = bLanded;

	LedgerFlight::Advance(State, Field, DeltaSeconds, PhysicsRemainder);

	Velocity = FVector(State.Velocity);
	SetActorLocation(FVector(State.Position));

	if (State.bLanded && !bLanded)
	{
		UE_LOG(LogLedger, Log, TEXT("ship down at %.0f m, %.0f m/s"),
			AltitudeMetres(), Velocity.Size() / 100.0f);
	}
	bLanded = State.bLanded;
}

void ALedgerShip::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// **Not valid, rather than not null.** T088 lets the world change bodies
	// mid-session, which destroys one planet and builds another -- and a
	// UPROPERTY to a destroyed actor is not cleared until the collector runs,
	// so a null check keeps handing the ship a planet that has already torn
	// down its job table.
	if (!IsValid(Planet))
	{
		Planet = nullptr;
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
		UpdateCanopy(DeltaSeconds);
	}

	if (!bFlightEnabled || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	ApplyInput(DeltaSeconds);
	Integrate(DeltaSeconds);
	SufferWeather(DeltaSeconds);
	SufferHeat(DeltaSeconds);

	// The camera boom follows the hull, so it needs no world-up reference and
	// stays correct upside down, in orbit, and everywhere between.
	if (Boom != nullptr)
	{
		Boom->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void ALedgerShip::SufferWeather(float DeltaSeconds)
{
	// T097. Inside the cloud, where the hail and the charge are: under the base
	// a storm is rain and wind, which the flight model already feels through
	// the gusts, and above the top there is nothing to hit.
	const UWorld* World = GetWorld();
	const ULedgerStorm* Storms = World != nullptr ? World->GetSubsystem<ULedgerStorm>() : nullptr;
	const ULedgerWorldBuilder* Builder =
		World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	if (Storms == nullptr || Builder == nullptr || Planet == nullptr || Integrity <= 0.0)
	{
		return;
	}
	const FLedgerStorm Storm = Storms->StormAt(GetActorLocation());
	if (!Storm.IsSevere())
	{
		return;
	}
	const FLedgerAirProfile Air = LedgerAir::For(
		Builder->GetSystem(), Builder->GetHomeBodyIndex(), Builder->GetWhenSeconds());
	const double AboveDatum = (FVector3d(GetActorLocation())
		- FVector3d(Planet->GetActorLocation())).Length() / 100.0 - Planet->Radius / 100.0;
	if (AboveDatum < Air.CloudBaseMetres || AboveDatum > Air.CloudTopMetres)
	{
		return;
	}

	// Hail and turbulence, as the square of the severity: the core of the worst
	// storm takes a hull in under two minutes and its fringe barely marks one.
	constexpr double WearPerSecond = 0.01;
	Integrity -= WearPerSecond * Storm.Severity * Storm.Severity * DeltaSeconds;

	// A fifth of the flashes in the cloud around the ship find it.
	constexpr double StrikeShare = 0.2;
	constexpr double StrikeDamage = 0.1;
	if (StrikeDice.FRand() < Storm.FlashesPerMinute / 60.0 * StrikeShare * DeltaSeconds)
	{
		++Strikes;
		Integrity -= StrikeDamage;
		UE_LOG(LogLedger, Log, TEXT("ship: struck by lightning at %.0f m, severity %.2f; hull %.2f"),
			AboveDatum, Storm.Severity, FMath::Max(Integrity, 0.0));
	}
	if (Integrity <= 0.0)
	{
		Integrity = 0.0;
		UE_LOG(LogLedger, Warning, TEXT("ship: hull lost in a storm of severity %.2f"), Storm.Severity);
	}
}

void ALedgerShip::SufferHeat(float DeltaSeconds)
{
	// T100. The skin is heated by the air it is pushing through and by nothing
	// else: the density where the ship is, its speed through that air, and the
	// temperature it radiates back towards -- each from the field everything
	// else reads. No altitude appears anywhere in the decision.
	const UWorld* World = GetWorld();
	const ULedgerWorldBuilder* Builder =
		World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	if (Builder == nullptr || Planet == nullptr)
	{
		return;
	}
	const FLedgerAirProfile Air = LedgerAir::For(
		Builder->GetSystem(), Builder->GetHomeBodyIndex(), Builder->GetWhenSeconds());
	const double AboveDatum = (FVector3d(GetActorLocation())
		- FVector3d(Planet->GetActorLocation())).Length() / 100.0 - Planet->Radius / 100.0;
	const double Density = LedgerAir::DensityAt(Air, AboveDatum);
	const double Airspeed = (FVector3d(Velocity) - LastWind).Length() / 100.0;
	const FLedgerAirHere Here = LedgerEnvironment::At(World, GetActorLocation());
	LedgerFlight::Heat(Heat, Shield, Density, Airspeed,
		Here.bValid && Here.Kelvin > 0.0 ? Here.Kelvin : 250.0, DeltaSeconds);

	if (Heat.Damage > LastHeatDamage)
	{
		Integrity = FMath::Max(0.0, Integrity - (Heat.Damage - LastHeatDamage));
		LastHeatDamage = Heat.Damage;
	}

	// As bright as the flux, and the colour of the skin.
	if (EntryGlow != nullptr)
	{
		const bool bGlowing = Heat.FluxWattsPerM2 > 2.0e4;
		EntryGlow->SetIntensity(bGlowing ? static_cast<float>(Heat.FluxWattsPerM2 * 2.0) : 0.0f);
		if (bGlowing)
		{
			EntryGlow->SetLightColor(FLinearColor::MakeFromColorTemperature(
				static_cast<float>(FMath::Clamp(Heat.SkinKelvin, 1000.0, 15000.0))));
		}
	}
}

void ALedgerShip::UpdateCanopy(float DeltaSeconds)
{
	// T101. What is falling where the ship is, how fast the air is going past
	// the glass, and how cold it is outside -- from the weather everything else
	// reads -- and the heater and wipers the ship has switched on.
	const UWorld* World = GetWorld();
	const ULedgerWorldBuilder* Builder =
		World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	if (Builder == nullptr || Planet == nullptr)
	{
		return;
	}
	const FLedgerSystem& System = Builder->GetSystem();
	const int32 Home = Builder->GetHomeBodyIndex();
	const double When = Builder->GetWhenSeconds();
	const FVector3d Up = (FVector3d(GetActorLocation()) - FVector3d(Planet->GetActorLocation())).GetSafeNormal();
	const FLedgerAirHere Here = LedgerEnvironment::At(World, GetActorLocation());
	const FLedgerPrecipitation Falling = LedgerPrecip::At(System, Home,
		LedgerAir::For(System, Home, When), FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0)),
		FMath::Atan2(Up.Y, Up.X), Here.AboveGroundMetres, When, Here.GroundKelvin);

	FLedgerCanopyWeather Outside;
	// ponytail: snow counts as water on the glass; it melts on a heated one
	// and the ice term catches it on a cold one.
	Outside.RainMillimetresPerHour = Falling.Kind == ELedgerPrecipitation::Rain
		|| Falling.Kind == ELedgerPrecipitation::Snow ? Falling.RateMillimetresPerHour : 0.0;
	Outside.DustRate = Falling.Kind == ELedgerPrecipitation::Dust ? Falling.RateMillimetresPerHour : 0.0;
	Outside.AirspeedMetresPerSecond = (FVector3d(Velocity) - LastWind).Length() / 100.0;
	Outside.AmbientKelvin = Here.bValid && Here.Kelvin > 0.0 ? Here.Kelvin : 288.0;
	Outside.CabinDewPointKelvin = CabinDewPointKelvin;
	LedgerFlight::Expose(Canopy, Outside, DeltaSeconds);

	if (Visor != nullptr)
	{
		Visor->SetScalarParameterValue(TEXT("VisorWater"), static_cast<float>(Canopy.Water));
		Visor->SetScalarParameterValue(TEXT("VisorFlow"), static_cast<float>(Canopy.Flow));
		Visor->SetScalarParameterValue(TEXT("VisorIce"), static_cast<float>(Canopy.Ice));
		Visor->SetScalarParameterValue(TEXT("VisorFog"), static_cast<float>(Canopy.Fog));
		Visor->SetScalarParameterValue(TEXT("VisorDust"), static_cast<float>(Canopy.Dust));
	}
}

void ALedgerShip::LoadDefinition()
{
	// T108. A ship is a file: the flight numbers here are the file, and a ship
	// that will not load says why and flies the built-in ones rather than none.
	FString Name = TEXT("courier");
	FParse::Value(FCommandLine::Get(), TEXT("ship="), Name);
	TArray<FString> Errors;
	if (!LedgerShips::Load(LedgerShips::DefaultDirectory(), Name, ShipDefinition, Errors))
	{
		for (const FString& Error : Errors)
		{
			UE_LOG(LogLedger, Error, TEXT("ship: %s"), *Error);
		}
		UE_LOG(LogLedger, Warning, TEXT("ship: %s did not load; flying the built-in numbers"), *Name);
		ShipDefinition = FLedgerShipDefinition();
		return;
	}
	const FLedgerShipFlight& Flight = ShipDefinition.Flight;
	MainThrust = static_cast<float>(Flight.MainThrust * 100.0);
	ManoeuvringThrust = static_cast<float>(Flight.ManoeuvringThrust * 100.0);
	PitchRate = static_cast<float>(Flight.PitchRate);
	YawRate = static_cast<float>(Flight.YawRate);
	RollRate = static_cast<float>(Flight.RollRate);
	AtmosphericDrag = static_cast<float>(Flight.AtmosphericDrag);
	BallisticCoefficient = static_cast<float>(Flight.BallisticKgPerM2);
	if (Systems != nullptr)
	{
		Systems->Configure(ShipDefinition);
	}
	UE_LOG(LogLedger, Log, TEXT("ship: %s from Config/Ships, %d components, %d connections, %.1f t, %.1f m long"),
		*ShipDefinition.Name, ShipDefinition.Components.Num(), ShipDefinition.Connections.Num(),
		ShipDefinition.MassKg() / 1000.0, ShipDefinition.Hull.LengthMetres);
}
