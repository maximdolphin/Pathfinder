#include "LedgerFrames.h"

#include "LedgerEphemeris.h"

namespace
{
	/// East, north and up at an anchor on a body whose axis is known.
	///
	/// Up is the anchor. North is the part of the axis perpendicular to up,
	/// which is the direction "towards the pole along the ground". At the pole
	/// that is undefined -- the axis and the anchor are parallel -- and the
	/// fallback is a fixed reference so the frame stays a frame. A caller
	/// standing exactly on a pole gets an arbitrary but consistent north, which
	/// is the same thing every map projection does and for the same reason.
	void SurfaceBasis(const FVector3d& Anchor, const FVector3d& Axis,
		FVector3d& OutEast, FVector3d& OutNorth, FVector3d& OutUp)
	{
		OutUp = Anchor.GetSafeNormal();
		if (OutUp.IsNearlyZero())
		{
			OutUp = FVector3d::UnitZ();
		}

		FVector3d North = Axis - OutUp * FVector3d::DotProduct(Axis, OutUp);
		if (North.SquaredLength() < 1e-18)
		{
			// On the axis. Any perpendicular will do, as long as it is the same
			// one every time.
			const FVector3d Reference = FMath::Abs(OutUp.X) < 0.9
				? FVector3d::UnitX() : FVector3d::UnitY();
			North = Reference - OutUp * FVector3d::DotProduct(Reference, OutUp);
		}
		OutNorth = North.GetSafeNormal();
		OutEast = FVector3d::CrossProduct(OutNorth, OutUp).GetSafeNormal();
	}
}

namespace LedgerFrames
{
	FQuat4d BodyOrientation(const FLedgerBody& Body, double SecondsFromEpoch)
	{
		// Tilt first, spin second.
		//
		// The other order tilts the already-spun body, which carries the lean
		// around with the rotation -- a planet whose axis wanders in a circle
		// once a day instead of pointing steadily at one place in the sky.
		// Seasons (T073) come out of this being the right way round.
		const FQuat4d Tilt(FVector3d::UnitX(), Body.AxialTiltRadians);
		const FVector3d Axis = Tilt.RotateVector(FVector3d::UnitZ());
		const FQuat4d Spin(Axis, LedgerEphemeris::RotationAt(Body, SecondsFromEpoch));
		return Spin * Tilt;
	}

	FVector3d BodyAxis(const FLedgerBody& Body, double SecondsFromEpoch)
	{
		// Independent of the spin, by construction: turning about an axis does
		// not move the axis. Computed from the tilt alone rather than by
		// rotating a vector through the full orientation, because the latter
		// is the same number arrived at with more chances to be wrong.
		const FQuat4d Tilt(FVector3d::UnitX(), Body.AxialTiltRadians);
		return Tilt.RotateVector(FVector3d::UnitZ());
	}

	FLedgerSystemPoint ToSystem(
		const FLedgerSystem& System, const FLedgerBodyPoint& Point, double SecondsFromEpoch)
	{
		FLedgerSystemPoint Out;
		if (!System.Bodies.IsValidIndex(Point.BodyIndex))
		{
			return Out;
		}

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		const FLedgerBody& Body = System.Bodies[Point.BodyIndex];
		const FQuat4d Orientation = BodyOrientation(Body, SecondsFromEpoch);
		Out.Metres = States[Point.BodyIndex].PositionMetres
			+ Orientation.RotateVector(Point.Metres);
		return Out;
	}

	FLedgerBodyPoint ToBody(
		const FLedgerSystem& System, const FLedgerSystemPoint& Point,
		int32 BodyIndex, double SecondsFromEpoch)
	{
		FLedgerBodyPoint Out;
		Out.BodyIndex = BodyIndex;
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return Out;
		}

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		const FLedgerBody& Body = System.Bodies[BodyIndex];
		const FQuat4d Orientation = BodyOrientation(Body, SecondsFromEpoch);
		Out.Metres = Orientation.UnrotateVector(
			Point.Metres - States[BodyIndex].PositionMetres);
		return Out;
	}

	FLedgerBodyPoint ToBody(const FLedgerSurfacePoint& Point)
	{
		FLedgerBodyPoint Out;
		Out.BodyIndex = Point.BodyIndex;

		// The surface frame is anchored in the BODY frame, so the body's axis
		// here is the untilted, unspun +Z: the tilt and the spin are what carry
		// the whole body frame into the system, and applying them twice would
		// rotate the ground relative to the planet it is on.
		FVector3d East, North, Up;
		SurfaceBasis(Point.AnchorDirection, FVector3d::UnitZ(), East, North, Up);

		Out.Metres = East * Point.Metres.X + North * Point.Metres.Y + Up * Point.Metres.Z;
		return Out;
	}

	FLedgerSurfacePoint ToSurface(const FLedgerBodyPoint& Point, const FVector3d& AnchorDirection)
	{
		FLedgerSurfacePoint Out;
		Out.BodyIndex = Point.BodyIndex;
		Out.AnchorDirection = AnchorDirection.GetSafeNormal();

		FVector3d East, North, Up;
		SurfaceBasis(Out.AnchorDirection, FVector3d::UnitZ(), East, North, Up);

		Out.Metres = FVector3d(
			FVector3d::DotProduct(Point.Metres, East),
			FVector3d::DotProduct(Point.Metres, North),
			FVector3d::DotProduct(Point.Metres, Up));
		return Out;
	}
}
