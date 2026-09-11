#include "LedgerRigidBody.h"

namespace
{
	FVector3d RigidTimes(const double M[3][3], const FVector3d& V)
	{
		return FVector3d(
			M[0][0] * V.X + M[0][1] * V.Y + M[0][2] * V.Z,
			M[1][0] * V.X + M[1][1] * V.Y + M[1][2] * V.Z,
			M[2][0] * V.X + M[2][1] * V.Y + M[2][2] * V.Z);
	}

	/// Solve M x = b for a symmetric positive three by three, by Cramer.
	FVector3d RigidSolve(const double M[3][3], const FVector3d& B)
	{
		auto Det = [](const double A[3][3])
		{
			return A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
				- A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
				+ A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
		};
		const double D = Det(M);
		if (FMath::Abs(D) < 1.0e-18)
		{
			return FVector3d::ZeroVector;
		}
		FVector3d Out;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			double A[3][3];
			for (int32 Row = 0; Row < 3; ++Row)
			{
				for (int32 Column = 0; Column < 3; ++Column)
				{
					A[Row][Column] = Column == Axis ? B[Row] : M[Row][Column];
				}
			}
			Out[Axis] = Det(A) / D;
		}
		return Out;
	}
}

namespace LedgerFlight
{
	FVector3d TorqueOf(const FVector3d& PointMetres, const FVector3d& ForceNewtons, const FVector3d& CentreMetres)
	{
		return FVector3d::CrossProduct(PointMetres - CentreMetres, ForceNewtons);
	}

	FVector3d AngularAcceleration(const double Inertia[3][3], const FVector3d& AngularVelocity, const FVector3d& TorqueBody)
	{
		const FVector3d Momentum = RigidTimes(Inertia, AngularVelocity);
		return RigidSolve(Inertia, TorqueBody - FVector3d::CrossProduct(AngularVelocity, Momentum));
	}

	void Rotate(FLedgerSpin& Spin, const double Inertia[3][3], const FVector3d& TorqueBody, double DeltaSeconds)
	{
		if (DeltaSeconds <= 0.0)
		{
			return;
		}
		const FVector3d W = Spin.AngularVelocity;
		const FVector3d K1 = AngularAcceleration(Inertia, W, TorqueBody);
		const FVector3d K2 = AngularAcceleration(Inertia, W + K1 * (DeltaSeconds * 0.5), TorqueBody);
		const FVector3d K3 = AngularAcceleration(Inertia, W + K2 * (DeltaSeconds * 0.5), TorqueBody);
		const FVector3d K4 = AngularAcceleration(Inertia, W + K3 * DeltaSeconds, TorqueBody);
		const FVector3d Next = W + (K1 + K2 * 2.0 + K3 * 2.0 + K4) * (DeltaSeconds / 6.0);

		// The orientation turns by the step's mean rate about its own axis: an
		// exact rotation, so the quaternion stays a rotation.
		const FVector3d Mean = (W + Next) * 0.5;
		const double Angle = Mean.Length() * DeltaSeconds;
		if (Angle > 0.0)
		{
			const FQuat4d Turn(Mean.GetSafeNormal(), Angle);
			Spin.Orientation = (Spin.Orientation * Turn).GetNormalized();
		}
		Spin.AngularVelocity = Next;
	}

	FVector3d AngularMomentum(const double Inertia[3][3], const FVector3d& AngularVelocity)
	{
		return RigidTimes(Inertia, AngularVelocity);
	}

	double RotationalEnergy(const double Inertia[3][3], const FVector3d& AngularVelocity)
	{
		return 0.5 * FVector3d::DotProduct(AngularVelocity, RigidTimes(Inertia, AngularVelocity));
	}

