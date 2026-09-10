// The four frames everything positional lives in. T071.
//
//   system    metres from the primary's centre, axes fixed against the stars.
//             The ephemeris speaks this and so does the map.
//   body      metres from a body's centre, ROTATING with it. A mountain has one
//             body-frame position forever; its system-frame position changes
//             every second.
//   surface   metres, east-north-up at a point on a body. What a person standing
//             somewhere means by "forward" and "down".
//   vehicle   metres from a vehicle's origin, along its own axes.
//
// **The distinction that matters is system against body.** They differ by the
// body's rotation, which means a position that forgets to say which one it is in
// is wrong by up to a planet's circumference, and wrong by a different amount
// every second. Getting that wrong late is what the task means by unaffordable,
// so the types are separate and there is no implicit conversion between them.
//
// Everything is in metres and radians, in double precision, and none of it
// knows what a centimetre is. The renderer's units are the renderer's business.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"

/// A point in the system frame: metres from the primary, axes fixed against the
/// stars.
struct FLedgerSystemPoint
{
	FVector3d Metres = FVector3d::ZeroVector;
};

/// A point in a body's rotating frame: metres from its centre, turning with it.
struct FLedgerBodyPoint
{
	/// Which body. An index into the system's array, because a body pointer
	/// would let a point outlive the system that gives it meaning.
	int32 BodyIndex = INDEX_NONE;
	FVector3d Metres = FVector3d::ZeroVector;
};

/// A point in the local east-north-up frame at a place on a body.
struct FLedgerSurfacePoint
{
	int32 BodyIndex = INDEX_NONE;

	/// Where the frame is anchored, as a unit direction in the body frame.
	/// Not a latitude and longitude: those have a singularity at the poles and
	/// this is used at the poles.
	FVector3d AnchorDirection = FVector3d::UnitZ();

	/// East, north and up, metres.
	FVector3d Metres = FVector3d::ZeroVector;
};

namespace LedgerFrames
{
	/// The rotation of a body about its own axis at a time, as a quaternion in
	/// the system frame.
	///
	/// Tilt is applied first and the spin second, which is the order that makes
	/// axial tilt mean what it says: the axis leans away from the orbit's normal
	/// and the body turns about the leaned axis, rather than the lean being
	/// carried around by the spin.
	LEDGERCORE_API FQuat4d BodyOrientation(const FLedgerBody& Body, double SecondsFromEpoch);

	/// The body's rotation axis in the system frame.
	LEDGERCORE_API FVector3d BodyAxis(const FLedgerBody& Body, double SecondsFromEpoch);

	LEDGERCORE_API FLedgerSystemPoint ToSystem(
		const FLedgerSystem& System, const FLedgerBodyPoint& Point, double SecondsFromEpoch);

	LEDGERCORE_API FLedgerBodyPoint ToBody(
		const FLedgerSystem& System, const FLedgerSystemPoint& Point,
		int32 BodyIndex, double SecondsFromEpoch);

	/// East-north-up at an anchor into that body's rotating frame.
	///
	/// Up is the anchor direction. North is whatever component of the rotation
	/// axis is perpendicular to up, so north points along the ground towards the
	/// pole -- and at the pole itself, where that is undefined, it falls back to
	/// a fixed reference so the frame is still a frame rather than a division by
	/// zero.
	LEDGERCORE_API FLedgerBodyPoint ToBody(const FLedgerSurfacePoint& Point);

	LEDGERCORE_API FLedgerSurfacePoint ToSurface(
		const FLedgerBodyPoint& Point, const FVector3d& AnchorDirection);
}
