// Aurora: a body's magnetic field, its star's activity, and the oval of light
// where the two meet. T103.
//
// **A function of time, like the weather.** The star's activity is a schedule
// of events -- a birth, a peak and a decay each -- so the aurora on a night
// next month is arithmetic, and the same for everybody who asks. The oval sits
// around the magnetic pole, not the rotational one, and widens towards the
// equator as the activity rises: that is the whole of why an aurora is seen
// further south in a big storm.

#pragma once

#include "CoreMinimal.h"

#include "LedgerAir.h"
#include "LedgerBody.h"

struct FLedgerAurora
{
	/// The star's geomagnetic activity, on the Kp scale: 0 quiet to 9 the
	/// worst storm there is.
	double Kp = 0.0;

	/// Whether a solar event is under way.
	bool bEvent = false;

	/// The oval: the magnetic latitude of its centre line and its half-width,
	/// radians.
	double OvalMagneticLatitude = 0.0;
	double OvalHalfWidth = 0.0;

	/// How bright the oval is, 0 to 1. Zero outside an event.
	double Strength = 0.0;

	/// Brightness overhead at the place asked about, 0 to 1, and that place's
	/// magnetic latitude, radians.
	double Overhead = 0.0;
	double MagneticLatitude = 0.0;
};

namespace LedgerAurora
{
	/// The body's magnetic dipole against Earth's.
	///
	/// ponytail: the magnetic Bode's law -- moment goes as angular momentum --
	/// with a working dynamo assumed wherever there is enough body to hold one.
	/// Stars, asteroids and stations have none.
	LEDGERCORE_API double DipoleRelative(const FLedgerBody& Body);

	/// The northern magnetic pole, a unit direction in the body's frame: tilted
	/// off the rotation axis by a few degrees, as Earth's is.
	LEDGERCORE_API FVector3d MagneticPole(const FLedgerSystem& System, int32 BodyIndex);

	/// The star's activity at a time, on the Kp scale.
	LEDGERCORE_API double ActivityKp(const FLedgerSystem& System, double SecondsFromEpoch,
		bool* bOutEvent = nullptr);

	/// Everything, at a place and a time.
	LEDGERCORE_API FLedgerAurora At(const FLedgerSystem& System, int32 BodyIndex,
		const FLedgerAirProfile& Air, double LatitudeRadians, double LongitudeRadians,
		double SecondsFromEpoch);
}