	FLedgerAllocation Allocate(const TArray<FLedgerNozzle>& Nozzles, const TArray<double>& LimitNewtons,
		const FVector3d& CentreMetres, const FVector3d& ForceNewtons, const FVector3d& TorqueNewtonMetres)
	{
		// Torque counts four times what force does, per unit of each scaled below.
		constexpr double TorqueWeight = 4.0;
		constexpr int32 Sweeps = 500;
		// A price on thrust small enough not to move an exact answer, large
		// enough to choose between them: no pair fires against itself.
		constexpr double ThrustPrice = 1.0e-6;

		const int32 Count = Nozzles.Num();
		FLedgerAllocation Out;
		Out.ThrustNewtons.Init(0.0, Count);
		if (Count == 0 || LimitNewtons.Num() != Count)
		{
			Out.bMet = ForceNewtons.IsNearlyZero() && TorqueNewtonMetres.IsNearlyZero();
			return Out;
		}

		// A newton and a newton-metre made comparable: the strongest nozzle
		// and the longest lever.
		double ForceScale = 1.0;
		double Lever = 1.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			ForceScale = FMath::Max(ForceScale, LimitNewtons[Index]);
			Lever = FMath::Max(Lever, (Nozzles[Index].PositionMetres - CentreMetres).Length());
		}
		const double TorqueScale = ForceScale * Lever / TorqueWeight;

		// Each nozzle at full thrust as six numbers, its force and its torque,
		// scaled; the unknowns are the fraction of full each one gives.
		TArray<double> Columns;
		Columns.SetNumZeroed(Count * 6);
		TArray<double> Norms;
		Norms.SetNumZeroed(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector3d Force = Nozzles[Index].Push * FMath::Max(LimitNewtons[Index], 0.0);
			const FVector3d Torque = TorqueOf(Nozzles[Index].PositionMetres, Force, CentreMetres);
			for (int32 Row = 0; Row < 3; ++Row)
			{
				Columns[Index * 6 + Row] = Force[Row] / ForceScale;
				Columns[Index * 6 + 3 + Row] = Torque[Row] / TorqueScale;
			}
			for (int32 Row = 0; Row < 6; ++Row)
			{
				Norms[Index] += Columns[Index * 6 + Row] * Columns[Index * 6 + Row];
			}
		}
		double Residual[6];
		for (int32 Row = 0; Row < 3; ++Row)
		{
			Residual[Row] = -ForceNewtons[Row] / ForceScale;
			Residual[3 + Row] = -TorqueNewtonMetres[Row] / TorqueScale;
		}

