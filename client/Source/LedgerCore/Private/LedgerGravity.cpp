#include "LedgerGravity.h"

#include "LedgerEphemeris.h"
#include "LedgerMath.h"

namespace LedgerGravity
{
	FVector3d FieldAt(
		const FLedgerSystem& System, const FVector3d& PositionMetres,
		double SecondsFromEpoch)
	{
		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		FVector3d Sum = FVector3d::ZeroVector;
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			const double Mass = System.Bodies[Index].MassKg;
			if (!(Mass > 0.0))
			{
				continue;
			}
			const FVector3d Offset = States[Index].PositionMetres - PositionMetres;
			const double DistanceSquared = Offset.SquaredLength();

			// Inside a body, the mass above you stops pulling: the field falls
			// linearly to zero at the centre rather than going to infinity.
			// Nothing should be in there, and a trajectory that ends up there
			// should not produce a singularity on its way to being wrong.
			const double Radius = System.Bodies[Index].RadiusMetres;
			if (DistanceSquared < Radius * Radius)
			{
				const double Distance = FMath::Sqrt(DistanceSquared);
				if (Distance <= 0.0)
				{
					continue;
				}
				Sum += Offset.GetSafeNormal(1e-30)
					* (LedgerEphemeris::GravitationalConstant * Mass * Distance
						/ (Radius * Radius * Radius));
				continue;
			}

			const double Distance = FMath::Sqrt(DistanceSquared);
			Sum += Offset * (LedgerEphemeris::GravitationalConstant * Mass
				/ (DistanceSquared * Distance));
		}
		return Sum;
	}

	double SphereOfInfluenceMetres(const FLedgerSystem& System, int32 BodyIndex)
	{
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return 0.0;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];
		const int32 Parent = Body.ParentIndex;
		if (!System.Bodies.IsValidIndex(Parent))
		{
			// The primary's sphere is the system.
			return TNumericLimits<double>::Max();
		}
		const double ParentMass = System.Bodies[Parent].MassKg;
		if (!(ParentMass > 0.0) || !(Body.MassKg > 0.0))
		{
			return 0.0;
		}
		return Body.Orbit.SemiMajorAxisMetres
			* FMath::Pow(Body.MassKg / ParentMass, 0.4);
	}

	int32 DominantBody(
		const FLedgerSystem& System, const FVector3d& PositionMetres,
		double SecondsFromEpoch)
	{
		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		// The deepest sphere the point is inside. Depth is how many parents a
		// body has, so a moon beats its planet and a station beats its moon --
		// which is the order a trajectory planner wants, because the innermost
		// body is the one whose orbit the ship is actually on.
		int32 Best = INDEX_NONE;
		int32 BestDepth = -1;
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			const double Reach = SphereOfInfluenceMetres(System, Index);
			const double Distance =
				(States[Index].PositionMetres - PositionMetres).Length();
			if (Distance > Reach)
			{
				continue;
			}

			int32 Depth = 0;
			int32 Walk = System.Bodies[Index].ParentIndex;
			int32 Guard = 0;
			while (System.Bodies.IsValidIndex(Walk) && Guard++ <= System.Bodies.Num())
			{
				++Depth;
				Walk = System.Bodies[Walk].ParentIndex;
			}
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = Index;
			}
		}
		return Best;
	}

	void Step(
		const FLedgerSystem& System, FVector3d& Position, FVector3d& Velocity,
		double SecondsFromEpoch, double StepSeconds)
	{
		const double H = StepSeconds;

		const FVector3d K1V = FieldAt(System, Position, SecondsFromEpoch);
		const FVector3d K1X = Velocity;

		const FVector3d K2V = FieldAt(
			System, Position + K1X * (H * 0.5), SecondsFromEpoch + H * 0.5);
		const FVector3d K2X = Velocity + K1V * (H * 0.5);

		const FVector3d K3V = FieldAt(
			System, Position + K2X * (H * 0.5), SecondsFromEpoch + H * 0.5);
		const FVector3d K3X = Velocity + K2V * (H * 0.5);

		const FVector3d K4V = FieldAt(System, Position + K3X * H, SecondsFromEpoch + H);
		const FVector3d K4X = Velocity + K3V * H;

		Position += (K1X + K2X * 2.0 + K3X * 2.0 + K4X) * (H / 6.0);
		Velocity += (K1V + K2V * 2.0 + K3V * 2.0 + K4V) * (H / 6.0);
	}
}
