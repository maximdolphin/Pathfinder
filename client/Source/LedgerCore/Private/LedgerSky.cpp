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

	double SolarDeclination(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch)
	{
		// The body frame's axis is +Z -- the tilt and the spin are what carry
		// that frame into the system -- so declination is just how far up the
		// sun sits in it. No spherical triangle, and no dependence on the time
		// of day, which is the property that makes it a season rather than an
		// hour.
		const FVector3d Sun = SunDirectionInBody(System, BodyIndex, SecondsFromEpoch);
		return Sun.IsNearlyZero() ? 0.0 : FMath::Asin(FMath::Clamp(Sun.Z, -1.0, 1.0));
	}

	double SolarDaySeconds(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch)
	{
		// Measured rather than derived: the interval between the two noons
		// either side of the moment asked about. NextLocalNoon already solves
		// for an hour angle of zero, and two of those is a solar day by
		// definition -- no separate formula to disagree with it.
		const double First = NextLocalNoon(System, BodyIndex, AnchorDirection, SecondsFromEpoch);
		const double Second = NextLocalNoon(System, BodyIndex, AnchorDirection, First + 1.0);
		const double Length = Second - First;
		if (Length > 0.0)
		{
			return Length;
		}
		// Tidally stopped, or a body with no rotation. There is no solar day.
		return System.Bodies.IsValidIndex(BodyIndex)
			? FMath::Abs(System.Bodies[BodyIndex].RotationPeriodSeconds) : 0.0;
	}

	double DayLengthSeconds(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch)
	{
		const FVector3d Anchor = AnchorDirection.GetSafeNormal();
		if (Anchor.IsNearlyZero())
		{
			return 0.0;
		}

		const double Day = SolarDaySeconds(System, BodyIndex, AnchorDirection, SecondsFromEpoch);
		if (!(Day > 0.0))
		{
			return 0.0;
		}

		const double Latitude = FMath::Asin(FMath::Clamp(Anchor.Z, -1.0, 1.0));
		const double Declination = SolarDeclination(System, BodyIndex, SecondsFromEpoch);

		// cos(H0) = -tan(latitude) tan(declination), the hour angle at which the
		// star crosses the horizon. Outside [-1, 1] there is no crossing, which
		// is not a failure -- it is the polar day and the polar night, and they
		// are the whole reason a high-latitude site is where this is tested.
		const double CosH0 = -FMath::Tan(Latitude) * FMath::Tan(Declination);
		if (CosH0 <= -1.0)
		{
			return Day;
		}
		if (CosH0 >= 1.0)
		{
			return 0.0;
		}
		return Day * FMath::Acos(CosH0) / LedgerPi;
	}

	double SeasonPhase(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch)
	{
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return 0.0;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];
		const int32 Parent = Body.ParentIndex;
		if (!System.Bodies.IsValidIndex(Parent))
		{
			return 0.0;
		}

		const double Year = LedgerEphemeris::PeriodSeconds(
			System.Bodies[Parent].MassKg, Body.Orbit.SemiMajorAxisMetres);
		if (!(Year > 0.0))
		{
			return 0.0;
		}

		// Phase measured from maximum northern declination, found rather than
		// assumed: the epoch is not a solstice and the orbit is not a circle, so
		// there is no closed form for where in the year the tilt points hardest
		// at the star. A scan and a refinement.
		//
		// Not cached. This is asked once per run -- the world reads it at begin
		// play and hands the answer to the terrain, because a season that moved
		// while patches were being built would invalidate each one as it landed.
		// A cache keyed on the system's address would be a dangling pointer
		// waiting for a system that happened to be rebuilt at the same place.
		constexpr int32 Samples = 360;
		double BestAt = 0.0;
		double Best = -2.0;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const double At = Year * Index / Samples;
			const double D = SolarDeclination(System, BodyIndex, At);
			if (D > Best) { Best = D; BestAt = At; }
		}
		double Low = BestAt - Year / Samples;
		double High = BestAt + Year / Samples;
		for (int32 Step = 0; Step < 80; ++Step)
		{
			const double A = Low + (High - Low) / 3.0;
			const double B = High - (High - Low) / 3.0;
			if (SolarDeclination(System, BodyIndex, A)
				< SolarDeclination(System, BodyIndex, B)) { Low = A; } else { High = B; }
		}
		const double Solstice = (Low + High) * 0.5;

		double Phase = FMath::Fmod((SecondsFromEpoch - Solstice) / Year, 1.0);
		if (Phase < 0.0) { Phase += 1.0; }
		return Phase;
	}

	double DailyInsolationAt(double LatitudeRadians, double DeclinationRadians)
	{
		const double CosH0 =
			-FMath::Tan(LatitudeRadians) * FMath::Tan(DeclinationRadians);
		const double H0 = CosH0 <= -1.0 ? LedgerPi : (CosH0 >= 1.0 ? 0.0 : FMath::Acos(CosH0));

		// The mean of cos(zenith) over a whole rotation, counting only the lit
		// part. Integrating sin(lat)sin(dec) + cos(lat)cos(dec)cos(H) from -H0
		// to H0 and dividing by 2pi gives this, and it is the standard daily
		// insolation up to the solar constant and the inverse square -- both of
		// which belong to the star, not to the season.
		const double Mean = (H0 * FMath::Sin(LatitudeRadians) * FMath::Sin(DeclinationRadians)
			+ FMath::Cos(LatitudeRadians) * FMath::Cos(DeclinationRadians) * FMath::Sin(H0))
			/ LedgerPi;
		return FMath::Max(0.0, Mean);
	}

	double DailyInsolation(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch)
	{
		const FVector3d Anchor = AnchorDirection.GetSafeNormal();
		if (Anchor.IsNearlyZero())
		{
			return 0.0;
		}
		return DailyInsolationAt(
			FMath::Asin(FMath::Clamp(Anchor.Z, -1.0, 1.0)),
			SolarDeclination(System, BodyIndex, SecondsFromEpoch));
	}

	FVector3d ObserverPosition(
		const FLedgerSystem& System, int32 BodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch)
	{
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return FVector3d::ZeroVector;
		}
		FLedgerBodyPoint Standing;
		Standing.BodyIndex = BodyIndex;
		Standing.Metres =
			AnchorDirection.GetSafeNormal() * System.Bodies[BodyIndex].RadiusMetres;
		return LedgerFrames::ToSystem(System, Standing, SecondsFromEpoch).Metres;
	}

	double PhaseAngle(
		const FLedgerSystem& System, int32 TargetIndex,
		const FVector3d& ObserverPositionMetres, double SecondsFromEpoch)
	{
		const int32 Star = PrimaryIndex(System);
		if (!System.Bodies.IsValidIndex(TargetIndex) || Star == INDEX_NONE)
		{
			return 0.0;
		}

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		// The angle at the TARGET, between the star and the observer. Measured
		// there rather than anywhere more convenient because that is where the
		// terminator is.
		const FVector3d Target = States[TargetIndex].PositionMetres;
		const FVector3d ToStar = (States[Star].PositionMetres - Target).GetSafeNormal();
		const FVector3d ToObserver = (ObserverPositionMetres - Target).GetSafeNormal();
		if (ToStar.IsNearlyZero() || ToObserver.IsNearlyZero())
		{
			return 0.0;
		}
		return FMath::Acos(
			FMath::Clamp(FVector3d::DotProduct(ToStar, ToObserver), -1.0, 1.0));
	}

	double IlluminatedFraction(double PhaseAngleRadians)
	{
		return (1.0 + FMath::Cos(PhaseAngleRadians)) * 0.5;
	}

	void VisibleBodies(
		const FLedgerSystem& System, int32 ObserverBodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch,
		TArray<FLedgerSkyBody>& Out)
	{
		Out.Reset();
		if (!System.Bodies.IsValidIndex(ObserverBodyIndex))
		{
			return;
		}

		const FVector3d Eye =
			ObserverPosition(System, ObserverBodyIndex, AnchorDirection, SecondsFromEpoch);

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			if (Index == ObserverBodyIndex)
			{
				continue;
			}

			const FVector3d Offset = States[Index].PositionMetres - Eye;
			const double Distance = Offset.Length();
			if (!(Distance > 0.0))
			{
				continue;
			}

			FLedgerSkyBody Seen;
			Seen.BodyIndex = Index;
			Seen.DistanceMetres = Distance;

			// asin rather than the small-angle ratio: a moon seen from low
			// orbit is not a small angle, and the ratio would put its limb
			// outside its own disc.
			const double Radius = System.Bodies[Index].RadiusMetres;
			Seen.AngularRadiusRadians = Radius < Distance
				? FMath::Asin(Radius / Distance) : LedgerPi * 0.5;

			Seen.PhaseAngleRadians = PhaseAngle(System, Index, Eye, SecondsFromEpoch);
			Seen.IlluminatedFraction = IlluminatedFraction(Seen.PhaseAngleRadians);

			// Into the observer's east-north-up. The direction is a direction,
			// so it goes through the body frame without a radius on it.
			FLedgerBodyPoint InBody;
			InBody.BodyIndex = ObserverBodyIndex;
			InBody.Metres = LedgerFrames::BodyOrientation(
				System.Bodies[ObserverBodyIndex], SecondsFromEpoch)
					.UnrotateVector(Offset / Distance);
			Seen.DirectionInSurface =
				LedgerFrames::ToSurface(InBody, AnchorDirection).Metres.GetSafeNormal();

			Out.Add(Seen);
		}

		// Brightest-looking first: lit area, which is what decides whether a
		// thing is worth drawing before it decides how.
		Out.Sort([](const FLedgerSkyBody& A, const FLedgerSkyBody& B)
		{
			return A.AngularRadiusRadians * A.AngularRadiusRadians * A.IlluminatedFraction
				> B.AngularRadiusRadians * B.AngularRadiusRadians * B.IlluminatedFraction;
		});
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