		// Coordinate descent on the box: each nozzle in turn moved to where it
		// best closes what is left, clamped to its range. Convex, so it settles
		// on the least-squares answer. ponytail: six multiplies a nozzle a
		// sweep is nothing for a ship's dozen or two; an active-set solver if
		// nozzles ever reach the hundreds.
		TArray<double> Fraction;
		Fraction.Init(0.0, Count);
		for (int32 Sweep = 0; Sweep < Sweeps; ++Sweep)
		{
			double Moved = 0.0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (Norms[Index] <= 0.0)
				{
					continue;
				}
				const double* Column = &Columns[Index * 6];
				double Slope = ThrustPrice * LimitNewtons[Index] / ForceScale;
				for (int32 Row = 0; Row < 6; ++Row)
				{
					Slope += Column[Row] * Residual[Row];
				}
				const double Next = FMath::Clamp(Fraction[Index] - Slope / Norms[Index], 0.0, 1.0);
				const double Step = Next - Fraction[Index];
				if (Step != 0.0)
				{
					for (int32 Row = 0; Row < 6; ++Row)
					{
						Residual[Row] += Column[Row] * Step;
					}
					Fraction[Index] = Next;
					Moved = FMath::Max(Moved, FMath::Abs(Step));
				}
			}
			if (Moved < 1.0e-10)
			{
				break;
			}
		}

		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out.ThrustNewtons[Index] = Fraction[Index] * FMath::Max(LimitNewtons[Index], 0.0);
			const FVector3d Force = Nozzles[Index].Push * Out.ThrustNewtons[Index];
			Out.Force += Force;
			Out.Torque += TorqueOf(Nozzles[Index].PositionMetres, Force, CentreMetres);
		}
		Out.bMet = (Out.Force - ForceNewtons).Length() <= 0.01 * ForceNewtons.Length() + 1.0e-4 * ForceScale
			&& (Out.Torque - TorqueNewtonMetres).Length() <= 0.01 * TorqueNewtonMetres.Length() + 1.0e-4 * ForceScale * Lever;
		return Out;
	}

	FLedgerCommand Control(ELedgerFlightMode Mode, const FLedgerStick& Stick, const FLedgerHandling& Handling,
		const FLedgerMassProperties& Mass, const FLedgerMotion& State, double DeltaSeconds)
	{
		FLedgerCommand Out;
		if (Mass.MassKg <= 0.0 || DeltaSeconds <= 0.0)
		{
			return Out;
		}
		// The turn the stick asks for, body frame: a small rotator made a
		// rotation vector, so the signs are the rotator signs.
		constexpr double Probe = 1.0e-3;
		const FVector3d Rate = FRotator3d(Stick.Turn.X * Handling.PitchRate * Probe, Stick.Turn.Y * Handling.YawRate * Probe,
			Stick.Turn.Z * Handling.RollRate * Probe).Quaternion().ToRotationVector() / Probe;
		const FVector3d Omega = State.Spin.AngularVelocity;
		if (Mode == ELedgerFlightMode::AssistOff)
		{
			// The stick is torque: full deflection is the angular acceleration
			// that would reach the full rate in one hold time, and nothing
			// takes it away again.
			Out.Torque = AngularMomentum(Mass.Inertia, Rate * Handling.RateHoldPerSecond);
		}
		else
		{
			// The rate held: most of the gap closed each step whatever the
			// step, plus what the spin needs to keep itself (omega x I omega).
			const double Close = 1.0 - FMath::Exp(-Handling.RateHoldPerSecond * DeltaSeconds);
			Out.Torque = AngularMomentum(Mass.Inertia, (Rate - Omega) * (Close / DeltaSeconds))
				+ FVector3d::CrossProduct(Omega, AngularMomentum(Mass.Inertia, Omega));
		}
		FVector3d Acceleration = Stick.Push
			* FVector3d(Handling.MainAcceleration, Handling.ManoeuvringAcceleration, Handling.ManoeuvringAcceleration);
		if (Mode == ELedgerFlightMode::Coupled)
		{
			// Drift across the nose, sideways and vertically, closed the same
			// way -- unless the stick is asking for it.
			const FVector3d Body = State.Spin.Orientation.UnrotateVector(State.Velocity);
			const double Hold = (1.0 - FMath::Exp(-Handling.DriftHoldPerSecond * DeltaSeconds)) / DeltaSeconds;
			if (FMath::IsNearlyZero(Stick.Push.Y))
			{
				Acceleration.Y -= Body.Y * Hold;
			}
			if (FMath::IsNearlyZero(Stick.Push.Z))
			{
				Acceleration.Z -= Body.Z * Hold;
			}
		}
		Out.Force = Acceleration * Mass.MassKg;
		return Out;
	}

	FLedgerAllocation Push(const TArray<FLedgerNozzle>& Nozzles, const TArray<double>& LimitNewtons,
		const FLedgerMassProperties& Mass, const FLedgerCommand& Command, FLedgerMotion& State, double DeltaSeconds)
	{
		const FLedgerAllocation Given = Allocate(Nozzles, LimitNewtons, Mass.CentreMetres, Command.Force, Command.Torque);
		if (Mass.MassKg > 0.0 && DeltaSeconds > 0.0)
		{
			Rotate(State.Spin, Mass.Inertia, Given.Torque, DeltaSeconds);
			State.Velocity += State.Spin.Orientation.RotateVector(Given.Force / Mass.MassKg) * DeltaSeconds;
		}
		return Given;
	}
}
