#include "LedgerSky.h"

#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
#include "LedgerMath.h"

namespace
{
	/// Wraps to [-pi, pi].
	double Wrap(double Radians)
	{
		double Wrapped = FMath::Fmod(Radians, LedgerTwoPi);
		if (Wrapped > LedgerPi) { Wrapped -= LedgerTwoPi; }
		if (Wrapped < -LedgerPi) { Wrapped += LedgerTwoPi; }
		return Wrapped;
	}

	/// Angle around the rotation axis, which in the body frame is +Z.
	double Azimuth(const FVector3d& V)
	{
		return FMath::Atan2(V.Y, V.X);
	}

	/// The primary: the body that orbits nothing. Index 0 by convention, and
	/// this does not rely on the convention, because a system read from disk is
	/// checked rather than trusted.
	int32 PrimaryIndex(const FLedgerSystem& System)
	{
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			if (System.Bodies[Index].ParentIndex == INDEX_NONE)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

namespace LedgerSky
{
	FVector3d SunDirectionInBody(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch)
	{
		const int32 Star = PrimaryIndex(System);
		if (!System.Bodies.IsValidIndex(BodyIndex) || Star == INDEX_NONE || Star == BodyIndex)
		{
			return FVector3d::ZeroVector;
		}

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		// Star minus body, not star minus the body's parent. For a moon those
		// differ by the moon's orbit, which is the whole reason a moon has its
		// own terminator rather than sharing its planet's.
		const FVector3d ToStar =
			States[Star].PositionMetres - States[BodyIndex].PositionMetres;

		const FQuat4d Orientation =
			LedgerFrames::BodyOrientation(System.Bodies[BodyIndex], SecondsFromEpoch);
		return Orientation.UnrotateVector(ToStar).GetSafeNormal();
	}

	FVector3d SunDirectionInSurface(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch)
	{
		FLedgerBodyPoint InBody;
		InBody.BodyIndex = BodyIndex;
		InBody.Metres = SunDirectionInBody(System, BodyIndex, SecondsFromEpoch);
		return LedgerFrames::ToSurface(InBody, AnchorDirection).Metres;
	}

	double SolarAltitude(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch)
	{
		const FVector3d Sun =
			SunDirectionInSurface(System, BodyIndex, AnchorDirection, SecondsFromEpoch);
		return FMath::Asin(FMath::Clamp(Sun.Z, -1.0, 1.0));
	}

	double HourAngle(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch)
	{
		const FVector3d Sun = SunDirectionInBody(System, BodyIndex, SecondsFromEpoch);
		if (Sun.IsNearlyZero())
		{
			return 0.0;
		}
		return Wrap(Azimuth(AnchorDirection) - Azimuth(Sun));
	}

	double NextLocalNoon(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double AfterSeconds)
	{
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return AfterSeconds;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];
		const double Period = Body.RotationPeriodSeconds;
		if (!(FMath::Abs(Period) > 0.0))
		{
			// Tidally stopped. The sun is where it is and noon is now or never;
			// "now" is the answer that keeps callers from looping forever.
			return AfterSeconds;
		}

		// The hour angle advances at the rotation rate, so the time to the next
		// zero is roughly the angle still to run divided by that rate. Signed,
		// so a retrograde body works without a special case.
		const double Rate = LedgerTwoPi / Period;
		auto TimeToNoon = [&](double At) -> double
		{
			const double H = HourAngle(System, BodyIndex, AnchorDirection, At);
			// Strictly in the future: an hour angle of exactly zero means noon
			// is now, and the *next* one is a whole day away.
			const double Remaining = Rate > 0.0
				? (H > 0.0 ? LedgerTwoPi - H : -H)
				: (H < 0.0 ? -(LedgerTwoPi + H) : -H);
			return Remaining / Rate;
		};

		double T0 = AfterSeconds + TimeToNoon(AfterSeconds);

		// And then a secant iteration on the wrapped angle, which is smooth
		// near zero.
		//
		// **Secant rather than Newton with the rotation rate, and the moon is
		// why.** The hour angle does not advance at the rotation rate: it
		// advances at the rotation rate minus the rate the star's direction
		// itself moves, which is the difference between a sidereal day and a
		// solar one. On the planet that is a third of a per cent and eight
		// Newton steps bury it. On a tidally locked moon, whose rotation period
		// is its month, it is several per cent -- and eight steps at a linear
		// factor of 0.07 left the hour angle at 2.283e-9 rad against a 1e-9
		// bound. Measuring the slope instead of assuming it costs one extra
		// evaluation and does not care what is orbiting what.
		double H0 = HourAngle(System, BodyIndex, AnchorDirection, T0);
		double T = T0 - H0 / Rate;
		for (int32 Step = 0; Step < 16; ++Step)
		{
			const double H = HourAngle(System, BodyIndex, AnchorDirection, T);
			const double Slope = (H - H0) / (T - T0);
			if (!(FMath::Abs(Slope) > 0.0))
			{
				break;
			}
			const double Delta = H / Slope;
			T0 = T;
			H0 = H;
			T -= Delta;
			if (FMath::Abs(Delta) < 1e-9)
			{
				break;
			}
		}
		return T;
	}
}
