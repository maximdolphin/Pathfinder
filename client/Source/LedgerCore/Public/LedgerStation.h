// A structure on rails, and the frame things dock to. T082.
//
// A station is a body: it has a parent, an orbit, and the ephemeris puts it
// where it belongs like everything else. What it needs on top is an ATTITUDE,
// because a station is not a sphere and "which way is up on it" is a question a
// planet never has to answer.
//
// **The frame is the orbit's, not the stars'.** A real station holds one face
// to the body it orbits -- that is where the airlocks point, where the docking
// ports are, and what "down" means to somebody inside. So the frame is built
// from the position and velocity the ephemeris already gives: nadir towards the
// parent, forward along the track, and the third axis from those two. It is the
// local-vertical local-horizontal frame that every real spacecraft uses, and it
// costs nothing extra because both vectors are already computed.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"

/// A point fixed to a station: a docking port, a handrail, a docked ship.
struct FLedgerStationPoint
{
	int32 StationIndex = INDEX_NONE;

	/// Metres in the station's own frame: forward, left, up.
	FVector3d Metres = FVector3d::ZeroVector;
};

namespace LedgerStation
{
	/// The station's attitude at a time, as a rotation from its own frame into
	/// the system frame.
	///
	/// Up is away from the parent, forward is along the track. Undefined only
	/// for a station that is not moving relative to its parent, which is not an
	/// orbit.
	LEDGERCORE_API FQuat4d Orientation(
		const FLedgerSystem& System, int32 StationIndex, double SecondsFromEpoch);

	/// Where a point fixed to the station is, in the system frame.
	LEDGERCORE_API FVector3d ToSystem(
		const FLedgerSystem& System, const FLedgerStationPoint& Point,
		double SecondsFromEpoch);

	/// And back, which is how a thing checks it is still where it was docked.
	LEDGERCORE_API FLedgerStationPoint ToStation(
		const FLedgerSystem& System, const FVector3d& SystemMetres,
		int32 StationIndex, double SecondsFromEpoch);
}
