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
#include "LedgerShip.generated.h"

class ALedgerPlanet;
class UCameraComponent;
class UProceduralMeshComponent;
class USpringArmComponent;

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

	/// While true the ship does not integrate — something else is placing it.
	/// Used by the scripted reentry so the two do not fight over the transform.
	void SetFlightEnabled(bool bEnabled) { bFlightEnabled = bEnabled; }
	bool IsFlightEnabled() const { return bFlightEnabled; }

	void SetVelocity(const FVector& InVelocity) { Velocity = InVelocity; }

	/// Throttle applied on top of player input, in [0,1]. The scripted ascent
	/// uses this so the climb to orbit runs through the *same* flight model the
	/// player flies — gravity, drag and all — rather than being animated.
	void SetAutoThrottle(float Fraction) { AutoThrottle = FMath::Clamp(Fraction, 0.0f, 1.0f); }
	FVector GetVelocity() const override { return Velocity; }

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

	UPROPERTY()
	TObjectPtr<ALedgerPlanet> Planet;

	FVector Velocity = FVector::ZeroVector;
	bool bFlightEnabled = true;
	bool bLanded = false;

	// Input state, sampled each frame.
	float ThrottleInput = 0.0f;
	float StrafeInput = 0.0f;
	float LiftInput = 0.0f;
	float PitchInput = 0.0f;
	float YawInput = 0.0f;
	float RollInput = 0.0f;
	float AutoThrottle = 0.0f;

	void BuildHull();
	void ApplyInput(float DeltaSeconds);
	void Integrate(float DeltaSeconds);

	void InputThrottle(float Value) { ThrottleInput = Value; }
	void InputStrafe(float Value) { StrafeInput = Value; }
	void InputLift(float Value) { LiftInput = Value; }
	void InputPitch(float Value) { PitchInput = Value; }
	void InputYaw(float Value) { YawInput = Value; }
	void InputRoll(float Value) { RollInput = Value; }
};
