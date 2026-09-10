#include "LedgerAsteroid.h"

#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "LedgerNoise.h"

namespace
{
	/// How finely the interior is chopped for the gravity sum.
	///
	/// **The cost is cubic and the accuracy is not.** 48 gives about 58,000
	/// cells inside a typical shape, which puts the sphere check within a
	/// tenth of a per cent of GM/r^2 -- and going to 96 costs eight times as
	/// much for a factor of four. This is a landing, not an ephemeris.
	/// 64, not 48. At 48 the integrator's own error at the SURFACE -- where the
	/// evaluation point sits right on top of the nearest cells -- was 1.24
	/// degrees of false lean on a perfect sphere, which is a fifth of the real
	/// lean this is meant to measure. 64 puts that floor near a third of a
	/// degree for a bit over twice the build cost, paid once and cached.
	constexpr int32 CellsAcross = 64;

	struct FMassPoint
	{
		FVector3d Position;
		double Mass;
	};

	/// The mass points of a shape, built once and kept.
	///
	/// Keyed on the shape's own numbers rather than its address, because a
	/// caller may well build the struct fresh each time it asks.
	const TArray<FMassPoint>& MassPointsFor(const FLedgerAsteroidShape& Shape)
	{
		struct FEntry
		{
			uint32 Seed;
			double MeanRadius;
			double Irregularity;
			double Density;
			TArray<FMassPoint> Points;
		};
		static TArray<FEntry> Cache;
		static FCriticalSection Lock;

		FScopeLock Held(&Lock);
		for (const FEntry& Entry : Cache)
		{
			if (Entry.Seed == Shape.Seed
				&& Entry.MeanRadius == Shape.MeanRadiusMetres
				&& Entry.Irregularity == Shape.Irregularity
				&& Entry.Density == Shape.DensityKgPerM3)
			{
				return Entry.Points;
			}
		}

		FEntry Built;
		Built.Seed = Shape.Seed;
		Built.MeanRadius = Shape.MeanRadiusMetres;
		Built.Irregularity = Shape.Irregularity;
		Built.Density = Shape.DensityKgPerM3;

		// A cube of cells over the shape's bounding box, keeping the ones
		// inside. Equal volumes, so equal masses, so the sum is an integral
		// rather than a weighted guess.
		const double Extent = Shape.MeanRadiusMetres * (1.0 + Shape.Irregularity) * 1.02;
		const double Step = 2.0 * Extent / CellsAcross;
		const double CellVolume = Step * Step * Step;
		const double CellMass = CellVolume * Shape.DensityKgPerM3;

		for (int32 IX = 0; IX < CellsAcross; ++IX)
		{
			for (int32 IY = 0; IY < CellsAcross; ++IY)
			{
				for (int32 IZ = 0; IZ < CellsAcross; ++IZ)
				{
					const FVector3d At(
						-Extent + (IX + 0.5) * Step,
						-Extent + (IY + 0.5) * Step,
						-Extent + (IZ + 0.5) * Step);
					const double Distance = At.Length();
					if (Distance <= 0.0)
					{
						continue;
					}
					if (Distance <= LedgerAsteroid::RadiusInDirection(Shape, At / Distance))
					{
						Built.Points.Add({ At, CellMass });
					}
				}
			}
		}

		Cache.Add(MoveTemp(Built));
		return Cache.Last().Points;
	}
}

namespace LedgerAsteroid
{
	double RadiusInDirection(
		const FLedgerAsteroidShape& Shape, const FVector3d& Direction)
	{
		const FVector3d Unit = Direction.GetSafeNormal();
		if (Unit.IsNearlyZero())
		{
			return Shape.MeanRadiusMetres;
		}

		// Low frequencies only. An asteroid's SHAPE is a few big lobes; the
		// gravel on it is a surface detail and belongs to whatever draws it,
		// not to the thing that decides where the mass is.
		const double Lumps = LedgerNoise::Fractal(Unit * 1.3, Shape.Seed, 3);
		return Shape.MeanRadiusMetres * (1.0 + Shape.Irregularity * Lumps);
	}

	double MassKg(const FLedgerAsteroidShape& Shape)
	{
		double Total = 0.0;
		for (const FMassPoint& Point : MassPointsFor(Shape))
		{
			Total += Point.Mass;
		}
		return Total;
	}

	FVector3d GravityAt(
		const FLedgerAsteroidShape& Shape, const FVector3d& PointMetres)
	{
		const TArray<FMassPoint>& Points = MassPointsFor(Shape);

		// A floor on the distance, so a sample point sitting on top of a cell
		// does not produce an infinite pull from one of fifty thousand. Half a
		// cell is the scale below which the cell is not a point anyway.
		const double Extent = Shape.MeanRadiusMetres * (1.0 + Shape.Irregularity) * 1.02;
		const double Softening = (2.0 * Extent / CellsAcross) * 0.5;
		const double SofteningSquared = Softening * Softening;

		FVector3d Sum = FVector3d::ZeroVector;
		for (const FMassPoint& Point : Points)
		{
			const FVector3d Offset = Point.Position - PointMetres;
			const double DistanceSquared = Offset.SquaredLength() + SofteningSquared;
			const double Distance = FMath::Sqrt(DistanceSquared);
			Sum += Offset * (Point.Mass / (DistanceSquared * Distance));
		}
		return Sum * LedgerEphemeris::GravitationalConstant;
	}

	FVector3d SurfaceNormal(
		const FLedgerAsteroidShape& Shape, const FVector3d& Direction)
	{
		const FVector3d Unit = Direction.GetSafeNormal();
		if (Unit.IsNearlyZero())
		{
			return FVector3d::UnitZ();
		}

		// Two tangents, stepped a little around the sphere, and the surface
		// points they land on. The cross product of the two edges is the
		// normal -- which on a lumpy body leans away from the radial direction
		// by however much the surface slopes.
		FVector3d East = FVector3d::CrossProduct(Unit, FVector3d::UnitZ());
		if (East.SquaredLength() < 1e-12)
		{
			East = FVector3d::CrossProduct(Unit, FVector3d::UnitX());
		}
		East = East.GetSafeNormal();
		const FVector3d North = FVector3d::CrossProduct(Unit, East).GetSafeNormal();

		constexpr double Step = 1e-3;
		auto SurfacePoint = [&Shape](const FVector3d& Where) -> FVector3d
		{
			const FVector3d Unit2 = Where.GetSafeNormal();
			return Unit2 * RadiusInDirection(Shape, Unit2);
		};

		const FVector3d Centre = SurfacePoint(Unit);
		const FVector3d AlongEast = SurfacePoint(Unit + East * Step) - Centre;
		const FVector3d AlongNorth = SurfacePoint(Unit + North * Step) - Centre;

		FVector3d Normal = FVector3d::CrossProduct(AlongEast, AlongNorth).GetSafeNormal();
		if (FVector3d::DotProduct(Normal, Unit) < 0.0)
		{
			Normal = -Normal;
		}
		return Normal;
	}
}
