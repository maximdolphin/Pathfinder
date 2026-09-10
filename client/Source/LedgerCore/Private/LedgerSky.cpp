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

	/// The Sun, as the yardstick everything else is quoted against.
	constexpr double SolarMassKg = 1.98892e30;
	constexpr double SolarLuminosityWatts = 3.828e26;
	constexpr double StefanBoltzmann = 5.670374419e-8;

	/// Lumens per watt of a sun-like star's output, averaged over its spectrum.
	///
	/// **A simplification, and a known one.** Efficacy depends on the spectrum:
	/// a cool red star puts most of its output where an eye sees nothing, and a
	/// hot blue one loses it the other side. Treating it as constant makes a
	/// red dwarf's planets brighter than they should be. It is right for the
	/// star this system has, it is the difference between a lux and a watt
	/// rather than the difference between bright and dim, and correcting it
	/// means integrating a Planck curve against the photopic response -- which
	/// is a task, not a line.
	constexpr double LuminousEfficacy = 93.0;

	/// The area two circles on the sky share, over the first one's area.
	///
	/// Radii and separation in radians, which is close enough to a plane at
	/// half a degree that the flat lens formula is exact to a part in a million
	/// -- and a spherical version would be a different way of being wrong,
	/// since neither disc is a great circle.
	double DiscOverlapFraction(double FirstRadius, double SecondRadius, double Separation)
	{
		if (!(FirstRadius > 0.0))
		{
			return 0.0;
		}
		if (Separation >= FirstRadius + SecondRadius)
		{
			return 0.0;
		}
		if (Separation <= FMath::Abs(FirstRadius - SecondRadius))
		{
			// One is entirely inside the other: either totality, or a ring.
			const double Smaller = FMath::Min(FirstRadius, SecondRadius);
			return (Smaller * Smaller) / (FirstRadius * FirstRadius);
		}

		const double R1 = FirstRadius;
		const double R2 = SecondRadius;
		const double D = Separation;
		const double A1 = FMath::Acos(
			FMath::Clamp((D * D + R1 * R1 - R2 * R2) / (2.0 * D * R1), -1.0, 1.0));
		const double A2 = FMath::Acos(
			FMath::Clamp((D * D + R2 * R2 - R1 * R1) / (2.0 * D * R2), -1.0, 1.0));
		const double Area = R1 * R1 * (A1 - FMath::Sin(2.0 * A1) * 0.5)
			+ R2 * R2 * (A2 - FMath::Sin(2.0 * A2) * 0.5);
		return FMath::Clamp(Area / (LedgerPi * R1 * R1), 0.0, 1.0);
	}

	/// Shared by the coverage and the "what is it" query, so the two cannot
	/// disagree about which body is in the way.
	double CoverageAndBody(
		const FLedgerSystem& System, int32 ObserverBodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch, int32& OutBody)
	{
		OutBody = INDEX_NONE;

		TArray<FLedgerSkyBody> Seen;
		VisibleBodies(System, ObserverBodyIndex, AnchorDirection, SecondsFromEpoch, Seen);

		const FLedgerSkyBody* Star = Seen.FindByPredicate(
			[](const FLedgerSkyBody& B) { return B.BodyIndex == 0; });
		if (Star == nullptr || Star->DirectionInSurface.Z <= 0.0)
		{
			return 0.0;
		}

		double Best = 0.0;
		for (const FLedgerSkyBody& Body : Seen)
		{
			if (Body.BodyIndex == 0)
			{
				continue;
			}
			// In front, not behind. A body further away than the star is on the
			// other side of it and lines up just as often.
			if (Body.DistanceMetres >= Star->DistanceMetres)
			{
				continue;
			}

			const double Separation = FMath::Acos(FMath::Clamp(
				FVector3d::DotProduct(Body.DirectionInSurface, Star->DirectionInSurface),
				-1.0, 1.0));
			const double Covered = DiscOverlapFraction(
				Star->AngularRadiusRadians, Body.AngularRadiusRadians, Separation);
			if (Covered > Best)
			{
				Best = Covered;
				OutBody = Body.BodyIndex;
			}
		}
		return Best;
	}

	double StarCoveredFraction(
		const FLedgerSystem& System, int32 ObserverBodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch)
	{
		int32 Ignored = INDEX_NONE;
		return CoverageAndBody(
			System, ObserverBodyIndex, AnchorDirection, SecondsFromEpoch, Ignored);
	}

	int32 EclipsingBody(
		const FLedgerSystem& System, int32 ObserverBodyIndex,
		const FVector3d& AnchorDirection, double SecondsFromEpoch)
	{
		int32 Which = INDEX_NONE;
		CoverageAndBody(
			System, ObserverBodyIndex, AnchorDirection, SecondsFromEpoch, Which);
		return Which;
	}

	double NextEclipse(
		const FLedgerSystem& System, int32 ObserverBodyIndex,
		const FVector3d& AnchorDirection, double AfterSeconds, double SpanSeconds,
		double& OutPeakCoverage)
	{
		OutPeakCoverage = 0.0;
		if (!(SpanSeconds > 0.0))
		{
			return -1.0;
		}

		// **The step has to be shorter than an eclipse, or the search steps
		// over them.** The occulter moves across the star at roughly its own
		// diameter per hour at these distances, so first contact to last is a
		// couple of hours and a ten-minute step lands inside it a dozen times.
		// A step chosen for speed instead would report that this system has no
		// eclipses, which is a very convincing wrong answer.
		constexpr double CoarseStep = 600.0;

		const int32 Steps = static_cast<int32>(SpanSeconds / CoarseStep);
		for (int32 Index = 0; Index <= Steps; ++Index)
		{
			const double At = AfterSeconds + Index * CoarseStep;
			const double Covered = StarCoveredFraction(
				System, ObserverBodyIndex, AnchorDirection, At);
			if (Covered <= 0.0)
			{
				continue;
			}

			// Something is in the way -- but this is FIRST CONTACT, not the
			// peak, and a predicted time means greatest coverage.
			//
			// **The first version bracketed the peak at plus or minus one
			// coarse step and refined inside that.** It reported 3.5% coverage
			// at a moment when ten minutes later the star was 8.5% covered and
			// half an hour later 19.4%: a converged, precise answer to the
			// wrong question. An eclipse runs for hours and the search found
			// the edge of one.
			//
			// So: walk forward at a minute a step for as long as anything is
			// still in front of the star, keep the deepest, and refine there.
			constexpr double FineStep = 60.0;
			double BestAt = At;
			double Best = Covered;
			for (double Walk = At; ; Walk += FineStep)
			{
				const double Here = StarCoveredFraction(
					System, ObserverBodyIndex, AnchorDirection, Walk);
				if (Here <= 0.0 && Walk > At)
				{
					break;
				}
				if (Here > Best)
				{
					Best = Here;
					BestAt = Walk;
				}
				// A guard, not a limit anybody should reach: the longest
				// possible transit here is a few hours.
				if (Walk - At > 86400.0)
				{
					break;
				}
			}

			double Low = BestAt - FineStep;
			double High = BestAt + FineStep;
			for (int32 Refine = 0; Refine < 120; ++Refine)
			{
				const double A = Low + (High - Low) / 3.0;
				const double B = High - (High - Low) / 3.0;
				if (StarCoveredFraction(System, ObserverBodyIndex, AnchorDirection, A)
					< StarCoveredFraction(System, ObserverBodyIndex, AnchorDirection, B))
				{
					Low = A;
				}
				else
				{
					High = B;
				}
			}
			const double Peak = (Low + High) * 0.5;
			OutPeakCoverage = StarCoveredFraction(
				System, ObserverBodyIndex, AnchorDirection, Peak);
			return Peak;
		}
		return -1.0;
	}

	double StarLuminosityWatts(const FLedgerBody& Star)
	{
		if (!(Star.MassKg > 0.0))
		{
			return 0.0;
		}
		// The relation itself lives on the body, because T084's generator needs
		// it before anything has a sky: the frost line and the habitable zone
		// are both set by it. Two copies of a piecewise power law is two chances
		// for a system to be laid out against one and lit by the other.
		return SolarLuminosityWatts * LedgerBodies::StarLuminosityRelative(Star);
	}

	double StarTemperatureKelvin(const FLedgerBody& Star)
	{
		const double Luminosity = StarLuminosityWatts(Star);
		if (!(Luminosity > 0.0) || !(Star.RadiusMetres > 0.0))
		{
			return 0.0;
		}
		const double Area = 4.0 * LedgerPi * Star.RadiusMetres * Star.RadiusMetres;
		return FMath::Pow(Luminosity / (Area * StefanBoltzmann), 0.25);
	}

	double IlluminanceFromDisc(double TemperatureKelvin, double AngularRadiusRadians)
	{
		if (!(TemperatureKelvin > 0.0) || !(AngularRadiusRadians > 0.0))
		{
			return 0.0;
		}
		const double SinAlpha = FMath::Sin(AngularRadiusRadians);
		const double T2 = TemperatureKelvin * TemperatureKelvin;
		return StefanBoltzmann * T2 * T2 * SinAlpha * SinAlpha * LuminousEfficacy;
	}

	double IlluminanceLux(
		const FLedgerSystem& System, const FVector3d& ObserverPositionMetres,
		double SecondsFromEpoch)
	{
		const int32 Star = PrimaryIndex(System);
		if (Star == INDEX_NONE)
		{
			return 0.0;
		}

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		const double Distance =
			(States[Star].PositionMetres - ObserverPositionMetres).Length();
		if (!(Distance > 0.0))
		{
			return 0.0;
		}

		const double Luminosity = StarLuminosityWatts(System.Bodies[Star]);
		return (Luminosity / (4.0 * LedgerPi * Distance * Distance)) * LuminousEfficacy;
	}

	double EquilibriumTemperatureKelvin(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch)
	{
		const int32 Star = PrimaryIndex(System);
		if (!System.Bodies.IsValidIndex(BodyIndex) || Star == INDEX_NONE)
		{
			return 0.0;
		}

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);
		const double Distance =
			(States[Star].PositionMetres - States[BodyIndex].PositionMetres).Length();
		if (!(Distance > 0.0))
		{
			return 0.0;
		}

		// A grey ball: it catches sunlight over its cross-section and radiates
		// from its whole surface, which is the factor of four, and reflects
		// some of it away, which is the albedo. 0.3 is Earth's and near enough
		// to a rocky average.
		constexpr double Albedo = 0.3;
		const double Luminosity = StarLuminosityWatts(System.Bodies[Star]);
		const double Flux = Luminosity / (4.0 * LedgerPi * Distance * Distance);
		return FMath::Pow(
			Flux * (1.0 - Albedo) / (4.0 * StefanBoltzmann), 0.25);
	}

	double EscapeVelocity(const FLedgerBody& Body)
	{
		if (!(Body.MassKg > 0.0) || !(Body.RadiusMetres > 0.0))
		{
			return 0.0;
		}
		return FMath::Sqrt(
			2.0 * LedgerEphemeris::GravitationalConstant * Body.MassKg / Body.RadiusMetres);
	}

	bool RetainsAtmosphere(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch)
	{
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return false;
		}

		const double Temperature =
			EquilibriumTemperatureKelvin(System, BodyIndex, SecondsFromEpoch);
		if (!(Temperature > 0.0))
		{
			return false;
		}

		// Root-mean-square speed of a nitrogen molecule at that temperature.
		constexpr double Boltzmann = 1.380649e-23;
		constexpr double NitrogenMassKg = 4.6517e-26;
		const double Thermal =
			FMath::Sqrt(3.0 * Boltzmann * Temperature / NitrogenMassKg);

		// **Eight, calibrated against bodies somebody has been to.**
		//
		// A gas is a distribution and its fast tail leaves first, so a body
		// whose escape velocity merely matches the typical speed empties within
		// a geological eye-blink. Six is the textbook figure and it was tried
		// first; it gets Mercury wrong, putting it at 6.8 and therefore inside.
		//
		// The six real cases sort like this:
		//
		//   Earth   23.5      Mercury  6.8
		//   Mars    11.6      Moon     4.9
		//   Titan    9.1      Ceres    1.1
		//
		// which leaves a clean gap between 6.8 and 9.1 and no threshold in it
		// that gets any of them wrong. Eight sits in the middle of that gap.
		// The number was not chosen and then defended -- it was measured out of
		// the cases and the cases are in Ledger.Body.WhoKeepsAnAtmosphere.
		return EscapeVelocity(System.Bodies[BodyIndex]) > 8.0 * Thermal;
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
