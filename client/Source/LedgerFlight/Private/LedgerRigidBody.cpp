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

	static FLedgerAllocation Solve(const TArray<FLedgerNozzle>& Nozzles, const TArray<double>& LimitNewtons,
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

	FLedgerAllocation Allocate(const TArray<FLedgerNozzle>& Nozzles, const TArray<double>& LimitNewtons,
		const FVector3d& CentreMetres, const FVector3d& ForceNewtons, const FVector3d& TorqueNewtonMetres)
	{
		const FLedgerAllocation Whole = Solve(Nozzles, LimitNewtons, CentreMetres, ForceNewtons, TorqueNewtonMetres);
		if (Whole.bMet)
		{
			return Whole;
		}
		// **Short, the turn comes first (M5P).** One least-squares answer shared
		// the shortfall, so a coupled turn at speed -- asking the side nozzles
		// for more sideways push than they have -- gave up the yaw they also
		// make, and the ship lurched at up to 73 deg/s. Now the torque is made
		// whole, then force an axis at a time -- along the nose, up, sideways --
		// each as much as still fits. (A heavier torque weight would do it in one
		// solve, but leaves the fore and aft pairs near opposite and coordinate
		// descent crawling.) ponytail: up to 21 solves, only while short; an
		// active-set solver with priorities if it ever shows in the frame.
		FLedgerAllocation Held = Solve(Nozzles, LimitNewtons, CentreMetres, FVector3d::ZeroVector, TorqueNewtonMetres);
		if (!Held.bMet)
		{
			return Whole;
		}
		FVector3d Given = FVector3d::ZeroVector;
		for (const int32 Axis : { 0, 2, 1 })
		{
			const double Asked = ForceNewtons[Axis];
			if (Asked == 0.0)
			{
				continue;
			}
			double Low = 0.0;
			double High = 1.0;
			for (int32 Halving = 0; Halving < 7; ++Halving)
			{
				const double Try = Halving == 0 ? 1.0 : 0.5 * (Low + High);
				FVector3d Force = Given;
				Force[Axis] = Asked * Try;
				const FLedgerAllocation Attempt = Solve(Nozzles, LimitNewtons, CentreMetres, Force, TorqueNewtonMetres);
				if (Attempt.bMet)
				{
					Low = Try;
					Held = Attempt;
					if (Try == 1.0)
					{
						break;
					}
				}
				else
				{
					High = Try;
				}
			}
			Given[Axis] = Asked * Low;
		}
		Held.bMet = false;
		return Held;
	}

	FLedgerCommand Control(ELedgerFlightMode Mode, const FLedgerStick& Stick, const FLedgerHandling& Handling,
		const FLedgerMassProperties& Mass, const FLedgerMotion& State, double DeltaSeconds,
		const FVector3d& ExternalTorque)
	{
		FLedgerCommand Out;
		if (Mass.MassKg <= 0.0 || DeltaSeconds <= 0.0)
		{
			return Out;
		}
		// The turn the stick asks for, body frame: a small rotator made a
		// rotation vector, so the signs are the rotator signs.
		constexpr double Probe = 1.0e-3;
		FVector3d Rate = FRotator3d(Stick.Turn.X * Handling.PitchRate * Probe, Stick.Turn.Y * Handling.YawRate * Probe,
			Stick.Turn.Z * Handling.RollRate * Probe).Quaternion().ToRotationVector() / Probe;
		if (Mode == ELedgerFlightMode::Coupled)
		{
			// **Coupled, the nose turns no faster than the velocity can follow
			// (M5P).** Swinging it at v takes v*omega of push across it; past what
			// the manoeuvring thrusters give, the ship slid through the turn --
			// 110 m/s sideways after 22 deg/s at 150 m/s. Four fifths of it, to
			// leave room for holding the ship up. Roll turns no velocity.
			const double Swing = FMath::Sqrt(Rate.Y * Rate.Y + Rate.Z * Rate.Z);
			const double Most = 0.8 * Handling.ManoeuvringAcceleration / FMath::Max(State.Velocity.Length(), 1.0);
			if (Swing > Most)
			{
				Rate.Y *= Most / Swing;
				Rate.Z *= Most / Swing;
			}
		}
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
			// Less what the air is already doing, so the nozzles give only the rest.
			Out.Torque = AngularMomentum(Mass.Inertia, (Rate - Omega) * (Close / DeltaSeconds))
				+ FVector3d::CrossProduct(Omega, AngularMomentum(Mass.Inertia, Omega)) - ExternalTorque;
		}
		FVector3d Acceleration = Stick.Push
			* FVector3d(Handling.MainAcceleration, Handling.ManoeuvringAcceleration, Handling.ManoeuvringAcceleration);
		if (Mode == ELedgerFlightMode::Coupled)
		{
			// **Across the nose the stick is a velocity (M5P).** Centred, drift
			// sideways and vertically is closed; deflected, the ship is brought
			// to that fraction of CoupledSideSpeed and held there, within what
			// the manoeuvring thrusters can give. It used to be an acceleration
			// with the hold switched off while pushed, so a held key never
			// stopped adding speed.
			const FVector3d Body = State.Spin.Orientation.UnrotateVector(State.Velocity);
			const double Hold = (1.0 - FMath::Exp(-Handling.DriftHoldPerSecond * DeltaSeconds)) / DeltaSeconds;
			// Unclamped: the allocator gives what the nozzles can, and clamping
			// here as well changed how it shared that out (the modes test saw it).
			Acceleration.Y = (Stick.Push.Y * Handling.CoupledSideSpeed - Body.Y) * Hold;
			Acceleration.Z = (Stick.Push.Z * Handling.CoupledSideSpeed - Body.Z) * Hold;
			// Along it the throttle is a speed too: full forward CoupledForwardSpeed,
			// full back CoupledSideSpeed astern, centred none -- so letting go stops
			// the ship on the retro pair. It used to stop adding and coast, and a
			// ship that slid backwards after a pitch-up was sped up by the brake.
			const double Forward = Stick.Push.X * (Stick.Push.X >= 0.0 ? Handling.CoupledForwardSpeed : Handling.CoupledSideSpeed);
			Acceleration.X = (Forward - Body.X) * Hold;
		}
		Out.Force = Acceleration * Mass.MassKg;
		return Out;
	}

	FLedgerAllocation Push(const TArray<FLedgerNozzle>& Nozzles, const TArray<double>& LimitNewtons,
		const FLedgerMassProperties& Mass, const FLedgerCommand& Command, FLedgerMotion& State, double DeltaSeconds,
		const FLedgerCommand& External)
	{
		const FLedgerAllocation Given = Allocate(Nozzles, LimitNewtons, Mass.CentreMetres, Command.Force, Command.Torque);
		if (Mass.MassKg > 0.0 && DeltaSeconds > 0.0)
		{
			Rotate(State.Spin, Mass.Inertia, Given.Torque + External.Torque, DeltaSeconds);
			State.Velocity += State.Spin.Orientation.RotateVector((Given.Force + External.Force) / Mass.MassKg) * DeltaSeconds;
		}
		return Given;
	}

	FLedgerCommand Aerodynamics(const FLedgerShipAero& Aero, double BellyAreaM2, const FLedgerMotion& Motion,
		const FVector3d& WindMetresPerSecond, double DensityKgPerM3, const FVector3d& Surfaces, FLedgerAeroState* OutState)
	{
		// A sideslip pushes back sideways at this, per radian, on the wing area.
		constexpr double SideForce = 0.3;
		// What a stalled half-wing adds to its drag, over its area.
		constexpr double StallDragRise = 0.3;

		FLedgerCommand Out;
		FLedgerAeroState State;
		const FVector3d Air = Motion.Spin.Orientation.UnrotateVector(Motion.Velocity - WindMetresPerSecond);
		const double Speed = Air.Length();
		if (DensityKgPerM3 > 0.0 && Speed > 0.1)
		{
			const double Q = 0.5 * DensityKgPerM3 * Speed * Speed;
			const FVector3d Along = Air / Speed;
			State.DynamicPressure = Q;
			State.AngleOfAttack = FMath::Atan2(-Air.Z, Air.X);
			State.Sideslip = FMath::Asin(FMath::Clamp(Along.Y, -1.0, 1.0));

			// The body: a flat plate pushing back on the part of the flow that
			// meets it square.
			Out.Force.Z -= 0.5 * DensityKgPerM3 * BellyAreaM2 * Air.Z * FMath::Abs(Air.Z);

			if (Aero.bWinged && Aero.WingAreaM2 > 0.0 && Aero.SpanMetres > 0.0)
			{
				const double Area = Aero.WingAreaM2;
				const double Span = Aero.SpanMetres;
				const double Chord = Area / Span;
				const double Ratio = Span * Span / Area;
				const double Alpha = State.AngleOfAttack;

				// The lift slope of a finite wing (Helmbold), attached up to the
				// stall and gone five degrees past it: beyond, the belly is all
				// the lift there is.
				const double Slope = UE_DOUBLE_TWO_PI * Ratio / (2.0 + FMath::Sqrt(Ratio * Ratio + 4.0));
				const double Stall = FMath::DegreesToRadians(Aero.StallDegrees);
				auto Panel = [&](double LocalAlpha, double& OutLift, double& OutDrag)
				{
					const double Past = FMath::Clamp((FMath::Abs(LocalAlpha) - Stall) / FMath::DegreesToRadians(5.0), 0.0, 1.0);
					OutLift = Slope * FMath::Sin(LocalAlpha) * (1.0 - Past);
					OutDrag = Aero.ZeroLiftDrag + OutLift * OutLift / (UE_DOUBLE_PI * Aero.Oswald * Ratio) + StallDragRise * Past;
					return Past;
				};

				// T136: two half-wings, each flying its own air. Rolling, the wing
				// going down meets the air from further below; yawing, the outer
				// wing moves faster. Below the stall the down-going wing lifts
				// more, which is roll damping; past it, it lifts less and drags
				// more, and the ship rolls and yaws into it by itself -- the spin.
				const FVector3d PitchUp = FRotator3d(1.0, 0.0, 0.0).Quaternion().ToRotationVector().GetSafeNormal();
				const FVector3d YawRight = FRotator3d(0.0, 1.0, 0.0).Quaternion().ToRotationVector().GetSafeNormal();
				const FVector3d RollRight = FRotator3d(0.0, 0.0, 1.0).Quaternion().ToRotationVector().GetSafeNormal();
				const FVector3d Omega = Motion.Spin.AngularVelocity;
				// The right half at +y and the left at -y, a quarter-span out: a roll
				// lifts one into air from above and drops the other into air from
				// below, a yaw moves one forward and the other back. Straight off the
				// body rates, and the moments below straight off the forces, so no sign
				// convention stands between the model and the ship.
				const double Arm = Span * 0.25;
				const double AlphaRight = Alpha - Omega.X * Arm / Speed;
				const double AlphaLeft = Alpha + Omega.X * Arm / Speed;
				const double SpeedRight = FMath::Max(Speed - Omega.Z * Arm, 0.0);
				const double SpeedLeft = FMath::Max(Speed + Omega.Z * Arm, 0.0);
				double LiftRight = 0.0, DragRight = 0.0, LiftLeft = 0.0, DragLeft = 0.0;
				const double PastRight = Panel(AlphaRight, LiftRight, DragRight);
				const double PastLeft = Panel(AlphaLeft, LiftLeft, DragLeft);
				const double HalfRight = 0.5 * DensityKgPerM3 * SpeedRight * SpeedRight * Area * 0.5;
				const double HalfLeft = 0.5 * DensityKgPerM3 * SpeedLeft * SpeedLeft * Area * 0.5;
				State.Lift = (LiftRight + LiftLeft) * 0.5;
				State.Drag = (DragRight + DragLeft) * 0.5;
				State.bStalled = PastRight > 0.0 || PastLeft > 0.0;
				const FVector3d Lifting = FVector3d::CrossProduct(Along, FVector3d::UnitY()).GetSafeNormal();
				const FVector3d RightForce = Lifting * (LiftRight * HalfRight) - Along * (DragRight * HalfRight);
				const FVector3d LeftForce = Lifting * (LiftLeft * HalfLeft) - Along * (DragLeft * HalfLeft);
				Out.Force += RightForce + LeftForce;
				Out.Force.Y -= SideForce * State.Sideslip * Q * Area;
				const FVector3d WingTorque = FVector3d::CrossProduct(FVector3d(0.0, Arm, 0.0), RightForce)
					+ FVector3d::CrossProduct(FVector3d(0.0, -Arm, 0.0), LeftForce);

				// Moments about the axes the stick turns the ship on, so the
				// surfaces and the rate hold agree about which way is which.
				const double Pitching = Q * Area * Chord * (Aero.PitchMoment0 + Aero.PitchStability * Alpha
					+ Aero.PitchDamping * FVector3d::DotProduct(Omega, PitchUp) * Chord / (2.0 * Speed)
					+ Aero.Elevator * Surfaces.X);
				const double Yawing = Q * Area * Span * (Aero.YawStability * State.Sideslip
					+ Aero.YawDamping * FVector3d::DotProduct(Omega, YawRight) * Span / (2.0 * Speed)
					+ Aero.Rudder * Surfaces.Y);
				const double Rolling = Q * Area * Span * (Aero.RollDamping * FVector3d::DotProduct(Omega, RollRight) * Span / (2.0 * Speed)
					+ Aero.Ailerons * Surfaces.Z);
				Out.Torque = PitchUp * Pitching + YawRight * Yawing + RollRight * Rolling + WingTorque;
			}
		}
		if (OutState != nullptr)
		{
			*OutState = State;
		}
		return Out;
	}
}
