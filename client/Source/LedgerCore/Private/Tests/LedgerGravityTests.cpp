// Coasting between two bodies. T083.
//
// The acceptance has two halves and they pull against each other. A ship must
// follow what the TWO-body model predicts -- which is the analytic Kepler orbit
// around whichever body dominates -- while actually moving under a field summed
// from every body there is. The gap between them is the perturbation, and
// measuring it is the point: too big and the two-body model is a lie, zero and
// the summed field is not summing anything.

#include "LedgerGravity.h"
#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGravityCoast,
	"Ledger.Gravity.ACoastFollowsTheTwoBodyPrediction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGravityCoast::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const FLedgerBody& Body = System.Bodies[Planet];

	// A circular orbit 2000 km up. The speed comes from the physics rather than
	// being chosen: sqrt(GM/r) is what keeps a circle a circle.
	const double Radius = Body.RadiusMetres + 2.0e6;
	const double Mu = LedgerEphemeris::GravitationalConstant * Body.MassKg;
	const double Speed = FMath::Sqrt(Mu / Radius);
	const double Period = LedgerTwoPi * FMath::Sqrt(Radius * Radius * Radius / Mu);

	AddInfo(FString::Printf(
		TEXT("a circular orbit at %.0f km altitude: %.0f m/s, period %.1f minutes"),
		(Radius - Body.RadiusMetres) / 1000.0, Speed, Period / 60.0));

	TArray<FLedgerState> Start;
	LedgerEphemeris::StatesAt(System, 0.0, Start);

	// Started in the plane of the planet's own orbit, offset along a direction
	// that is not any axis, so a sign error anywhere has somewhere to show.
	const FVector3d Out = FVector3d(0.6, 0.48, 0.64).GetSafeNormal();
	FVector3d Along = FVector3d::CrossProduct(Out, FVector3d(0.1, -0.9, 0.3));
	Along = Along.GetSafeNormal();

	FVector3d Position = Start[Planet].PositionMetres + Out * Radius;
	FVector3d Velocity = Start[Planet].VelocityMetresPerSecond + Along * Speed;

	// **Against the two-body prediction, which is a rotation.** With no
	// perturbation the ship would trace a circle about the planet at a constant
	// rate, so the prediction at time t is the starting offset turned by
	// 2 pi t / T -- computed from nothing the integrator touches.
	const FVector3d Axis = FVector3d::CrossProduct(Out, Along).GetSafeNormal();

	constexpr double StepSeconds = 1.0;
	const int32 Steps = static_cast<int32>(Period / StepSeconds);
	double WorstDrift = 0.0;
	double WorstRadius = 0.0;
	FString Table;

	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const double At = Step * StepSeconds;
		LedgerGravity::Step(System, Position, Velocity, At - StepSeconds, StepSeconds);

		TArray<FLedgerState> Now;
		LedgerEphemeris::StatesAt(System, At, Now);
		const FVector3d Offset = Position - Now[Planet].PositionMetres;

		const FQuat4d Turn(Axis, LedgerTwoPi * At / Period);
		const FVector3d Predicted = Turn.RotateVector(Out * Radius);

		const double Drift = (Offset - Predicted).Length();
		WorstDrift = FMath::Max(WorstDrift, Drift);
		WorstRadius = FMath::Max(WorstRadius, FMath::Abs(Offset.Length() - Radius));

		if (Step % (Steps / 4) == 0)
		{
			Table += FString::Printf(
				TEXT("  %5.2f of an orbit: %8.1f m from the prediction, radius off "
					 "by %7.1f m\n"),
				At / Period, Drift, Offset.Length() - Radius);
		}
	}

	AddInfo(FString::Printf(TEXT("one orbit, integrated under every body:\n%s"), *Table));
	AddInfo(FString::Printf(
		TEXT("worst departure from the two-body circle: %.1f m in %.0f km of arc, "
			 "which is %.2e of the orbit"),
		WorstDrift, LedgerTwoPi * Radius / 1000.0, WorstDrift / (LedgerTwoPi * Radius)));

	// **Close, because the perturbation is small here.** The star pulls on the
	// ship and on the planet almost equally at this distance, so what is left
	// is a tidal difference of about GM_star * 2r / d^3 -- parts per million of
	// the planet's own pull.
	TestTrue(*FString::Printf(
		TEXT("the coast follows the two-body circle (%.1f m over %.0f km)"),
		WorstDrift, LedgerTwoPi * Radius / 1000.0),
		WorstDrift < Radius * 1e-3);

	// **But not exactly -- and the reason has to be separated from the
	// integrator's own error before it means anything.**
	//
	// A non-zero drift on its own proves nothing: fourth-order Runge-Kutta over
	// eight thousand steps accumulates too, and a field that had quietly
	// dropped every body but the planet would still wander. So the same coast
	// is run again with a field that contains ONLY the planet. What is left
	// there is purely the integrator; the difference between the two runs is
	// the other bodies.
	//
	// **The ephemeris is left alone.** The first attempt zeroed the other
	// bodies' masses instead, which also stopped the planet's own orbit -- its
	// mean motion is computed from its parent's mass -- so the ship kept the
	// planet's original velocity and departed. It reported a drift of 1.4e11 m
	// and blamed the integrator. Only the FIELD may be cut down; where the
	// bodies are is not the thing being varied.
	auto PlanetOnly = [&System](const FVector3d& At, double When) -> FVector3d
	{
		TArray<FLedgerState> Where;
		LedgerEphemeris::StatesAt(System, When, Where);
		const FVector3d Offset = Where[Planet].PositionMetres - At;
		const double DistanceSquared = Offset.SquaredLength();
		return Offset * (LedgerEphemeris::GravitationalConstant * System.Bodies[Planet].MassKg
			/ (DistanceSquared * FMath::Sqrt(DistanceSquared)));
	};

	FVector3d LonePosition = Start[Planet].PositionMetres + Out * Radius;
	FVector3d LoneVelocity = Start[Planet].VelocityMetresPerSecond + Along * Speed;
	double LoneWorst = 0.0;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const double At = Step * StepSeconds;
		const double From = At - StepSeconds;

		// The same fourth-order scheme, so the comparison is of fields and not
		// of integrators.
		const FVector3d K1V = PlanetOnly(LonePosition, From);
		const FVector3d K1X = LoneVelocity;
		const FVector3d K2V = PlanetOnly(
			LonePosition + K1X * (StepSeconds * 0.5), From + StepSeconds * 0.5);
		const FVector3d K2X = LoneVelocity + K1V * (StepSeconds * 0.5);
		const FVector3d K3V = PlanetOnly(
			LonePosition + K2X * (StepSeconds * 0.5), From + StepSeconds * 0.5);
		const FVector3d K3X = LoneVelocity + K2V * (StepSeconds * 0.5);
		const FVector3d K4V = PlanetOnly(LonePosition + K3X * StepSeconds, At);
		const FVector3d K4X = LoneVelocity + K3V * StepSeconds;
		LonePosition += (K1X + K2X * 2.0 + K3X * 2.0 + K4X) * (StepSeconds / 6.0);
		LoneVelocity += (K1V + K2V * 2.0 + K3V * 2.0 + K4V) * (StepSeconds / 6.0);

		TArray<FLedgerState> Now;
		LedgerEphemeris::StatesAt(System, At, Now);
		const FVector3d Offset = LonePosition - Now[Planet].PositionMetres;
		const FQuat4d Turn(Axis, LedgerTwoPi * At / Period);
		LoneWorst = FMath::Max(LoneWorst, (Offset - Turn.RotateVector(Out * Radius)).Length());
	}

	// **And the answer came out the other way round, which is the better
	// result.** The planet-only field drifts far WORSE, not better.
	//
	// The frame is not inertial: the planet is falling around the star at about
	// 6.3e-3 m/s^2, so a ship that does not feel the star does not accelerate
	// with it and falls out of the frame at half a t squared. Over one orbit
	// that is about 190 km predicted from nothing but that acceleration, and
	// the measurement below is that number.
	//
	// So the star is not a small correction here. It is what keeps the ship
	// with the planet at all, and dropping it does not degrade the answer --
	// it destroys it.
	const double FrameAcceleration = LedgerEphemeris::GravitationalConstant
		* System.Bodies[0].MassKg
		/ (Start[Planet].PositionMetres - Start[0].PositionMetres).SquaredLength();
	const double Predicted = 0.5 * FrameAcceleration * Period * Period;
	AddInfo(FString::Printf(
		TEXT("with only the planet in the field the coast drifts %.0f km, against "
			 "%.0f km predicted from half a-t-squared on the planet's own %.2e m/s^2 "
			 "fall around the star"),
		LoneWorst / 1000.0, Predicted / 1000.0, FrameAcceleration));

	TestTrue(*FString::Printf(
		TEXT("dropping the other bodies wrecks the coast (%.0f km against %.1f m)"),
		LoneWorst / 1000.0, WorstDrift),
		LoneWorst > WorstDrift * 50.0);
	TestTrue(*FString::Printf(
		TEXT("and it wrecks it by the amount the frame's own fall predicts "
			 "(%.0f km against %.0f km)"), LoneWorst / 1000.0, Predicted / 1000.0),
		FMath::Abs(LoneWorst - Predicted) < Predicted * 0.3);

	// What is left in the full-field run is the tidal difference plus the
	// integrator. Halving the step separates them: if the answer barely moves,
	// the integration has converged and the residue is physics.
	FVector3d FinePosition = Start[Planet].PositionMetres + Out * Radius;
	FVector3d FineVelocity = Start[Planet].VelocityMetresPerSecond + Along * Speed;
	double FineWorst = 0.0;
	for (int32 Step = 1; Step <= Steps * 2; ++Step)
	{
		const double At = Step * StepSeconds * 0.5;
		LedgerGravity::Step(System, FinePosition, FineVelocity,
			At - StepSeconds * 0.5, StepSeconds * 0.5);

		TArray<FLedgerState> Now;
		LedgerEphemeris::StatesAt(System, At, Now);
		const FVector3d Offset = FinePosition - Now[Planet].PositionMetres;
		const FQuat4d Turn(Axis, LedgerTwoPi * At / Period);
		FineWorst = FMath::Max(FineWorst, (Offset - Turn.RotateVector(Out * Radius)).Length());
	}
	AddInfo(FString::Printf(
		TEXT("halving the step gives %.1f m against %.1f m, a change of %.2f%% -- so "
			 "the integration has converged and the residue is the tide, not the "
			 "arithmetic"),
		FineWorst, WorstDrift, FMath::Abs(FineWorst - WorstDrift) / WorstDrift * 100.0));
	TestTrue(*FString::Printf(
		TEXT("the integration has converged (%.2f%% on halving the step)"),
		FMath::Abs(FineWorst - WorstDrift) / WorstDrift * 100.0),
		FMath::Abs(FineWorst - WorstDrift) < WorstDrift * 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGravitySwitch,
	"Ledger.Gravity.TheDominantBodySwitchesOnceAndCleanly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGravitySwitch::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const int32 Moon = LedgerBodies::FirstChildOfKind(System, 1, ELedgerBodyKind::Moon);

	FString Table;
	for (int32 Index = 1; Index < System.Bodies.Num(); ++Index)
	{
		const double Reach = LedgerGravity::SphereOfInfluenceMetres(System, Index);
		Table += FString::Printf(TEXT("  body %d (%s): sphere of influence %.0f km\n"),
			Index, LexToString(System.Bodies[Index].Kind), Reach / 1000.0);
	}
	AddInfo(FString::Printf(TEXT("the spheres of this system:\n%s"), *Table));

	TArray<FLedgerState> States;
	LedgerEphemeris::StatesAt(System, 0.0, States);
	const FVector3d From = States[Planet].PositionMetres;
	const FVector3d To = States[Moon].PositionMetres;
	const double Span = (To - From).Length();

	// **Walked from one body to the other, and counted.** A boundary that
	// chattered -- flicking between two answers over a few kilometres -- would
	// make a trajectory planner switch reference frames dozens of times in a
	// row, and every switch is a chance to lose a digit.
	constexpr int32 Steps = 200000;
	int32 Changes = 0;
	double CrossedAt = -1.0;
	int32 Previous = LedgerGravity::DominantBody(System, From + (To - From) * 0.001, 0.0);
	const int32 Started = Previous;
	int32 Ended = Previous;

	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const double Fraction = 0.001 + (0.999 - 0.001) * Step / Steps;
		const FVector3d At = From + (To - From) * Fraction;
		const int32 Here = LedgerGravity::DominantBody(System, At, 0.0);
		if (Here != Previous)
		{
			++Changes;
			CrossedAt = (At - To).Length();
			Previous = Here;
		}
		Ended = Here;
	}

	const double MoonSphere = LedgerGravity::SphereOfInfluenceMetres(System, Moon);
	AddInfo(FString::Printf(
		TEXT("walking the %.0f km from planet to moon in %d steps: %d change(s), "
			 "from body %d to body %d"),
		Span / 1000.0, Steps, Changes, Started, Ended));
	AddInfo(FString::Printf(
		TEXT("the change happened %.0f km from the moon; its sphere of influence is "
			 "%.0f km"), CrossedAt / 1000.0, MoonSphere / 1000.0));

	TestEqual(TEXT("the dominant body changes exactly once"), Changes, 1);
	TestEqual(TEXT("it starts as the planet"), Started, Planet);
	TestEqual(TEXT("and ends as the moon"), Ended, Moon);

	// And it changes AT the boundary, not somewhere near it.
	TestTrue(*FString::Printf(
		TEXT("the switch is at the sphere of influence (%.0f km against %.0f km)"),
		CrossedAt / 1000.0, MoonSphere / 1000.0),
		FMath::Abs(CrossedAt - MoonSphere) < Span / Steps * 2.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGravityField,
	"Ledger.Gravity.TheSummedFieldIsTheOneEachBodyMakes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGravityField::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const FLedgerBody& Body = System.Bodies[Planet];

	TArray<FLedgerState> States;
	LedgerEphemeris::StatesAt(System, 0.0, States);

	// At the surface the planet is essentially all of it.
	const FVector3d Out = FVector3d(0.3, -0.8, 0.52).GetSafeNormal();
	const FVector3d Standing = States[Planet].PositionMetres + Out * Body.RadiusMetres;
	const FVector3d Field = LedgerGravity::FieldAt(System, Standing, 0.0);
	const double Alone = LedgerEphemeris::GravitationalConstant * Body.MassKg
		/ (Body.RadiusMetres * Body.RadiusMetres);

	AddInfo(FString::Printf(
		TEXT("at the surface: %.6f m/s^2 summed, %.6f from the planet alone, "
			 "%.3e of a difference"),
		Field.Length(), Alone, FMath::Abs(Field.Length() - Alone) / Alone));
	TestTrue(TEXT("the surface is the planet's own gravity to a part in a thousand"),
		FMath::Abs(Field.Length() - Alone) / Alone < 1e-3);

	// **Inside a body the field falls to zero rather than blowing up.** Nothing
	// should be in there, but a trajectory that ends up there should produce a
	// wrong answer rather than an infinity that poisons everything after it.
	FString Table;
	double Previous = TNumericLimits<double>::Max();
	bool bFalls = true;
	for (const double Fraction : { 1.0, 0.75, 0.5, 0.25, 0.05, 0.0 })
	{
		const FVector3d Inside =
			States[Planet].PositionMetres + Out * (Body.RadiusMetres * Fraction);
		const double Here = LedgerGravity::FieldAt(System, Inside, 0.0).Length();
		Table += FString::Printf(TEXT("  %.2f of the way out: %.6f m/s^2\n"),
			Fraction, Here);
		if (Here > Previous + 1e-9) { bFalls = false; }
		Previous = Here;
		TestTrue(TEXT("the field stays finite inside a body"), FMath::IsFinite(Here));
	}
	AddInfo(FString::Printf(TEXT("down through the planet:\n%s"), *Table));
	TestTrue(TEXT("gravity falls towards the centre rather than rising"), bFalls);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
