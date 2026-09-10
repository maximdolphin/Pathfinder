// A station on rails, and a ship that stays docked to it. T082.

#include "LedgerStation.h"
#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 StationIndex = 4;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStationOrbit,
	"Ledger.Station.ItsPositionHoldsOverAMonth",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStationOrbit::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	TestTrue(TEXT("the system has a station"), System.Bodies.IsValidIndex(StationIndex));

	const FLedgerBody& Station = System.Bodies[StationIndex];
	const double Period = LedgerEphemeris::PeriodSeconds(
		System.Bodies[Station.ParentIndex].MassKg, Station.Orbit.SemiMajorAxisMetres);
	const double Altitude = Station.Orbit.SemiMajorAxisMetres
		- System.Bodies[Station.ParentIndex].RadiusMetres;

	AddInfo(FString::Printf(
		TEXT("station at %.0f km altitude, %.1f minute orbit, inclination %.1f degrees"),
		Altitude / 1000.0, Period / 60.0,
		FMath::RadiansToDegrees(Station.Orbit.InclinationRadians)));

	// **A month, walked forward, against the same month asked directly.** The
	// ephemeris is a function of time, so this is close to a tautology and that
	// is exactly the point: an integrated station would fail it, and this is
	// what stops one quietly replacing this.
	constexpr double Month = 30.0 * 86400.0;
	const int32 Steps = 20000;
	double Worst = 0.0;
	double WorstAt = 0.0;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const double At = Month * Step / Steps;
		TArray<FLedgerState> Walked;
		LedgerEphemeris::StatesAt(System, At, Walked);

		// Asked a second way: relative to the parent, plus the parent's own
		// place. The chain is the thing being checked -- a station is two
		// orbits deep and the sum has to come out the same either way.
		const FLedgerState Relative = LedgerEphemeris::StateAt(
			Station, System.Bodies[Station.ParentIndex].MassKg, At);
		const FVector3d Direct =
			Walked[Station.ParentIndex].PositionMetres + Relative.PositionMetres;

		const double Apart = (Walked[StationIndex].PositionMetres - Direct).Length();
		if (Apart > Worst) { Worst = Apart; WorstAt = At; }
	}
	AddInfo(FString::Printf(
		TEXT("over 30 days in %d steps, the two routes to the station's position "
			 "differ by at most %.6f m, at %.2f days"),
		Steps, Worst, WorstAt / 86400.0));
	TestTrue(*FString::Printf(TEXT("the station is where its orbit says (%.6f m)"), Worst),
		Worst < 0.001);

	// And it goes round: a station that sat still would satisfy everything
	// above.
	TArray<FLedgerState> Start;
	TArray<FLedgerState> Quarter;
	LedgerEphemeris::StatesAt(System, 0.0, Start);
	LedgerEphemeris::StatesAt(System, Period * 0.25, Quarter);
	const double Moved = ((Quarter[StationIndex].PositionMetres
		- Quarter[Station.ParentIndex].PositionMetres)
		- (Start[StationIndex].PositionMetres
			- Start[Station.ParentIndex].PositionMetres)).Length();
	AddInfo(FString::Printf(
		TEXT("a quarter orbit moves it %.0f km, against an orbital radius of %.0f km"),
		Moved / 1000.0, Station.Orbit.SemiMajorAxisMetres / 1000.0));
	TestTrue(TEXT("a quarter orbit takes it a long way round"),
		Moved > Station.Orbit.SemiMajorAxisMetres);

	// A whole orbit brings it back.
	TArray<FLedgerState> Later;
	LedgerEphemeris::StatesAt(System, Period, Later);
	const double Returned = ((Later[StationIndex].PositionMetres
		- Later[Station.ParentIndex].PositionMetres)
		- (Start[StationIndex].PositionMetres
			- Start[Station.ParentIndex].PositionMetres)).Length();
	AddInfo(FString::Printf(TEXT("one orbit returns it to within %.6f m"), Returned));
	TestTrue(*FString::Printf(TEXT("one orbit closes (%.6f m)"), Returned),
		Returned < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStationDocked,
	"Ledger.Station.ADockedShipStaysDocked",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStationDocked::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const FLedgerBody& Station = System.Bodies[StationIndex];
	const double Period = LedgerEphemeris::PeriodSeconds(
		System.Bodies[Station.ParentIndex].MassKg, Station.Orbit.SemiMajorAxisMetres);

	// A ship on a port forty metres out along the station's nose, ten to one
	// side and six up. Nothing symmetric, so a frame that quietly swapped two
	// axes would show.
	FLedgerStationPoint Docked;
	Docked.StationIndex = StationIndex;
	Docked.Metres = FVector3d(40.0, -10.0, 6.0);
	const double Reach = Docked.Metres.Length();

	constexpr double Month = 30.0 * 86400.0;
	const int32 Steps = 20000;

	double WorstReach = 0.0;
	double WorstRoundTrip = 0.0;
	double WorstAt = 0.0;

	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const double At = Month * Step / Steps;

		const FVector3d InSystem = LedgerStation::ToSystem(System, Docked, At);

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, At, States);

		// **Still forty-two metres from the station, whatever the station is
		// doing.** A docking that drifted would show here first, and it is the
		// simplest statement of the acceptance there is.
		const double Distance =
			(InSystem - States[StationIndex].PositionMetres).Length();
		WorstReach = FMath::Max(WorstReach, FMath::Abs(Distance - Reach));

		// And the local coordinates come back, which is the stronger claim: not
		// merely the same distance, but the same PLACE on the hull.
		const FLedgerStationPoint Back =
			LedgerStation::ToStation(System, InSystem, StationIndex, At);
		const double Apart = (Back.Metres - Docked.Metres).Length();
		if (Apart > WorstRoundTrip)
		{
			WorstRoundTrip = Apart;
			WorstAt = At;
		}
	}

	AddInfo(FString::Printf(
		TEXT("over 30 days in %d steps: the dock stays %.3f m out to within %.3e m, "
			 "and its local coordinates return to within %.3e m (worst at %.2f days)"),
		Steps, Reach, WorstReach, WorstRoundTrip, WorstAt / 86400.0));

	// **A tenth of a millimetre, and the reason is arithmetic rather than
	// slack.** The station is 1.5e11 m from the primary and the dock is 42 m
	// from the station, so recovering the second from the first throws away
	// eleven digits before anything is compared: a double's last bit at 1.5e11
	// is about 20 microns, and both numbers above are a couple of those.
	//
	// The first bound here was a micron, which is finer than the arithmetic can
	// resolve at that distance -- a bound no correct implementation could ever
	// have met. This one is stated against the floor it is actually near.
	TestTrue(*FString::Printf(
		TEXT("the ship stays the same distance out (%.3e m, against a %.1e m "
			 "floating-point floor at this distance)"),
		WorstReach, 1.5e11 * 2.2e-16), WorstReach < 1e-4);
	TestTrue(*FString::Printf(TEXT("and on the same spot on the hull (%.3e m)"),
		WorstRoundTrip), WorstRoundTrip < 1e-4);

	// It really is being carried, not left behind: over a quarter orbit the
	// docked point moves about as far as the station does.
	const FVector3d At0 = LedgerStation::ToSystem(System, Docked, 0.0);
	const FVector3d AtQuarter = LedgerStation::ToSystem(System, Docked, Period * 0.25);
	AddInfo(FString::Printf(TEXT("across a quarter orbit the docked point travels %.0f km"),
		(AtQuarter - At0).Length() / 1000.0));
	TestTrue(TEXT("the docked point travels with the station"),
		(AtQuarter - At0).Length() > Station.Orbit.SemiMajorAxisMetres);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStationFrame,
	"Ledger.Station.UpIsAwayFromThePlanetAndForwardIsAlongTheTrack",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStationFrame::RunTest(const FString&)
{
	// The frame has to mean what it says, or "up" on a station is whichever way
	// the maths happened to point and every handrail is somewhere different.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const FLedgerBody& Station = System.Bodies[StationIndex];
	const double Period = LedgerEphemeris::PeriodSeconds(
		System.Bodies[Station.ParentIndex].MassKg, Station.Orbit.SemiMajorAxisMetres);

	double WorstUp = 0.0;
	double WorstForward = 0.0;
	double WorstHanded = 0.0;

	for (int32 Step = 0; Step < 500; ++Step)
	{
		const double At = Period * Step / 500.0;

		// **A hundred-kilometre lever arm, not a one-metre one.** These axes
		// are recovered by differencing two system-frame positions of order
		// 1.5e11 m, which leaves about 20 microns of noise whatever the answer
		// is. On a 1 m arm that is 2e-5 radians -- a thousandth of a degree of
		// pure arithmetic, which is what the first run reported and blamed on
		// the frame. On a 100 km arm the same noise is 2e-10 radians.
		//
		// The station is not 100 km across. The arm is a measuring rod, not a
		// handrail: the frame is linear, so a direction read at 100 km is the
		// direction at 1 m with the noise divided by a hundred thousand.
		constexpr double Arm = 1.0e5;

		FLedgerStationPoint Up;
		Up.StationIndex = StationIndex;
		Up.Metres = FVector3d(0.0, 0.0, Arm);

		FLedgerStationPoint Forward;
		Forward.StationIndex = StationIndex;
		Forward.Metres = FVector3d(Arm, 0.0, 0.0);

		FLedgerStationPoint Left;
		Left.StationIndex = StationIndex;
		Left.Metres = FVector3d(0.0, Arm, 0.0);

		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, At, States);
		const FVector3d Centre = States[StationIndex].PositionMetres;

		const FVector3d UpAxis = LedgerStation::ToSystem(System, Up, At) - Centre;
		const FVector3d ForwardAxis = LedgerStation::ToSystem(System, Forward, At) - Centre;
		const FVector3d LeftAxis = LedgerStation::ToSystem(System, Left, At) - Centre;

		// Up is away from the parent.
		const FVector3d Outward =
			(Centre - States[Station.ParentIndex].PositionMetres).GetSafeNormal();
		WorstUp = FMath::Max(WorstUp, FMath::Acos(FMath::Clamp(
			FVector3d::DotProduct(UpAxis.GetSafeNormal(), Outward), -1.0, 1.0)));

		// Forward is along the track: perpendicular to up, and on the same side
		// as the velocity.
		const FLedgerState Relative = LedgerEphemeris::StateAt(
			Station, System.Bodies[Station.ParentIndex].MassKg, At);
		const FVector3d Track = Relative.VelocityMetresPerSecond.GetSafeNormal();
		WorstForward = FMath::Max(WorstForward, FMath::Acos(FMath::Clamp(
			FVector3d::DotProduct(ForwardAxis.GetSafeNormal(), Track), -1.0, 1.0)));

		// Right-handed: forward cross left is up.
		WorstHanded = FMath::Max(WorstHanded,
			(FVector3d::CrossProduct(ForwardAxis.GetSafeNormal(),
				LeftAxis.GetSafeNormal()) - UpAxis.GetSafeNormal()).Length());
	}

	AddInfo(FString::Printf(
		TEXT("around one orbit: up is %.3e degrees off outward, forward is %.4f "
			 "degrees off the velocity, and the basis is right-handed to %.3e"),
		FMath::RadiansToDegrees(WorstUp), FMath::RadiansToDegrees(WorstForward),
		WorstHanded));

	// A ten-thousandth of a degree. The station's outward direction is recovered
	// here by differencing two system positions of order 1.5e11 m to get one of
	// 6.9e6, which leaves a few parts in 1e11 of angular noise however right the
	// frame is. Tighter than this is measuring the subtraction.
	TestTrue(*FString::Printf(TEXT("up points away from the planet (%.3e degrees)"),
		FMath::RadiansToDegrees(WorstUp)),
		FMath::RadiansToDegrees(WorstUp) < 1e-4);
	TestTrue(TEXT("forward is right-handed with up and left"), WorstHanded < 1e-9);

	// **Forward is not exactly the velocity, and should not be.** On an
	// eccentric orbit the velocity has a radial part -- the station climbing or
	// falling -- and that belongs to up. The angle between forward and the
	// track is the flight path angle, and it is zero only on a circle.
	AddInfo(FString::Printf(
		TEXT("the flight path angle reaches %.4f degrees, which is the orbit's "
			 "eccentricity of %.5f showing up as the station climbing and falling"),
		FMath::RadiansToDegrees(WorstForward), Station.Orbit.Eccentricity));
	TestTrue(TEXT("forward tracks the velocity closely on a near-circular orbit"),
		FMath::RadiansToDegrees(WorstForward) < 1.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
