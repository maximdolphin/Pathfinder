// Rotation that answers to the inertia tensor. T132.

#include "LedgerRigidBody.h"
#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerRigidResponse,
	"Ledger.Flight.AngularResponseMatchesTheInertiaTensor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerRigidResponse::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}
	const FLedgerMassProperties Mass = LedgerShipSystems::MassProperties(Ship, FLedgerShipState::For(Ship));

	// A small torque about each axis for a second, from rest: the rate reached
	// is I^-1 tau t, to the precision the gyroscopic term allows at that rate.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FVector3d Torque = FVector3d::ZeroVector;
		Torque[Axis] = 500.0;
		FLedgerSpin Spin;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			LedgerFlight::Rotate(Spin, Mass.Inertia, Torque, 1.0 / 120.0);
		}
		const FVector3d Expected = Mass.Respond(Torque);
		AddInfo(FString::Printf(TEXT("axis %d: %.6f rad/s reached against %.6f from the tensor"),
			Axis, Spin.AngularVelocity[Axis], Expected[Axis]));
		TestTrue(FString::Printf(TEXT("the response about axis %d is the tensor's"), Axis),
			(Spin.AngularVelocity - Expected).Length() < 1.0e-4 * Expected.Length());
	}

	// An off-axis thrust: the main engine's push, applied a metre to starboard
	// of the centre of mass, yaws the ship by exactly r x F.
	const FVector3d Force(2.0e4, 0.0, 0.0);
	const FVector3d Point = Mass.CentreMetres + FVector3d(0.0, 1.0, 0.0);
	const FVector3d Torque = LedgerFlight::TorqueOf(Point, Force, Mass.CentreMetres);
	TestTrue(TEXT("a push a metre to starboard makes a pure yaw torque of F newton metres, to port"),
		FMath::IsNearlyEqual(Torque.Z, -Force.X) && FMath::Abs(Torque.X) < 1.0e-9 && FMath::Abs(Torque.Y) < 1.0e-9);
	FLedgerSpin Yaw;
	for (int32 Step = 0; Step < 60; ++Step)
	{
		LedgerFlight::Rotate(Yaw, Mass.Inertia, Torque, 1.0 / 120.0);
	}
	const FVector3d Predicted = Mass.Respond(Torque) * 0.5;
	AddInfo(FString::Printf(TEXT("off-axis thrust: torque (%.0f, %.0f, %.0f) N m; after half a second %.5f rad/s of yaw against %.5f predicted"),
		Torque.X, Torque.Y, Torque.Z, Yaw.AngularVelocity.Z, Predicted.Z));
	TestTrue(TEXT("and it turns the ship at the rate the geometry predicts"), (Yaw.AngularVelocity - Predicted).Length() < 1.0e-3 * Predicted.Length());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerRigidFree,
	"Ledger.Flight.ATorqueFreeBodyKeepsItsMomentumAndEnergy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerRigidFree::RunTest(const FString&)
{
	// Three different moments, spun near the unstable middle axis: it tumbles,
	// and a correct integrator keeps the momentum and the energy while it does.
	const double Inertia[3][3] = { { 1000.0, 0.0, 0.0 }, { 0.0, 2000.0, 0.0 }, { 0.0, 0.0, 3000.0 } };
	FLedgerSpin Spin;
	Spin.AngularVelocity = FVector3d(0.01, 1.0, 0.01);
	const FVector3d Momentum = LedgerFlight::AngularMomentum(Inertia, Spin.AngularVelocity);
	const FVector3d MomentumWorld = Spin.Orientation.RotateVector(Momentum);
	const double Energy = LedgerFlight::RotationalEnergy(Inertia, Spin.AngularVelocity);
	double Flipped = 0.0;
	for (int32 Step = 0; Step < 60 * 120; ++Step)
	{
		LedgerFlight::Rotate(Spin, Inertia, FVector3d::ZeroVector, 1.0 / 120.0);
		Flipped = FMath::Min(Flipped, Spin.AngularVelocity.Y);
	}
	const FVector3d MomentumAfter = Spin.Orientation.RotateVector(LedgerFlight::AngularMomentum(Inertia, Spin.AngularVelocity));
	const double EnergyAfter = LedgerFlight::RotationalEnergy(Inertia, Spin.AngularVelocity);
	AddInfo(FString::Printf(TEXT("a minute of tumbling: the middle-axis rate went as low as %.3f; momentum drifted %.2e, energy %.2e"),
		Flipped, (MomentumAfter - MomentumWorld).Length() / Momentum.Length(), FMath::Abs(EnergyAfter - Energy) / Energy));
	TestTrue(TEXT("spun about its middle axis it tumbles, as a real body does"), Flipped < -0.5);
	TestTrue(TEXT("and keeps its angular momentum in the world"), (MomentumAfter - MomentumWorld).Length() < 1.0e-3 * Momentum.Length());
	TestTrue(TEXT("and its energy"), FMath::Abs(EnergyAfter - Energy) < 1.0e-5 * Energy);
	return true;
}

