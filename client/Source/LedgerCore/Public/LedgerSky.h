// Where the star is in a local sky, and when noon is. T072.
//
// **There is no time-of-day slider and there is not going to be one.** The sun's
// place is a consequence of two things already in the model: where the body is
// in its orbit, and how far it has turned since the epoch. A slider is a fourth
// independent variable that can disagree with both, and the moment it does, the
// sky stops being evidence about anything.
//
// The star is the system's primary. Its position comes from the ephemeris like
// every other body's, which matters for a moon: a moon's sun direction is the
// star minus the moon, not the star minus the planet, and at a moon's distance
// those differ by enough to move a terminator.
//
// Angles in radians, times in seconds from the system's epoch.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"

namespace LedgerSky
{
	/// Unit vector towards the star, in the body's ROTATING frame.
	///
	/// Body frame rather than system frame because that is the frame a place on
	/// the ground is fixed in, so this is the vector that sweeps once a day
	/// while the ground stays put.
	LEDGERCORE_API FVector3d SunDirectionInBody(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch);

	/// Unit vector towards the star in east-north-up at an anchor.
	LEDGERCORE_API FVector3d SunDirectionInSurface(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch);

	/// How far above the local horizon the star is, radians. Negative is night.
	LEDGERCORE_API double SolarAltitude(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch);

	/// The hour angle: how far the body has turned past pointing this place at
	/// the star, wrapped to [-pi, pi]. Zero is local noon by definition.
	///
	/// Measured about the body's rotation axis, which in the body frame is +Z --
	/// the tilt and the spin are what carry that frame into the system, so
	/// inside it the axis does not move. That is what makes this a subtraction
	/// of two azimuths rather than a spherical triangle.
	LEDGERCORE_API double HourAngle(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch);

	/// How far the star is above the body's equator, radians. T073.
	///
	/// This is what the axial tilt is *for*. A body with no tilt has a
	/// declination of zero all year and no seasons at all; the tilt is the
	/// amplitude the declination swings through, and everything seasonal --
	/// day length, insolation, the snow line -- is downstream of it.
	LEDGERCORE_API double SolarDeclination(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch);

	/// One solar day: the interval between consecutive local noons.
	///
	/// Not the rotation period. A body turning once also moves along its orbit,
	/// so it must turn slightly further to face the star again -- the difference
	/// between a sidereal day and a solar one, which is a day a year on Earth
	/// and rather more on a tidally locked moon.
	LEDGERCORE_API double SolarDaySeconds(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch);

	/// How long the star is above the horizon at a place, seconds.
	///
	/// Zero through a polar night, a whole solar day through a polar summer, and
	/// the acceptance for T073 is that a high-latitude site measurably differs
	/// between the two.
	LEDGERCORE_API double DayLengthSeconds(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch);

	/// Where in its year the body is, 0 to 1, measured from the moment its
	/// northern hemisphere is tilted furthest towards the star.
	///
	/// Phase rather than a date because the climate field wants a cycle, and
	/// because a calendar would be borrowing Earth's.
	LEDGERCORE_API double SeasonPhase(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch);

	/// Mean insolation over one day, from a latitude and a declination alone.
	///
	/// The primitive the whole seasonal chain rests on, taking two angles and
	/// no system, so the climate field can call it per sample without carrying
	/// an ephemeris onto a worker thread -- and so there is one integral rather
	/// than two that have to agree.
	LEDGERCORE_API double DailyInsolationAt(
		double LatitudeRadians, double DeclinationRadians);

	/// Mean insolation over one day at a place, as a fraction of what the same
	/// body would receive with the star straight overhead all day.
	///
	/// The standard daily-insolation integral: how high the star gets, for how
	/// long, both of which the declination sets. This is the one number the
	/// climate field needs from the sky, which is why it is a fraction rather
	/// than a flux -- distance and luminosity belong to T076.
	LEDGERCORE_API double DailyInsolation(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch);

	/// The first local noon at or after a time: the next moment the hour angle
	/// is zero.
	///
	/// Solved rather than sampled. The hour angle runs down at very close to the
	/// rotation rate -- the orbit contributes the difference between a sidereal
	/// day and a solar one -- so a secant iteration on the unwrapped angle
	/// converges in a handful of steps from a first guess that is just the
	/// current angle divided by that rate.
	LEDGERCORE_API double NextLocalNoon(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double AfterSeconds);
}
