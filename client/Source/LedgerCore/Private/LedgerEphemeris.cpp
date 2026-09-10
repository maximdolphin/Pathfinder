#include "LedgerEphemeris.h"

#include "LedgerLog.h"

namespace LedgerEphemeris
{
	double MeanMotion(double ParentMassKg, double SemiMajorAxisMetres)
	{
		if (!(ParentMassKg > 0.0) || !(SemiMajorAxisMetres > 0.0))
		{
			return 0.0;
		}
		const double Mu = GravitationalConstant * ParentMassKg;
		return FMath::Sqrt(Mu / (SemiMajorAxisMetres * SemiMajorAxisMetres * SemiMajorAxisMetres));
	}

	double PeriodSeconds(double ParentMassKg, double SemiMajorAxisMetres)
	{
		const double N = MeanMotion(ParentMassKg, SemiMajorAxisMetres);
		return N > 0.0 ? (2.0 * PI) / N : 0.0;
	}

	double EccentricAnomaly(double MeanAnomalyRadians, double Eccentricity)
	{
		// Wrapped to [-pi, pi] first. Kepler's equation is periodic and Newton
		// converges from the nearest root, so an unwrapped mean anomaly a
		// century's worth of revolutions from zero -- which is exactly what the
		// acceptance asks for -- would start the iteration thousands of radians
		// from the answer.
		double M = FMath::Fmod(MeanAnomalyRadians, 2.0 * PI);
		if (M > PI) { M -= 2.0 * PI; }
		if (M < -PI) { M += 2.0 * PI; }

		const double E0 = FMath::Clamp(Eccentricity, 0.0, 0.999999);

		// A circle needs no solving, and this is the common case: most of the
		// bodies in a generated system are within a few per cent of circular.
		if (E0 < 1e-12)
		{
			return M;
		}

		// Danby's starting guess, and the reason is a test failure.
		//
		// The first version used the third-order seed
		// `M + e sin M / (1 - sin(M + e) + sin M)`, whose denominator passes
		// through zero. At e = 0.95 and M = -18002 it did, Newton launched off
		// the root, and Kepler's equation came back with a residual of 6.5
		// million radians. The substitution test caught it; a table of expected
		// values would not have, because the answer was not wrong in a way
		// anybody would have thought to tabulate.
		//
		// This one cannot blow up: it is M displaced towards periapsis by at
		// most 0.85 e, which is always inside the interval containing the root.
		const double SinM = FMath::Sin(M);
		double E = M + 0.85 * E0 * (SinM >= 0.0 ? 1.0 : -1.0);

		// Twelve. Danby's seed is robust rather than fast:
		// at e = 0.95 it needs a few more steps than the seed that could
		// diverge, which is a trade worth making in the direction of always
		// getting an answer. At the eccentricities a system description allows
		// it converges to double precision in three or four, and the limit is
		// here so a pathological input cannot spin.
		for (int32 Step = 0; Step < 12; ++Step)
		{
			const double Residual = E - E0 * FMath::Sin(E) - M;
			const double Slope = 1.0 - E0 * FMath::Cos(E);
			if (FMath::Abs(Slope) < 1e-15)
			{
				break;
			}
			const double Delta = Residual / Slope;
			E -= Delta;
			if (FMath::Abs(Delta) < 1e-15)
			{
				break;
			}
		}
		return E;
	}

