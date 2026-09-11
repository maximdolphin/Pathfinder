// A small ship, and the flight model that gets it to orbit and back.
//
// Design §6.9: ships are transport and capability, not the product. Three hulls
// at MVP, cockpit-only interiors, walkable interiors post-MVP. This is one hull,
// generated from the parametric kit, with no interior at all.
//
// **The flight model is not Chaos.** §6.9 notes that local physics grids are
// engine-level surgery because Chaos is a single simulation space with no nested
// reference frames. Rather than fight that, the ship integrates its own state:
// position, velocity, and orientation, with gravity toward the planet's centre
// and drag that only exists inside the atmosphere. That is a few dozen lines,
// it is exactly as correct at 6,000 km as at 6 m, and it never argues with the
// terrain's streaming collision.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "LedgerCanopy.h"
#include "LedgerFlightModel.h"
#include "LedgerRigidBody.h"
#include "LedgerShipDefinition.h"
#include "LedgerShip.generated.h"

struct FLedgerMeshBuilder;

class ALedgerPlanet;
class UCameraComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USpringArmComponent;
class ULedgerShipSystemsComponent;

UCLASS()
class LEDGERCLIENT_API ALedgerShip : public APawn
{
	GENERATED_BODY()

public:
	ALedgerShip();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/// Thrust available along each axis, in cm/s². Forward is generous because
	/// leaving a planet has to be possible in less than an afternoon.
	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float MainThrust = 900000.0f;

	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float ManoeuvringThrust = 260000.0f;

	/// Degrees per second at full deflection.
	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float PitchRate = 55.0f;

	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float YawRate = 45.0f;

	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float RollRate = 90.0f;

	/// Surface gravity in cm/s². Earth is 981.
	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float SurfaceGravity = 981.0f;

	/// Fraction of velocity bled off per second at sea-level density. Drag is
	/// what makes the atmosphere something you *enter* rather than a texture.
	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float AtmosphericDrag = 0.55f;

	/// Scale height of the drag falloff, in centimetres. 8 km, like the air.
	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float DragScaleHeight = 800000.0f;

	/// Mass over drag area, kg/m^2. T100: the drag an entry sheds its speed
	/// by, as rho v squared. A capsule is a few hundred.
	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float BallisticCoefficient = 300.0f;

	/// While true the ship does not integrate — something else is placing it.
	/// Used by the scripted reentry so the two do not fight over the transform.
	void SetFlightEnabled(bool bEnabled)
	{
		bFlightEnabled = bEnabled;
		// Held by hand, a ship is not spinning when it is let go.
		if (!bEnabled)
		{
			Spin.AngularVelocity = FVector3d::ZeroVector;
		}
	}
	bool IsFlightEnabled() const { return bFlightEnabled; }

	void SetVelocity(const FVector& InVelocity) { Velocity = InVelocity; }

	/// Throttle applied on top of player input, in [0,1]. The scripted ascent
	/// uses this so the climb to orbit runs through the *same* flight model the
	/// player flies — gravity, drag and all — rather than being animated.
	void SetAutoThrottle(float Fraction) { AutoThrottle = FMath::Clamp(Fraction, 0.0f, 1.0f); }

	/// Lift and strafe on top of player input, in [-1,1]. T098's fixture flies
	/// through these, so its corrections go through the thrusters a pilot's
	/// would and nowhere else.
	void SetAutoLift(float Fraction) { AutoLift = FMath::Clamp(Fraction, -1.0f, 1.0f); }
	void SetAutoStrafe(float Fraction) { AutoStrafe = FMath::Clamp(Fraction, -1.0f, 1.0f); }
	/// Stick for a fixture: pitch, yaw and roll, each a fraction of the rate.
	void SetAutoTurn(const FVector3f& Fractions) { AutoTurn = Fractions.BoundToCube(1.0f); }

	/// T134: how the stick is read -- assist off, decoupled or coupled. Only
	/// the controller changes; the ship underneath is the same one.
	void SetFlightMode(ELedgerFlightMode Mode) { FlightMode = Mode; }
	ELedgerFlightMode GetFlightMode() const { return FlightMode; }
	FVector GetVelocity() const override { return Velocity; }

	/// The wind the flight model was last handed, centimetres per second.
	/// T093's proof reads it to show the ship flies in the same air as
	/// everything else.
	FVector3d LastWindCmPerSecond() const { return LastWind; }

	/// What is left of the hull, 1 whole to 0 gone. T097: a severe storm wears
	/// it down inside the cloud, and a lightning strike takes a piece at once.
	double HullIntegrity() const { return Integrity; }
	int32 StrikesTaken() const { return Strikes; }
	void RepairHull() { Integrity = 1.0; Strikes = 0; }

	/// The skin through an entry, T100: the flux, the temperature, the heat
	/// taken in and radiated, and what it has cost the hull.
	const FLedgerHeatState& EntryHeat() const { return Heat; }
	void ResetEntryHeat() { Heat = FLedgerHeatState(); LastHeatDamage = 0.0; }

	/// The canopy, T101: what the weather has put on it, and the heater and
	/// wipers that take it off again.
	const FLedgerCanopy& CanopyState() const { return Canopy; }

