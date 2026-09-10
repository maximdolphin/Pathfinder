#include "LedgerStation.h"

#include "LedgerEphemeris.h"
#include "LedgerMath.h"

namespace
{
	/// The station's axes in the system frame: forward, left, up.
	///
	/// Built from the position and velocity relative to the PARENT, not to the
	/// primary. A station's "down" is the planet it orbits; taking the velocity
	/// in the primary's frame would give a frame that spun once a year for no
	/// reason anybody aboard could feel.
	bool AxesFor(
		const FLedgerSystem& System, int32 StationIndex, double SecondsFromEpoch,
		FVector3d& OutForward, FVector3d& OutLeft, FVector3d& OutUp)
	{
		if (!System.Bodies.IsValidIndex(StationIndex))
		{
			return false;
		}
		const FLedgerBody& Station = System.Bodies[StationIndex];
		const int32 Parent = Station.ParentIndex;
		if (!System.Bodies.IsValidIndex(Parent))
		{
			return false;
		}

		const double ParentMass = System.Bodies[Parent].MassKg;
		const FLedgerState Relative =
			LedgerEphemeris::StateAt(Station, ParentMass, SecondsFromEpoch);

		OutUp = Relative.PositionMetres.GetSafeNormal();
		if (OutUp.IsNearlyZero())
		{
			return false;
		}

		// Forward is the part of the velocity perpendicular to up. On a
		// circular orbit that is the whole of it; on an eccentric one the
		// radial component is what makes the station climb and fall, and it
		// belongs to up rather than to forward.
		FVector3d Forward = Relative.VelocityMetresPerSecond
			- OutUp * FVector3d::DotProduct(Relative.VelocityMetresPerSecond, OutUp);
		if (Forward.SquaredLength() < 1e-18)
		{
			return false;
		}
		OutForward = Forward.GetSafeNormal();
		OutLeft = FVector3d::CrossProduct(OutUp, OutForward).GetSafeNormal();
		return true;
	}
}

namespace LedgerStation
{
	FQuat4d Orientation(
		const FLedgerSystem& System, int32 StationIndex, double SecondsFromEpoch)
	{
		FVector3d Forward, Left, Up;
		if (!AxesFor(System, StationIndex, SecondsFromEpoch, Forward, Left, Up))
		{
			return FQuat4d::Identity;
		}

		// Columns are where the station's own x, y and z end up.
		FMatrix Basis(
			FPlane(Forward.X, Forward.Y, Forward.Z, 0.0),
			FPlane(Left.X, Left.Y, Left.Z, 0.0),
			FPlane(Up.X, Up.Y, Up.Z, 0.0),
			FPlane(0.0, 0.0, 0.0, 1.0));
		return FQuat4d(Basis.ToQuat());
	}

	FVector3d ToSystem(
		const FLedgerSystem& System, const FLedgerStationPoint& Point,
		double SecondsFromEpoch)
	{
		FVector3d Forward, Left, Up;
		if (!AxesFor(System, Point.StationIndex, SecondsFromEpoch, Forward, Left, Up))
		{
			return FVector3d::ZeroVector;
		}

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		// The axes directly rather than through the quaternion, so a rounding
		// difference in the quaternion cannot make the round trip disagree with
		// itself. Orientation() exists for things that need a rotation; this is
		// what a docked point uses.
		return States[Point.StationIndex].PositionMetres
			+ Forward * Point.Metres.X + Left * Point.Metres.Y + Up * Point.Metres.Z;
	}

	FLedgerStationPoint ToStation(
		const FLedgerSystem& System, const FVector3d& SystemMetres,
		int32 StationIndex, double SecondsFromEpoch)
	{
		FLedgerStationPoint Point;
		Point.StationIndex = StationIndex;

		FVector3d Forward, Left, Up;
		if (!AxesFor(System, StationIndex, SecondsFromEpoch, Forward, Left, Up))
		{
			return Point;
		}

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);
		const FVector3d Offset = SystemMetres - States[StationIndex].PositionMetres;

		Point.Metres = FVector3d(
			FVector3d::DotProduct(Offset, Forward),
			FVector3d::DotProduct(Offset, Left),
			FVector3d::DotProduct(Offset, Up));
		return Point;
	}
}
