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

/// Another body as it appears from somewhere. T074.
struct FLedgerSkyBody
{
	int32 BodyIndex = INDEX_NONE;

	/// Where to look, as a unit vector in the observer's east-north-up frame.
	/// Z above zero is above the horizon.
	FVector3d DirectionInSurface = FVector3d::ZeroVector;

	/// Half the angle the disc subtends, radians.
	double AngularRadiusRadians = 0.0;

	/// The angle at the target between the star and the observer. Zero is
	/// full, pi is new, and it is the only thing the lit fraction depends on.
	double PhaseAngleRadians = 0.0;

	/// How much of the visible disc is lit, 0 to 1.
	double IlluminatedFraction = 0.0;

	double DistanceMetres = 0.0;
};

namespace LedgerSky
{
	/// The angle at a target between the star and an observer, radians. T074.
	///
	/// **Everything about a phase is this one angle.** A crescent is not a
	/// property of a moon or of a time of night; it is the observer, the moon
	/// and the star not being in a line, and how far from a line they are.
	LEDGERCORE_API double PhaseAngle(
		const FLedgerSystem& System, int32 TargetIndex,
		const FVector3d& ObserverPositionMetres, double SecondsFromEpoch);

	/// How much of the visible disc is lit, from the phase angle alone.
	///
	/// (1 + cos a) / 2. Exact for a sphere lit by a point source and seen from
	/// far enough away that the terminator projects to a half-ellipse, which is
	/// every case in a solar system and none in a close-up.
	LEDGERCORE_API double IlluminatedFraction(double PhaseAngleRadians);

	/// Where an observer standing at an anchor on a body is, in system metres.
	LEDGERCORE_API FVector3d ObserverPosition(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch);

	/// Every other body in the system as it appears from a place, sorted
	/// brightest-looking first (largest disc times lit fraction).
	///
	/// The observer is a point on a body's surface rather than its centre,
	/// which matters for a moon: at this system's distances the parallax
	/// between the two is a measurable fraction of the moon's own disc.
	LEDGERCORE_API void VisibleBodies(
		const FLedgerSystem& System, int32 ObserverBodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch,
		TArray<FLedgerSkyBody>& Out);

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

	/// What the star puts out, watts. T076.
	///
	/// From its mass, by the main-sequence mass-luminosity relation. A star's
	/// mass is the one thing that decides nearly everything else about it, and
	/// it is already in the description -- so nothing new is stored and a
	/// system read from disk cannot carry a luminosity that disagrees with the
	/// body it belongs to.
	LEDGERCORE_API double StarLuminosityWatts(const FLedgerBody& Star);

	/// Its surface temperature, kelvin, from the luminosity and the radius.
	///
	/// Stefan-Boltzmann backwards: L = 4 pi R^2 sigma T^4, solved for T. This
	/// is what the light's colour comes from, so a small red star and a large
	/// blue one light their planets differently without anybody choosing a
	/// colour.
	LEDGERCORE_API double StarTemperatureKelvin(const FLedgerBody& Star);

	/// Illuminance on a surface facing the star, lux. T076.
	///
	/// **Falls off as one over distance squared, because it is computed that
	/// way and not because it was tuned to.** An inner planet is brighter and
	/// an outer one dimmer by the same arithmetic that makes a lamp dimmer
	/// across a room.
	LEDGERCORE_API double IlluminanceLux(
		const FLedgerSystem& System, const FVector3d& ObserverPositionMetres,
		double SecondsFromEpoch);

	/// The same number by a different road: surface flux times the solid angle
	/// the disc subtends.
	///
	/// sigma T^4 sin^2(angular radius). It goes through the star's APPARENT
	/// SIZE rather than its distance, which is the quantity the renderer draws,
	/// so agreement between the two is a check that the picture and the
	/// photometry are describing one star.
	LEDGERCORE_API double IlluminanceFromDisc(
		double TemperatureKelvin, double AngularRadiusRadians);

	/// How much of the star's disc is hidden by another body, 0 to 1. T075.
	///
	/// An eclipse is two circles on the sky overlapping, so this is the area
	/// they share over the star's own area. It counts only bodies actually
	/// between the observer and the star -- a moon on the far side lines up
	/// just as often and hides nothing.
	///
	/// Zero when the star is below the horizon: a thing nobody can see is not
	/// darkening anybody's ground.
	LEDGERCORE_API double StarCoveredFraction(
		const FLedgerSystem& System, int32 ObserverBodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch);

	/// What is covering it, or INDEX_NONE.
	LEDGERCORE_API int32 EclipsingBody(
		const FLedgerSystem& System, int32 ObserverBodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch);

	/// When the next eclipse is, searching forward. T075.
	///
	/// Returns the moment of greatest coverage, or a negative number if nothing
	/// is found inside the span. The prediction is the acceptance: a time comes
	/// out of the ephemeris, and then somebody stands there and looks.
	LEDGERCORE_API double NextEclipse(
		const FLedgerSystem& System, int32 ObserverBodyIndex,
		const FVector3d& AnchorDirection, double AfterSeconds, double SpanSeconds,
		double& OutPeakCoverage);

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
