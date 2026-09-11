// A ship as a rigid body: rotation with a real inertia tensor. T132.
//
// The translation half lives in LedgerFlightModel. This is the other half:
// orientation and angular velocity, driven by torque through Euler's equations
// with the inertia M05 computes from what the ship is made of. Still
// integrated by hand rather than handed to Chaos, for the reasons in 6.9 --
// and so a test can check the answer against the tensor.

#pragma once

#include "CoreMinimal.h"
#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

struct LEDGERFLIGHT_API FLedgerSpin
{
	/// Body to world.
	FQuat4d Orientation = FQuat4d::Identity;

	/// Radians per second, in the body frame: x forward, y right, z up.
	FVector3d AngularVelocity = FVector3d::ZeroVector;
};

/// What the nozzles were told to do, and what that makes. T133.
struct LEDGERFLIGHT_API FLedgerAllocation
{
	/// Per nozzle, newtons, each between nothing and its limit.
	TArray<double> ThrustNewtons;

	/// The force and torque they make together, in the body frame.
	FVector3d Force = FVector3d::ZeroVector;
	FVector3d Torque = FVector3d::ZeroVector;

	/// Whether that is what was asked, to a hundredth.
	bool bMet = false;
};

/// T134: how the stick is read. Each is a controller over the same allocator
/// and the same rigid body; nothing below the command knows which is flying.
enum class ELedgerFlightMode : uint8
{
	/// The stick is torque and thrust is force: nothing is held.
	AssistOff,
	/// The stick is a turn rate, held; thrust is force, and the ship keeps
	/// whatever velocity it has.
	Decoupled,
	/// The stick is a turn rate, held; and the velocity is held along the
	/// nose -- drift across it is thrusted away unless the stick asks for it.
	Coupled,
};

/// What the pilot asks for: a turn as fractions of the ship rates (pitch,
/// yaw, roll) and a push as fractions of its thrust (forward, right, up).
struct LEDGERFLIGHT_API FLedgerStick
{
	FVector3d Turn = FVector3d::ZeroVector;
	FVector3d Push = FVector3d::ZeroVector;
};

/// What the controllers are told a ship can do.
struct LEDGERFLIGHT_API FLedgerHandling
{
	/// Turn rates at full stick, degrees per second, as the ship file gives them.
	double PitchRate = 55.0;
	double YawRate = 45.0;
	double RollRate = 90.0;

	/// Acceleration at full thrust, metres per second squared.
	double MainAcceleration = 9000.0;
	double ManoeuvringAcceleration = 2600.0;

	/// How fast the assisted modes close on what is asked, per second.
	double RateHoldPerSecond = 8.0;
	double DriftHoldPerSecond = 4.0;
	/// Coupled mode's velocity command, m/s (M5P): full strafe or lift asks for
	/// this across the nose (and full reverse astern), and full throttle for the
	/// second along it; centred asks for rest. Without
	/// them a held key accelerated for as long as it was held, and a flat climb
	/// at 90 m/s put enough air on the belly to flip the ship.
	double CoupledSideSpeed = 40.0;
	double CoupledForwardSpeed = 300.0;

	static FLedgerHandling From(const FLedgerShipFlight& Flight)
	{
		FLedgerHandling Out;
		Out.PitchRate = Flight.PitchRate;
		Out.YawRate = Flight.YawRate;
		Out.RollRate = Flight.RollRate;
		Out.MainAcceleration = Flight.MainThrust;
		Out.ManoeuvringAcceleration = Flight.ManoeuvringThrust;
		return Out;
	}
};

/// A ship in flight: its rotation, and its velocity in the world frame,
/// metres per second.
struct LEDGERFLIGHT_API FLedgerMotion
{
	FLedgerSpin Spin;
	FVector3d Velocity = FVector3d::ZeroVector;
};

/// The force and torque asked of the allocator, body frame.
struct LEDGERFLIGHT_API FLedgerCommand
{
	FVector3d Force = FVector3d::ZeroVector;
	FVector3d Torque = FVector3d::ZeroVector;
};

/// What the air is doing to a ship now, for the panel and the tests. T135.
struct LEDGERFLIGHT_API FLedgerAeroState
{
	/// Radians: the nose above the path through the air, and the path to the right of the nose.
	double AngleOfAttack = 0.0;
	double Sideslip = 0.0;