	FLedgerState StateAt(
		const FLedgerBody& Body, double ParentMassKg, double SecondsFromEpoch)
	{
		FLedgerState State;

		const double Axis = Body.Orbit.SemiMajorAxisMetres;
		const double N = MeanMotion(ParentMassKg, Axis);
		if (N <= 0.0)
		{
			// The primary, or a body with no orbit. It sits at the origin of
			// its own frame, which is the honest answer rather than zero being
			// a failure code.
			return State;
		}

		const double E0 = Body.Orbit.Eccentricity;
		const double M = Body.Orbit.MeanAnomalyAtEpochRadians + N * SecondsFromEpoch;
		const double E = EccentricAnomaly(M, E0);

		const double CosE = FMath::Cos(E);
		const double SinE = FMath::Sin(E);
		const double OneMinusE2 = FMath::Sqrt(FMath::Max(0.0, 1.0 - E0 * E0));

		// In the orbital plane, periapsis along +x.
		const FVector3d Plane(Axis * (CosE - E0), Axis * OneMinusE2 * SinE, 0.0);

		// Velocity in the same plane: the time derivative of the above, with
		// dE/dt = n / (1 - e cos E).
		const double EDot = N / (1.0 - E0 * CosE);
		const FVector3d PlaneVelocity(
			-Axis * SinE * EDot,
			Axis * OneMinusE2 * CosE * EDot,
			0.0);

		// Into the reference frame: rotate by the argument of periapsis about
		// z, then the inclination about x, then the ascending node about z.
		const double CosW = FMath::Cos(Body.Orbit.PeriapsisArgumentRadians);
		const double SinW = FMath::Sin(Body.Orbit.PeriapsisArgumentRadians);
		const double CosI = FMath::Cos(Body.Orbit.InclinationRadians);
		const double SinI = FMath::Sin(Body.Orbit.InclinationRadians);
		const double CosN = FMath::Cos(Body.Orbit.AscendingNodeRadians);
		const double SinN = FMath::Sin(Body.Orbit.AscendingNodeRadians);

		auto ToFrame = [=](const FVector3d& In) -> FVector3d
		{
			// Periapsis argument, about z.
			const double AX = In.X * CosW - In.Y * SinW;
			const double AY = In.X * SinW + In.Y * CosW;
			// Inclination, about x.
			const double BY = AY * CosI;
			const double BZ = AY * SinI;
			// Ascending node, about z.
			return FVector3d(
				AX * CosN - BY * SinN,
				AX * SinN + BY * CosN,
				BZ);
		};

		State.PositionMetres = ToFrame(Plane);
		State.VelocityMetresPerSecond = ToFrame(PlaneVelocity);
		return State;
	}

	void StatesAt(
		const FLedgerSystem& System, double SecondsFromEpoch, TArray<FLedgerState>& Out)
	{
		Out.Reset();
		Out.SetNum(System.Bodies.Num());

		// Relative to the parent first, then summed up the chain. A moon's
		// place in the primary's frame is its own orbit plus its planet's, and
		// nothing here assumes the array is sorted parents-first.
		TArray<FLedgerState> Relative;
		Relative.SetNum(System.Bodies.Num());
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			const FLedgerBody& Body = System.Bodies[Index];
			const double ParentMass = System.Bodies.IsValidIndex(Body.ParentIndex)
				? System.Bodies[Body.ParentIndex].MassKg : 0.0;
			Relative[Index] = StateAt(Body, ParentMass, SecondsFromEpoch);
		}

		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			FLedgerState Absolute;
			int32 Walk = Index;
			int32 Steps = 0;
			while (Walk != INDEX_NONE && Steps++ <= System.Bodies.Num())
			{
				Absolute.PositionMetres += Relative[Walk].PositionMetres;
				Absolute.VelocityMetresPerSecond += Relative[Walk].VelocityMetresPerSecond;
				Walk = System.Bodies[Walk].ParentIndex;
			}
			Out[Index] = Absolute;
		}
	}

	double RotationAt(const FLedgerBody& Body, double SecondsFromEpoch)
	{
		if (!(FMath::Abs(Body.RotationPeriodSeconds) > 0.0))
		{
			return Body.RotationAtEpochRadians;
		}
		// Signed: a negative period is retrograde, which two planets in this
		// solar system actually do.
		return Body.RotationAtEpochRadians
			+ (2.0 * PI) * (SecondsFromEpoch / Body.RotationPeriodSeconds);
	}
}
