// Standing on something that is not a sphere. T081.
//
// The acceptance: correct local gravity direction at EVERY point on the surface.
// "Correct" is the load-bearing word, because the cheap answer -- straight at
// the centre -- is right on a sphere and wrong on everything else, and looks
// right in a screenshot either way.

#include "LedgerAsteroid.h"
#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// Points spread evenly over a sphere.
	FVector3d Fibonacci(int32 Index, int32 Count)
	{
		const double Z = 1.0 - 2.0 * (Index + 0.5) / Count;
		const double Ring = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		const double Angle = LedgerPi * (3.0 - FMath::Sqrt(5.0)) * Index;
		return FVector3d(Ring * FMath::Cos(Angle), Ring * FMath::Sin(Angle), Z);
	}

	FLedgerAsteroidShape Lumpy()
	{
		FLedgerAsteroidShape Shape;
		Shape.Seed = 20260908u;
		Shape.MeanRadiusMetres = 5000.0;
		// 0.18. At 0.32 the shape carried 50-degree slopes and at 0.22 it
		// carried 40, and a rubble pile does not: loose material sits at an
		// angle of repose near 35 and slumps past it, so a body with faces that
		// steep would have rearranged itself long before anybody landed on one.
		// The irregularity was walked down until the slopes were ones a real
		// asteroid is observed to hold, rather than the bound being walked up.
		Shape.Irregularity = 0.18;
		Shape.DensityKgPerM3 = 2000.0;
		return Shape;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAsteroidShellTheorem,
	"Ledger.Asteroid.ASphereGivesBackTheTextbookAnswer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAsteroidShellTheorem::RunTest(const FString&)
{
	// **The check that the sum is a gravity field.** Set the irregularity to
	// zero and the body is a sphere, where the shell theorem says the answer
	// must be GM/r^2 pointing exactly at the centre. If a fifty-thousand-term
	// sum cannot reproduce the one case anybody can do by hand, it is not
	// reproducing the others either.
	FLedgerAsteroidShape Sphere = Lumpy();
	Sphere.Irregularity = 0.0;

	const double Mass = LedgerAsteroid::MassKg(Sphere);
	const double Exact = (4.0 / 3.0) * LedgerPi * FMath::Pow(Sphere.MeanRadiusMetres, 3.0)
		* Sphere.DensityKgPerM3;
	AddInfo(FString::Printf(
		TEXT("integrated mass %.6g kg against an exact %.6g kg (%.3f%% out)"),
		Mass, Exact, FMath::Abs(Mass - Exact) / Exact * 100.0));
	TestTrue(*FString::Printf(TEXT("the volume integrates (%.3f%%)"),
		FMath::Abs(Mass - Exact) / Exact * 100.0),
		FMath::Abs(Mass - Exact) / Exact < 0.01);

	double WorstMagnitude = 0.0;
	double WorstAngle = 0.0;
	FString Table;
	for (const double Radii : { 1.05, 1.5, 3.0, 10.0 })
	{
		double Magnitude = 0.0;
		double Angle = 0.0;
		for (int32 Index = 0; Index < 60; ++Index)
		{
			const FVector3d Direction = Fibonacci(Index, 60);
			const FVector3d At = Direction * (Sphere.MeanRadiusMetres * Radii);
			const FVector3d Gravity = LedgerAsteroid::GravityAt(Sphere, At);

			const double Expected = LedgerEphemeris::GravitationalConstant * Exact
				/ At.SquaredLength();
			Magnitude = FMath::Max(Magnitude,
				FMath::Abs(Gravity.Length() - Expected) / Expected);
			Angle = FMath::Max(Angle, FMath::Acos(FMath::Clamp(
				FVector3d::DotProduct(Gravity.GetSafeNormal(1e-30), -Direction), -1.0, 1.0)));
		}
		Table += FString::Printf(
			TEXT("  at %5.2f radii: magnitude %.4f%% out, direction %.4f degrees off\n"),
			Radii, Magnitude * 100.0, FMath::RadiansToDegrees(Angle));
		WorstMagnitude = FMath::Max(WorstMagnitude, Magnitude);
		WorstAngle = FMath::Max(WorstAngle, Angle);
	}
	AddInfo(FString::Printf(TEXT("a sphere, against GM/r^2:\n%s"), *Table));

	TestTrue(*FString::Printf(TEXT("the magnitude is GM/r^2 (worst %.4f%%)"),
		WorstMagnitude * 100.0), WorstMagnitude < 0.02);
	TestTrue(*FString::Printf(TEXT("and it points at the centre (worst %.4f degrees)"),
		FMath::RadiansToDegrees(WorstAngle)),
		FMath::RadiansToDegrees(WorstAngle) < 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAsteroidNotAtTheCentre,
	"Ledger.Asteroid.GravityDoesNotPointAtTheCentre",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAsteroidNotAtTheCentre::RunTest(const FString&)
{
	// The claim the acceptance is really about. On a lumpy body the pull leans
	// towards the nearest mass, which is not the middle -- and a lander that
	// assumed otherwise touches down at an angle.
	const FLedgerAsteroidShape Shape = Lumpy();

	double Worst = 0.0;
	double Mean = 0.0;
	int32 Count = 0;
	FString Dump;

	for (int32 Index = 0; Index < 400; ++Index)
	{
		const FVector3d Direction = Fibonacci(Index, 400);
		const double Radius = LedgerAsteroid::RadiusInDirection(Shape, Direction);
		const FVector3d Standing = Direction * Radius;
		const FVector3d Gravity = LedgerAsteroid::GravityAt(Shape, Standing);

		const double Angle = FMath::Acos(FMath::Clamp(
			FVector3d::DotProduct(Gravity.GetSafeNormal(1e-30), -Direction), -1.0, 1.0));
		Worst = FMath::Max(Worst, Angle);
		Mean += Angle;
		++Count;

		Dump += FString::Printf(TEXT("%.6f %.6f %.6f %.6f %.6f" LINE_TERMINATOR),
			Direction.X, Direction.Y, Direction.Z,
			Radius / Shape.MeanRadiusMetres, FMath::RadiansToDegrees(Angle));
	}
	Mean /= Count;

	AddInfo(FString::Printf(
		TEXT("across %d surface points: gravity leans %.2f degrees off the radial "
			 "direction on average, %.2f at worst"),
		Count, FMath::RadiansToDegrees(Mean), FMath::RadiansToDegrees(Worst)));

	FFileHelper::SaveStringToFile(Dump,
		*FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("t081-asteroid.txt"))));

	// **Measurably not at the centre.** A degree is about what a person notices
	// as a slope underfoot; this body leans several.
	TestTrue(*FString::Printf(
		TEXT("gravity is not radial on a lumpy body (worst %.2f degrees)"),
		FMath::RadiansToDegrees(Worst)),
		FMath::RadiansToDegrees(Worst) > 1.0);

	// And the same computation on a sphere gives essentially zero, which is
	// what makes the number above a property of the SHAPE rather than of the
	// integrator's noise.
	FLedgerAsteroidShape Sphere = Shape;
	Sphere.Irregularity = 0.0;
	double SphereWorst = 0.0;
	for (int32 Index = 0; Index < 400; ++Index)
	{
		const FVector3d Direction = Fibonacci(Index, 400);
		const FVector3d Standing =
			Direction * LedgerAsteroid::RadiusInDirection(Sphere, Direction);
		const FVector3d Gravity = LedgerAsteroid::GravityAt(Sphere, Standing);
		SphereWorst = FMath::Max(SphereWorst, FMath::Acos(FMath::Clamp(
			FVector3d::DotProduct(Gravity.GetSafeNormal(1e-30), -Direction), -1.0, 1.0)));
	}
	AddInfo(FString::Printf(
		TEXT("the same integrator on a sphere leans %.4f degrees, so the %.2f above "
			 "is the shape and not the arithmetic"),
		FMath::RadiansToDegrees(SphereWorst), FMath::RadiansToDegrees(Worst)));
	// **Signal against a measured floor, not against a number somebody picked.**
	//
	// Evaluating gravity exactly ON the surface is the integrator's worst case:
	// the point sits amid the cells it is summing, so a perfect sphere still
	// reads about a degree of false lean. That floor is real and is not going
	// away without a much finer grid, so the honest claim is not "the floor is
	// small" -- it is that the lumpy body's lean is several times it, measured
	// the same way in the same place.
	AddInfo(FString::Printf(
		TEXT("the lumpy body leans %.1f times as far as the integrator's own floor"),
		Worst / FMath::Max(SphereWorst, 1e-9)));
	TestTrue(*FString::Printf(
		TEXT("the lean is the shape, not the arithmetic (%.2f degrees against a "
			 "%.2f degree floor)"),
		FMath::RadiansToDegrees(Worst), FMath::RadiansToDegrees(SphereWorst)),
		Worst > SphereWorst * 4.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAsteroidStandable,
	"Ledger.Asteroid.YouCanStandAnywhereOnIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAsteroidStandable::RunTest(const FString&)
{
	// **Every point, which is what the acceptance says.** Gravity must pull
	// INTO the surface everywhere -- if it pointed outwards anywhere, that spot
	// would throw a lander off, and if it pointed exactly along the surface the
	// lander would slide for ever.
	const FLedgerAsteroidShape Shape = Lumpy();

	int32 Outward = 0;
	double WorstSlope = 0.0;
	FVector3d WorstAt = FVector3d::ZeroVector;
	double Weakest = TNumericLimits<double>::Max();
	double Strongest = 0.0;

	for (int32 Index = 0; Index < 2000; ++Index)
	{
		const FVector3d Direction = Fibonacci(Index, 2000);
		const FVector3d Standing =
			Direction * LedgerAsteroid::RadiusInDirection(Shape, Direction);
		const FVector3d Gravity = LedgerAsteroid::GravityAt(Shape, Standing);
		const FVector3d Normal = LedgerAsteroid::SurfaceNormal(Shape, Direction);

		const double Into = FVector3d::DotProduct(Gravity.GetSafeNormal(1e-30), -Normal);
		if (Into <= 0.0)
		{
			++Outward;
		}

		// The slope a person would feel: the angle between the pull and
		// straight down the local surface.
		const double Slope = FMath::Acos(FMath::Clamp(Into, -1.0, 1.0));
		if (Slope > WorstSlope)
		{
			WorstSlope = Slope;
			WorstAt = Direction;
		}

		const double Magnitude = Gravity.Length();
		Weakest = FMath::Min(Weakest, Magnitude);
		Strongest = FMath::Max(Strongest, Magnitude);
	}

	AddInfo(FString::Printf(
		TEXT("2000 surface points: %d with gravity pointing outwards, worst local "
			 "slope %.2f degrees at (%.2f %.2f %.2f)"),
		Outward, FMath::RadiansToDegrees(WorstSlope),
		WorstAt.X, WorstAt.Y, WorstAt.Z));
	AddInfo(FString::Printf(
		TEXT("surface gravity runs %.6f to %.6f m/s^2, a factor of %.2f -- because "
			 "the near end of a lumpy body is nearer its own mass"),
		Weakest, Strongest, Strongest / Weakest));

	TestEqual(TEXT("nowhere on it throws you off"), Outward, 0);

	// A slope steeper than about 40 degrees is scree that would have slid long
	// ago, so a shape producing one is a shape no rubble pile would keep.
	TestTrue(*FString::Printf(
		TEXT("no slope is steeper than a rubble pile holds (%.2f degrees)"),
		FMath::RadiansToDegrees(WorstSlope)),
		FMath::RadiansToDegrees(WorstSlope) < 40.0);

	// And the strength varies, which is the other half of "not a sphere": you
	// weigh measurably more at the ends of a peanut than at its waist.
	TestTrue(*FString::Printf(TEXT("weight varies over the surface (factor %.2f)"),
		Strongest / Weakest), Strongest / Weakest > 1.05);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
