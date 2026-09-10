// A point on a rotating planet, there and back. T071.
//
// The acceptance is millimetre agreement, and the number that makes it hard is
// the scale: a point on this planet's surface is 1.5e11 metres from the primary
// once the orbit is included, and a double has about 1e-16 of relative
// precision. A millimetre out of 1.5e11 is 7e-15 relative -- perhaps a hundred
// times the floating-point floor, which is enough room for the arithmetic to be
// right and nowhere near enough for it to be sloppy.

#include "LedgerFrames.h"
#include "LedgerEphemeris.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFramesRoundTrip,
	"Ledger.Frames.SurfacePointSurvivesTheRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFramesRoundTrip::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const double Radius = System.Bodies[Planet].RadiusMetres;

	// Places on the planet, including both poles -- where north is undefined
	// and a frame built from a latitude would have a singularity.
	const FVector3d Anchors[] = {
		FVector3d(0.0, 0.0, 1.0),
		FVector3d(0.0, 0.0, -1.0),
		FVector3d(1.0, 0.0, 0.0),
		FVector3d(0.0, 1.0, 0.0),
		FVector3d(0.3, -0.7, 0.65),
		FVector3d(-0.5, -0.5, -0.707),
	};

	// Times spread across a day and a year, because the whole point is that
	// the transform depends on when it is asked.
	const double Times[] = { 0.0, 3600.0, 43200.0, 86400.0, 1.0e7, 3.15e7 };

	double Worst = 0.0;
	for (const FVector3d& RawAnchor : Anchors)
	{
		const FVector3d Anchor = RawAnchor.GetSafeNormal();
		for (const double At : Times)
		{
			// Someone standing 1.8 m up and a few metres along the ground.
			FLedgerSurfacePoint Standing;
			Standing.BodyIndex = Planet;
			Standing.AnchorDirection = Anchor;
			Standing.Metres = FVector3d(12.5, -7.25, 1.8);

			const FLedgerBodyPoint OnBody = LedgerFrames::ToBody(Standing);

			// The anchor is a direction; the ground is a radius out along it.
			FLedgerBodyPoint Ground = OnBody;
			Ground.Metres += Anchor * Radius;

			const FLedgerSystemPoint InSystem =
				LedgerFrames::ToSystem(System, Ground, At);
			const FLedgerBodyPoint Back =
				LedgerFrames::ToBody(System, InSystem, Planet, At);

			const double Apart = (Back.Metres - Ground.Metres).Length();
			Worst = FMath::Max(Worst, Apart);
		}
	}

	AddInfo(FString::Printf(
		TEXT("worst round trip %.6f mm, at a system-frame distance of order 1.5e11 m"),
		Worst * 1000.0));
	TestTrue(*FString::Printf(
		TEXT("a surface point round-trips to the millimetre (worst %.6f mm)"), Worst * 1000.0),
		Worst < 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFramesRotate,
	"Ledger.Frames.TheBodyFrameActuallyRotates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFramesRotate::RunTest(const FString&)
{
	// A round trip passes trivially if both directions are the identity, so
	// this is the check that the transform is doing anything at all: a fixed
	// body-frame point must MOVE in the system frame as the planet turns, and
	// must come back after exactly one rotation.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const FLedgerBody& Body = System.Bodies[Planet];

	FLedgerBodyPoint Mountain;
	Mountain.BodyIndex = Planet;
	Mountain.Metres = FVector3d(Body.RadiusMetres, 0.0, 0.0);

	const FVector3d AtZero = LedgerFrames::ToSystem(System, Mountain, 0.0).Metres;
	const FVector3d AtQuarter = LedgerFrames::ToSystem(
		System, Mountain, Body.RotationPeriodSeconds * 0.25).Metres;

	// A quarter turn moves a point on the equator by about sqrt(2) radii. The
	// planet has also moved along its orbit, so this is a loose bound -- it is
	// here to catch "nothing happened", not to measure anything.
	const double Moved = (AtQuarter - AtZero).Length();
	TestTrue(TEXT("a quarter rotation moves a surface point a long way"),
		Moved > Body.RadiusMetres);

	// And the rotation itself is periodic, asked of the orientation rather than
	// by differencing two system positions -- subtracting two vectors of order
	// 1.5e11 m to recover one of 6.4e6 throws away five digits before anything
	// is compared, and the claim here is about the rotation.
	//
	// **This is the assertion that found Unreal's PI is a float.** It read
	// 1105 mm, and 1.1 m over a 6.4e6 m radius is 1.7e-7 radians -- single
	// precision, in code that is double precision throughout. I first assumed
	// the test was measuring its own cancellation and rewrote it into the form
	// below to remove the large magnitudes. It still read 1105 mm, which is
	// what ruled the test out and left the code. `2.0 * PI` printed as
	// 6.2831854820251465 against a true 6.283185307179586: `PI` is a float
	// constant, so the product was a widened float. See LedgerTwoPi.
	const double Day = Body.RotationPeriodSeconds;
	const FVector3d ZeroRelative =
		LedgerFrames::BodyOrientation(Body, 0.0).RotateVector(Mountain.Metres);
	const FVector3d DayRelative =
		LedgerFrames::BodyOrientation(Body, Day).RotateVector(Mountain.Metres);

	const double Apart = (DayRelative - ZeroRelative).Length();
	AddInfo(FString::Printf(TEXT("after one rotation, %.6f mm from where it started"),
		Apart * 1000.0));
	TestTrue(TEXT("one rotation brings a point back to where it was"),
		Apart < 0.001);

	// And the constant itself, so that a regression to the float one fails here
	// with its cause written on it rather than as a millimetre count somewhere
	// downstream.
	TestTrue(*FString::Printf(
		TEXT("two pi is the double one (%.17g, want 6.283185307179586)"), LedgerTwoPi),
		LedgerTwoPi != static_cast<double>(2.0f * PI));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFramesSurfaceBasis,
	"Ledger.Frames.EastNorthUpIsRightHandedEverywhere",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFramesSurfaceBasis::RunTest(const FString&)
{
	// Including at both poles, where north is undefined and the fallback is
	// what keeps the frame a frame.
	for (int32 Step = 0; Step < 200; ++Step)
	{
		const double Z = -1.0 + 2.0 * Step / 199.0;
		const double R = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		const double Angle = Step * 0.7;
		const FVector3d Anchor(R * FMath::Cos(Angle), R * FMath::Sin(Angle), Z);

		FLedgerBodyPoint East;
		East.Metres = FVector3d::ZeroVector;

		FLedgerSurfacePoint Probe;
		Probe.AnchorDirection = Anchor;

		// Unit steps along each surface axis, taken into the body frame.
		Probe.Metres = FVector3d(1.0, 0.0, 0.0);
		const FVector3d E = LedgerFrames::ToBody(Probe).Metres;
		Probe.Metres = FVector3d(0.0, 1.0, 0.0);
		const FVector3d N = LedgerFrames::ToBody(Probe).Metres;
		Probe.Metres = FVector3d(0.0, 0.0, 1.0);
		const FVector3d U = LedgerFrames::ToBody(Probe).Metres;

		const FString Where = FString::Printf(TEXT("z=%.3f"), Z);
		TestTrue(*(Where + TEXT(": east is a unit vector")), FMath::IsNearlyEqual(E.Length(), 1.0, 1e-9));
		TestTrue(*(Where + TEXT(": north is a unit vector")), FMath::IsNearlyEqual(N.Length(), 1.0, 1e-9));
		TestTrue(*(Where + TEXT(": up is a unit vector")), FMath::IsNearlyEqual(U.Length(), 1.0, 1e-9));

		TestTrue(*(Where + TEXT(": the axes are perpendicular")),
			FMath::Abs(FVector3d::DotProduct(E, N)) < 1e-9
			&& FMath::Abs(FVector3d::DotProduct(N, U)) < 1e-9
			&& FMath::Abs(FVector3d::DotProduct(U, E)) < 1e-9);

		// Right-handed: east cross north is up. Getting this backwards is how
		// a compass ends up mirrored, which is the sort of thing nobody
		// notices until a mission says "go north".
		TestTrue(*(Where + TEXT(": east x north = up")),
			(FVector3d::CrossProduct(E, N) - U).Length() < 1e-9);

		// And up really is the anchor.
		TestTrue(*(Where + TEXT(": up is the anchor direction")),
			(U - Anchor.GetSafeNormal()).Length() < 1e-9);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
