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
}