namespace
{
	bool AllocationCourier(FAutomationTestBase& Test, FLedgerShipDefinition& Ship, FLedgerMassProperties& Mass, TArray<double>& Limits)
	{
		TArray<FString> Errors;
		const bool bLoaded = LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors);
		for (const FString& Error : Errors)
		{
			Test.AddError(Error);
		}
		if (!bLoaded || Ship.Nozzles.Num() == 0)
		{
			return false;
		}
		Mass = LedgerShipSystems::MassProperties(Ship, FLedgerShipState::For(Ship));
		Limits.Reset();
		for (const FLedgerNozzle& Nozzle : Ship.Nozzles)
		{
			Limits.Add(Nozzle.ThrustNewtons);
		}
		return true;
	}

	bool AllocationWithin(const FLedgerAllocation& Allocation, const TArray<double>& Limits)
	{
		for (int32 Index = 0; Index < Limits.Num(); ++Index)
		{
			if (Allocation.ThrustNewtons[Index] < 0.0 || Allocation.ThrustNewtons[Index] > Limits[Index] * (1.0 + 1.0e-9))
			{
				return false;
			}
		}
		return true;
	}

	/// The most torque about an axis the layout has: every nozzle that turns
	/// the ship that way, at full, and none that does not.
	double AllocationMostTorque(const TArray<FLedgerNozzle>& Nozzles, const TArray<double>& Limits,
		const FVector3d& Centre, const FVector3d& Axis)
	{
		double Most = 0.0;
		for (int32 Index = 0; Index < Nozzles.Num(); ++Index)
		{
			Most += FMath::Max(0.0, FVector3d::DotProduct(
				LedgerFlight::TorqueOf(Nozzles[Index].PositionMetres, Nozzles[Index].Push * Limits[Index], Centre), Axis));
		}
		return Most;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAllocationMeets,
	"Ledger.Flight.TheAllocatorMakesTheCommandedForceAndTorque",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAllocationMeets::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	FLedgerMassProperties Mass;
	TArray<double> Limits;
	if (!TestTrue(TEXT("the courier loads, with nozzles"), AllocationCourier(*this, Ship, Mass, Limits)))
	{
		return false;
	}
	const FVector3d Centre = Mass.CentreMetres;
	const double M = Mass.MassKg;
	const double Pitch = AllocationMostTorque(Ship.Nozzles, Limits, Centre, FVector3d(0.0, 1.0, 0.0));
	const double Yaw = AllocationMostTorque(Ship.Nozzles, Limits, Centre, FVector3d(0.0, 0.0, 1.0));
	const double Roll = AllocationMostTorque(Ship.Nozzles, Limits, Centre, FVector3d(1.0, 0.0, 0.0));
	AddInfo(FString::Printf(TEXT("courier, %d nozzles, %.1f t: at most %.1f MN m of pitch, %.1f of yaw, %.1f of roll"),
		Ship.Nozzles.Num(), M / 1000.0, Pitch / 1.0e6, Yaw / 1.0e6, Roll / 1.0e6));

	struct FCommand
	{
		const TCHAR* What;
		FVector3d Force;
		FVector3d Torque;
	};
	const FCommand Commands[] =
	{
		{ TEXT("a third of its pitch"), FVector3d::ZeroVector, FVector3d(0.0, Pitch / 3.0, 0.0) },
		{ TEXT("a third of its yaw"), FVector3d::ZeroVector, FVector3d(0.0, 0.0, -Yaw / 3.0) },
		{ TEXT("a third of its roll"), FVector3d::ZeroVector, FVector3d(Roll / 3.0, 0.0, 0.0) },
		{ TEXT("strafe and yaw together"), FVector3d(0.0, 200.0 * M, 0.0), FVector3d(0.0, 0.0, Yaw / 5.0) },
		{ TEXT("hover against gravity"), FVector3d(0.0, 0.0, 9.81 * M), FVector3d::ZeroVector },
		{ TEXT("half the main engine, held straight"), FVector3d(0.5 * Ship.Flight.MainThrust * M, 0.0, 0.0), FVector3d::ZeroVector },
		{ TEXT("nothing"), FVector3d::ZeroVector, FVector3d::ZeroVector },
	};
	for (const FCommand& Command : Commands)
	{
		const FLedgerAllocation Allocation = LedgerFlight::Allocate(Ship.Nozzles, Limits, Centre, Command.Force, Command.Torque);
		int32 Firing = 0;
		double Total = 0.0;
		for (const double Thrust : Allocation.ThrustNewtons)
		{
			Firing += Thrust > 0.0 ? 1 : 0;
			Total += Thrust;
		}
		AddInfo(FString::Printf(TEXT("%s: force off by %.3g N, torque off by %.3g N m, %d nozzles firing, %.2f MN in all"),
			Command.What, (Allocation.Force - Command.Force).Length(), (Allocation.Torque - Command.Torque).Length(), Firing, Total / 1.0e6));
		TestTrue(FString::Printf(TEXT("%s is made"), Command.What), Allocation.bMet);
		TestTrue(FString::Printf(TEXT("%s keeps every nozzle in its range"), Command.What), AllocationWithin(Allocation, Limits));
		if (Command.Force.IsZero() && Command.Torque.IsZero())
		{
			TestTrue(TEXT("and asked for nothing, nothing fires"), Total == 0.0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAllocationLayout,
	"Ledger.Flight.HandlingFollowsTheThrusterLayout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAllocationLayout::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	FLedgerMassProperties Mass;
	TArray<double> Limits;
	if (!TestTrue(TEXT("the courier loads, with nozzles"), AllocationCourier(*this, Ship, Mass, Limits)))
	{
		return false;
	}
	const FVector3d Centre = Mass.CentreMetres;
	const FVector3d Axis(0.0, 1.0, 0.0);
	const int32 MainEngine = Ship.FindComponent(TEXT("main_engine"));

	// Asked for far more pitch than it has, it gives what its nozzles have.
	auto Reach = [&Limits, &Centre, &Axis](const TArray<FLedgerNozzle>& Nozzles, double& Most)
	{
		Most = AllocationMostTorque(Nozzles, Limits, Centre, Axis);
		return LedgerFlight::Allocate(Nozzles, Limits, Centre, FVector3d::ZeroVector, Axis * (Most * 1000.0)).Torque;
	};
	double FullMost = 0.0;
	const FVector3d Full = Reach(Ship.Nozzles, FullMost);

	// The same nozzles, half as far from the centre along the ship.
	TArray<FLedgerNozzle> Closer = Ship.Nozzles;
	for (FLedgerNozzle& Nozzle : Closer)
	{
		if (Nozzle.Component != MainEngine)
		{
			Nozzle.PositionMetres.X = Centre.X + (Nozzle.PositionMetres.X - Centre.X) * 0.5;
		}
	}
	double HalfMost = 0.0;
	const FVector3d Half = Reach(Closer, HalfMost);
	AddInfo(FString::Printf(TEXT("the most pitch: %.2f MN m against %.2f its nozzles have; moved in halfway, %.2f against %.2f"),
		Full.Y / 1.0e6, FullMost / 1.0e6, Half.Y / 1.0e6, HalfMost / 1.0e6));
	TestTrue(TEXT("the most pitch it makes is the most its nozzles have"),
		FMath::Abs(Full.Y - FullMost) < 0.01 * FullMost && FMath::Abs(Half.Y - HalfMost) < 0.01 * HalfMost);
	TestTrue(TEXT("and it is pitch, not some other turn"), Full.GetSafeNormal().Y > 0.99);
	TestTrue(TEXT("nozzles moved in make a ship that pitches less"), Half.Y < 0.7 * Full.Y);

	// The nozzle that pitches it most, gone: the rest make the same turn.
	int32 Lost = INDEX_NONE;
	double Strongest = 0.0;
	for (int32 Index = 0; Index < Ship.Nozzles.Num(); ++Index)
	{
		const double Share = FVector3d::DotProduct(LedgerFlight::TorqueOf(Ship.Nozzles[Index].PositionMetres,
			Ship.Nozzles[Index].Push * Limits[Index], Centre), Axis);
		if (Share > Strongest)
		{
			Strongest = Share;
			Lost = Index;
		}
	}
	TArray<double> Short = Limits;
	Short[Lost] = 0.0;
	const FLedgerAllocation Intact = LedgerFlight::Allocate(Ship.Nozzles, Limits, Centre, FVector3d::ZeroVector, Axis * (0.2 * FullMost));
	const FLedgerAllocation Without = LedgerFlight::Allocate(Ship.Nozzles, Short, Centre, FVector3d::ZeroVector, Axis * (0.2 * FullMost));
	AddInfo(FString::Printf(TEXT("a fifth of its pitch: %.2f MN from nozzle %d intact; with it gone, %.2f MN from the rest, torque off by %.3g N m"),
		Intact.ThrustNewtons[Lost] / 1.0e6, Lost, Without.ThrustNewtons[Lost] / 1.0e6, (Without.Torque - Axis * (0.2 * FullMost)).Length()));
	TestTrue(TEXT("with its strongest pitch nozzle gone it makes the same turn another way"),
		Without.bMet && Without.ThrustNewtons[Lost] == 0.0 && Intact.ThrustNewtons[Lost] > 0.0);

	// And the authority it loses is where the geometry says: that nozzle is
	// share of the pitch, and none of the yaw it never gave.
	const FVector3d YawAxis(0.0, 0.0, 1.0);
	const double YawBefore = AllocationMostTorque(Ship.Nozzles, Limits, Centre, YawAxis);
	const double YawShare = FMath::Max(0.0, FVector3d::DotProduct(LedgerFlight::TorqueOf(Ship.Nozzles[Lost].PositionMetres,
		Ship.Nozzles[Lost].Push * Limits[Lost], Centre), YawAxis));
	const double PitchLeft = LedgerFlight::Allocate(Ship.Nozzles, Short, Centre, FVector3d::ZeroVector, Axis * (FullMost * 1000.0)).Torque.Y;
	const double YawLeft = LedgerFlight::Allocate(Ship.Nozzles, Short, Centre, FVector3d::ZeroVector, YawAxis * (YawBefore * 1000.0)).Torque.Z;
	AddInfo(FString::Printf(TEXT("the most pitch with it gone: %.2f MN m against %.2f predicted (%.2f less its %.2f); the most yaw %.2f against %.2f"),
		PitchLeft / 1.0e6, (FullMost - Strongest) / 1.0e6, FullMost / 1.0e6, Strongest / 1.0e6, YawLeft / 1.0e6, (YawBefore - YawShare) / 1.0e6));
	TestTrue(TEXT("it loses the pitch that nozzle gave, and no more"), FMath::Abs(PitchLeft - (FullMost - Strongest)) < 0.01 * FullMost);
	TestTrue(TEXT("and keeps the yaw it did not give"), FMath::Abs(YawLeft - (YawBefore - YawShare)) < 0.01 * YawBefore);
	return true;
}

#endif