	/// Half rho v squared, pascals.
	double DynamicPressure = 0.0;

	/// The wing's lift and drag coefficients.
	double Lift = 0.0;
	double Drag = 0.0;
	bool bStalled = false;
};

namespace LedgerFlight
{
	/// The torque about the centre of mass of a force applied at a point, all in
	/// the body frame: r cross F, with r from the centre of mass to the point.
	LEDGERFLIGHT_API FVector3d TorqueOf(const FVector3d& PointMetres, const FVector3d& ForceNewtons,
		const FVector3d& CentreMetres);

	/// Angular acceleration now: I^-1 (torque - omega x I omega). The second
	/// term is what makes a spinning body precess, and what a rate-based
	/// rotation leaves out.
	LEDGERFLIGHT_API FVector3d AngularAcceleration(const double Inertia[3][3], const FVector3d& AngularVelocity,
		const FVector3d& TorqueBody);

	/// One step of rotation under a body-frame torque: fourth-order
	/// Runge-Kutta on the angular velocity, and the orientation carried along.
	LEDGERFLIGHT_API void Rotate(FLedgerSpin& Spin, const double Inertia[3][3], const FVector3d& TorqueBody, double DeltaSeconds);

	/// Angular momentum (kg m2/s) and rotational energy (J), for the checks
	/// that a torque-free body keeps both.
	LEDGERFLIGHT_API FVector3d AngularMomentum(const double Inertia[3][3], const FVector3d& AngularVelocity);
	LEDGERFLIGHT_API double RotationalEnergy(const double Inertia[3][3], const FVector3d& AngularVelocity);

	/// T133: the thrust on each nozzle that comes closest to a commanded force
	/// and torque about the centre of mass, each nozzle between nothing and its
	/// limit. Closest in least squares with torque weighted over force -- a
	/// ship that cannot do both keeps its attitude first -- and among exact
	/// answers, the one that burns least. So handling is layout: move a nozzle
	/// and the torque the ship can make moves with it.
	LEDGERFLIGHT_API FLedgerAllocation Allocate(const TArray<FLedgerNozzle>& Nozzles, const TArray<double>& LimitNewtons,
		const FVector3d& CentreMetres, const FVector3d& ForceNewtons, const FVector3d& TorqueNewtonMetres);

	/// T134, the controller: a mode reading the stick, as the force and
	/// torque to ask for.
	LEDGERFLIGHT_API FLedgerCommand Control(ELedgerFlightMode Mode, const FLedgerStick& Stick, const FLedgerHandling& Handling,
		const FLedgerMassProperties& Mass, const FLedgerMotion& State, double DeltaSeconds,
		const FVector3d& ExternalTorque = FVector3d::ZeroVector);

	/// T134, the physics: the allocator gives what it can of a command, and
	/// the body turns and speeds up by what that makes. It is not told the
	/// mode, which is what makes switching modes change nothing below here.
	LEDGERFLIGHT_API FLedgerAllocation Push(const TArray<FLedgerNozzle>& Nozzles, const TArray<double>& LimitNewtons,
		const FLedgerMassProperties& Mass, const FLedgerCommand& Command, FLedgerMotion& State, double DeltaSeconds,
		const FLedgerCommand& External = FLedgerCommand());

	/// T135: the air on a ship, body frame, from its speed through air of a
	/// density. The body is a flat plate across the flow -- belly-first it is
	/// the whole of the entry drag the ship file states (BellyAreaM2 is its
	/// mass over that ballistic coefficient), nose-first it is nothing -- and a
	/// wing, if it has one, lifts and drags off its angle of attack up to the
	/// stall, weathercocks, damps its turns, and answers its control surfaces
	/// (pitch, yaw, roll, as the stick gives them). Nothing at all in vacuum.
	LEDGERFLIGHT_API FLedgerCommand Aerodynamics(const FLedgerShipAero& Aero, double BellyAreaM2, const FLedgerMotion& Motion,
		const FVector3d& WindMetresPerSecond, double DensityKgPerM3, const FVector3d& Surfaces, FLedgerAeroState* OutState = nullptr);
}