	/// What this ship is, from Config/Ships (T108): -ship=name, the courier
	/// when nothing is named. The flight numbers and the hull size come from it.
	const FLedgerShipDefinition& Definition() const { return ShipDefinition; }

	/// T132 and T133: how fast it is turning, body frame, and what its nozzles
	/// were last told to do.
	FVector3d AngularVelocity() const { return Spin.AngularVelocity; }
	const FLedgerAllocation& Allocation() const { return LastAllocation; }

	/// Its systems, running. T131.
	ULedgerShipSystemsComponent* GetSystems() const { return Systems; }

	/// What the pilot and the autopilot are asking of the main engine, 0 to 1:
	/// the engine note is pitched from it. T126.
	float CurrentThrottle() const { return FMath::Clamp(ThrottleInput + AutoThrottle, 0.0f, 1.0f); }

	/// The hull, for the damage view to paint. T127.
	UProceduralMeshComponent* GetHullMesh() const { return Hull; }
	void SetCanopyHeater(bool bOn) { Canopy.bHeater = bOn; }
	void SetWipers(bool bOn) { Canopy.bWipers = bOn; }

	/// The cabin air's dew point, kelvin: glass colder than this fogs.
	UPROPERTY(EditAnywhere, Category = "Ledger|Flight")
	float CabinDewPointKelvin = 288.0f;

	/// Moves the chase camera. A negative arm length puts it ahead of the nose.
	///
	/// The default boom holds the camera fifty metres back and fifteen up,
	/// which is a good third-person view and useless in eight metres of water:
	/// the hull submerges and the camera stays in the air above it.
	void SetCameraBoom(float ArmLength, float HeightOffset);

	/// Metres above the terrain directly below.
	double AltitudeMetres() const;

	/// Local up — away from the planet's centre. Everything about orientation
	/// near a planet is relative to this, and it is a different direction at
	/// every point on the surface.
	FVector LocalUp() const;

private:
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Hull;

	UPROPERTY()
	TObjectPtr<USpringArmComponent> Boom;

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	/// The glow of the air the hull is heating. T100.
	UPROPERTY()
	TObjectPtr<class UPointLightComponent> EntryGlow;

	/// The ship's systems, running on its component graph. T131.
	UPROPERTY()
	TObjectPtr<ULedgerShipSystemsComponent> Systems;

	/// The canopy over the view. T101.
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> Visor;

	UPROPERTY()
	TObjectPtr<ALedgerPlanet> Planet;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Underwater;

	/// How much of the underwater murk is currently applied, in [0,1].
	float Submersion = 0.0f;

	FVector Velocity = FVector::ZeroVector;

	/// Leftover time between fixed physics steps. The flight model runs at a
	/// fixed rate whatever the frame rate is; this is what carries the
	/// remainder across frames.
	double PhysicsRemainder = 0.0;
	FVector3d LastWind = FVector3d::ZeroVector;
	bool bFlightEnabled = true;
	bool bLanded = false;
	double Integrity = 1.0;
	int32 Strikes = 0;
	FRandomStream StrikeDice{ 20260911 };
	FLedgerHeatState Heat;
	FLedgerHeatShield Shield;
	double LastHeatDamage = 0.0;
	FLedgerCanopy Canopy;
	FLedgerShipDefinition ShipDefinition;
	FLedgerSpin Spin;
	FLedgerAllocation LastAllocation;

	// Input state, sampled each frame.
	float ThrottleInput = 0.0f;
	float StrafeInput = 0.0f;
	float LiftInput = 0.0f;
	float PitchInput = 0.0f;
	float YawInput = 0.0f;
	float RollInput = 0.0f;
	float AutoThrottle = 0.0f;
	float AutoLift = 0.0f;
	float AutoStrafe = 0.0f;
	FVector3f AutoTurn = FVector3f::ZeroVector;
	ELedgerFlightMode FlightMode = ELedgerFlightMode::Decoupled;

	void BuildHull();

public:
	/// The hull's geometry, with no component and no actor state involved, so
	/// the mesh bake can produce the same shape as an asset. Static for the same
	/// reason: baking happens before any ship exists.
	static void DescribeHull(FLedgerMeshBuilder& Builder);

private:
	void UpdateSubmersion(float DeltaSeconds);
	void ApplyInput(float DeltaSeconds);

	/// A ship whose file lays out its nozzles: a rigid body pushed by what
	/// the allocator gives each one. T132, T133.
	void ApplyThrust(float DeltaSeconds);
	void Integrate(float DeltaSeconds);
	void SufferWeather(float DeltaSeconds);
	void SufferHeat(float DeltaSeconds);
	void UpdateCanopy(float DeltaSeconds);
	void LoadDefinition();

	void InputThrottle(float Value) { ThrottleInput = Value; }
	void InputStrafe(float Value) { StrafeInput = Value; }
	void InputLift(float Value) { LiftInput = Value; }
	void InputPitch(float Value) { PitchInput = Value; }
	void InputYaw(float Value) { YawInput = Value; }
	void InputRoll(float Value) { RollInput = Value; }
	void InputNextMode() { FlightMode = static_cast<ELedgerFlightMode>((static_cast<uint8>(FlightMode) + 1) % 3); }
};
