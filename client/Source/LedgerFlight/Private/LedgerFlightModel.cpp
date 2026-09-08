#include "LedgerFlightModel.h"

namespace LedgerFlight
{
	double AltitudeAbove(const FLedgerFlightState& State, const FLedgerGravityField& Field)
	{
		const FVector3d Radial = State.Position - Field.Centre;
		const double Distance = Radial.Length();
		if (Distance <= 0.0)
		{
			return 0.0;
		}
		return Distance - Field.GroundAt(Radial / Distance);
	}

	void Integrate(FLedgerFlightState& State, const FLedgerGravityField& Field, double DeltaSeconds)
	{
		if (DeltaSeconds <= 0.0)
		{
			return;
		}

		const FVector3d Radial = State.Position - Field.Centre;
		const double Distance = FMath::Max(Radial.Length(), 1.0);
		const FVector3d Up = Radial / Distance;

		// Inverse square, so leaving is expensive near the ground and cheap once
		// you are up. Newton, not a constant.
		const double GravityHere = Field.SurfaceGravity * FMath::Square(Field.Radius / Distance);
		State.Velocity -= Up * GravityHere * DeltaSeconds;

		// Drag, exponential in altitude. Above a few scale heights this is zero
		// and the ship coasts; below it, the air is something you feel.
		const double Altitude = Distance - Field.GroundAt(Up);
		if (Altitude < Field.DragScaleHeight * 6.0)
		{
			const double Density = FMath::Exp(-FMath::Max(Altitude, 0.0) / Field.DragScaleHeight);
			const double Damping = FMath::Clamp(
				1.0 - Field.AtmosphericDrag * Density * DeltaSeconds, 0.0, 1.0);
			State.Velocity *= Damping;
		}

		FVector3d NewPosition = State.Position + State.Velocity * DeltaSeconds;

		// Ground contact against the *surface height function*, not a trace.
		//
		// A ray against the streaming collision would miss whenever the patch
		// underneath has not cooked yet — which is exactly when a ship is moving
		// fast enough to need the answer. The height function is the same one
		// the mesh was built from, it is always available, and it cannot
		// disagree with the geometry.
		const FVector3d NewRadial = NewPosition - Field.Centre;
		const double NewDistance = FMath::Max(NewRadial.Length(), 1.0);
		const FVector3d NewUp = NewRadial / NewDistance;
		const double GroundRadius = Field.GroundAt(NewUp) + State.GearHeight;

		if (NewDistance < GroundRadius)
		{
			NewPosition = Field.Centre + NewUp * GroundRadius;

			// Kill the component of velocity into the ground, keep the rest,
			// and scrub the remainder off as friction.
			const double Into = FVector3d::DotProduct(State.Velocity, NewUp);
			if (Into < 0.0)
			{
				State.Velocity -= NewUp * Into;
			}
			State.Velocity *= State.GroundFriction;
			State.bLanded = true;
		}
		else if (NewDistance > GroundRadius + 500.0)
		{
			// A margin, not a threshold: at exactly the contact radius the
			// landed flag would flicker every frame the ship settles.
			State.bLanded = false;
		}

		State.Position = NewPosition;
	}
}
