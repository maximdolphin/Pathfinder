// The reason the flight model was extracted: these run in milliseconds, with no
// world, no actor, no frame and no planet.
//
// Before the split there was no way to ask "does a ship in a stable orbit stay
// in one" except by flying for a minute and looking. Every one of these was a
// question that used to need the scripted flight to answer.

#include "LedgerFlightModel.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// Earth, near enough: 6,371 km and 9.81 m/s^2, with no atmosphere unless a
	/// test asks for one.
	FLedgerGravityField EarthLike()
	{
		FLedgerGravityField Field;
		Field.Centre = FVector3d::ZeroVector;
		Field.Radius = 637100000.0;
		Field.SurfaceGravity = 981.0;
		Field.DragScaleHeight = 800000.0;
		Field.AtmosphericDrag = 0.0;
		return Field;
	}

	FLedgerFlightState AtAltitude(const FLedgerGravityField& Field, double Metres)
	{
		FLedgerFlightState State;
		State.Position = FVector3d(0.0, 0.0, Field.Radius + Metres * 100.0);
		return State;
	}

	void Run(FLedgerFlightState& State, const FLedgerGravityField& Field,
		double Seconds, double Step = 1.0 / 60.0)
	{
		for (double Elapsed = 0.0; Elapsed < Seconds; Elapsed += Step)
		{
			LedgerFlight::Integrate(State, Field, Step);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFlightFallsAtG,
	"Ledger.Flight.FallsAtSurfaceGravity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFlightFallsAtG::RunTest(const FString&)
{
	const FLedgerGravityField Field = EarthLike();
	FLedgerFlightState State = AtAltitude(Field, 10000.0);

	Run(State, Field, 1.0);

	// One second of free fall is one g of velocity, near enough that the only
	// difference is the integrator's own step error.
	const double Speed = State.Velocity.Length();
	TestTrue(TEXT("one second of fall is about 9.81 m/s"),
		FMath::Abs(Speed - 981.0) < 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFlightGravityFallsOff,
	"Ledger.Flight.GravityFallsOffWithSquare",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFlightGravityFallsOff::RunTest(const FString&)
{
	const FLedgerGravityField Field = EarthLike();

	// At twice the distance, gravity is a quarter. Newton, and the reason
	// leaving is expensive near the ground and cheap once you are up.
	//
	// Both samples are well clear of the surface, because the first version of
	// this test put the near one *at* it — where ground contact zeroed the
	// velocity before it could be measured, and the test failed for the one
	// reason that would have meant the model was right.
	FLedgerFlightState Near;
	Near.Position = FVector3d(0.0, 0.0, Field.Radius * 2.0);
	FLedgerFlightState Far;
	Far.Position = FVector3d(0.0, 0.0, Field.Radius * 4.0);

	LedgerFlight::Integrate(Near, Field, 1.0);
	LedgerFlight::Integrate(Far, Field, 1.0);

	const double Ratio = Near.Velocity.Length() / Far.Velocity.Length();
	TestTrue(TEXT("gravity at twice the distance is a quarter"),
		FMath::Abs(Ratio - 4.0) < 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFlightLandsAndStops,
	"Ledger.Flight.LandsAndStops",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFlightLandsAndStops::RunTest(const FString&)
{
	const FLedgerGravityField Field = EarthLike();
	FLedgerFlightState State = AtAltitude(Field, 200.0);

	Run(State, Field, 60.0);

	TestTrue(TEXT("the ship is landed"), State.bLanded);

	const double Height = LedgerFlight::AltitudeAbove(State, Field);
	TestTrue(TEXT("it rests on the gear rather than in the ground"),
		FMath::Abs(Height - State.GearHeight) < 1.0);

	// Friction scrubs the tangential remainder off, so a ship that has settled
	// is not still sliding.
	TestTrue(TEXT("it has stopped"), State.Velocity.Length() < 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFlightNeverSinks,
	"Ledger.Flight.NeverSinksThroughTerrain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFlightNeverSinks::RunTest(const FString&)
{
	// Straight down at three kilometres a second, which is faster than any
	// streaming collision would have cooked for. This is the case the height
	// function exists to answer and a trace would have missed.
	FLedgerGravityField Field = EarthLike();
	FLedgerFlightState State = AtAltitude(Field, 40000.0);
	State.Velocity = FVector3d(0.0, 0.0, -300000.0);

	for (int32 Step = 0; Step < 6000; ++Step)
	{
		LedgerFlight::Integrate(State, Field, 1.0 / 60.0);
		const double Height = LedgerFlight::AltitudeAbove(State, Field);
		if (Height < State.GearHeight - 1.0)
		{
			AddError(FString::Printf(
				TEXT("sank to %.1f cm below the gear on step %d"),
				State.GearHeight - Height, Step));
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFlightDragOnlyInAir,
	"Ledger.Flight.DragOnlyInsideTheAtmosphere",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFlightDragOnlyInAir::RunTest(const FString&)
{
	FLedgerGravityField Field = EarthLike();
	Field.AtmosphericDrag = 0.55;

	// Sideways, so gravity does not confuse the measurement.
	FLedgerFlightState Low = AtAltitude(Field, 1000.0);
	Low.Velocity = FVector3d(100000.0, 0.0, 0.0);
	FLedgerFlightState High = AtAltitude(Field, 400000.0);
	High.Velocity = FVector3d(100000.0, 0.0, 0.0);

	const double LowBefore = Low.Velocity.X;
	const double HighBefore = High.Velocity.X;
	LedgerFlight::Integrate(Low, Field, 1.0);
	LedgerFlight::Integrate(High, Field, 1.0);

	TestTrue(TEXT("the thick air slows it"), Low.Velocity.X < LowBefore * 0.9);
	TestTrue(TEXT("four hundred kilometres up it coasts"),
		High.Velocity.X > HighBefore * 0.999);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFlightIsDeterministic,
	"Ledger.Flight.IsDeterministic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFlightIsDeterministic::RunTest(const FString&)
{
	const FLedgerGravityField Field = EarthLike();

	FLedgerFlightState First = AtAltitude(Field, 50000.0);
	First.Velocity = FVector3d(120000.0, -40000.0, 15000.0);
	FLedgerFlightState Second = First;

	Run(First, Field, 120.0);
	Run(Second, Field, 120.0);

	TestTrue(TEXT("two hours of the same inputs land in the same place"),
		First.Position.Equals(Second.Position, 0.0));
	return true;
}

#endif
