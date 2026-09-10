// The star as a physical light. T076.
//
// The acceptance is inverse square, so the test has to be able to tell inverse
// square from a curve that merely goes down. It gets that two ways: by checking
// the product of illuminance and distance squared is the same everywhere, and
// by computing the illuminance a second time through the star's APPARENT SIZE,
// which is the quantity the renderer draws and which contains no distance at
// all.

#include "LedgerSky.h"
#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStarInverseSquare,
	"Ledger.Star.IlluminanceFollowsTheInverseSquare",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStarInverseSquare::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const FLedgerBody& Star = System.Bodies[0];

	const double Luminosity = LedgerSky::StarLuminosityWatts(Star);
	const double Temperature = LedgerSky::StarTemperatureKelvin(Star);
	AddInfo(FString::Printf(
		TEXT("star: %.4g kg (%.3f solar), %.4g W (%.3f solar), %.0f K, radius %.4g m"),
		Star.MassKg, Star.MassKg / 1.98892e30,
		Luminosity, Luminosity / 3.828e26, Temperature, Star.RadiusMetres));

	TestTrue(TEXT("the star puts out something"), Luminosity > 0.0);
	TestTrue(*FString::Printf(TEXT("its temperature is stellar (%.0f K)"), Temperature),
		Temperature > 1500.0 && Temperature < 60000.0);

	TArray<FLedgerState> States;
	LedgerEphemeris::StatesAt(System, 0.0, States);

	// **Illuminance times distance squared is a constant, or it is not an
	// inverse square.** Measured across four decades of distance, from inside
	// the orbit out to well past it, so the claim is about the law and not
	// about one point on it.
	double First = -1.0;
	double WorstDrift = 0.0;
	FString Table;
	for (const double Metres : { 1.0e10, 3.0e10, 1.0e11, 1.4e11, 5.0e11, 4.0e12, 1.0e13 })
	{
		const FVector3d Where = States[0].PositionMetres + FVector3d(Metres, 0.0, 0.0);
		const double Lux = LedgerSky::IlluminanceLux(System, Where, 0.0);
		const double Product = Lux * Metres * Metres;
		if (First < 0.0)
		{
			First = Product;
		}
		const double Drift = FMath::Abs(Product - First) / First;
		WorstDrift = FMath::Max(WorstDrift, Drift);
		Table += FString::Printf(TEXT("  %8.3g m -> %12.4g lux, E*d^2 = %.6g\n"),
			Metres, Lux, Product);
	}
	AddInfo(FString::Printf(TEXT("illuminance against distance:\n%s"), *Table));
	AddInfo(FString::Printf(TEXT("worst drift in E*d^2: %.3e"), WorstDrift));
	TestTrue(*FString::Printf(TEXT("E*d^2 is constant (worst drift %.3e)"), WorstDrift),
		WorstDrift < 1e-12);

	// And the doubling, stated plainly, because "constant product" is a claim
	// somebody has to translate and "a quarter" is not.
	const FVector3d Near = States[0].PositionMetres + FVector3d(1.0e11, 0.0, 0.0);
	const FVector3d Far = States[0].PositionMetres + FVector3d(2.0e11, 0.0, 0.0);
	const double NearLux = LedgerSky::IlluminanceLux(System, Near, 0.0);
	const double FarLux = LedgerSky::IlluminanceLux(System, Far, 0.0);
	AddInfo(FString::Printf(TEXT("twice as far: %.4g lux against %.4g lux, ratio %.6f"),
		FarLux, NearLux, FarLux / NearLux));
	TestEqual(TEXT("twice as far is a quarter as bright"), FarLux / NearLux, 0.25, 1e-12);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStarDiscAgrees,
	"Ledger.Star.TheDiscAndTheDistanceAgree",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStarDiscAgrees::RunTest(const FString&)
{
	// Two roads to the same lux. One divides the luminosity by the area of a
	// sphere; the other multiplies the surface flux by the solid angle the disc
	// subtends and never mentions distance. They agree only if the star being
	// drawn and the star doing the lighting are the same object.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const FLedgerBody& Star = System.Bodies[0];
	const double Temperature = LedgerSky::StarTemperatureKelvin(Star);

	TArray<FLedgerState> States;
	LedgerEphemeris::StatesAt(System, 0.0, States);

	double Worst = 0.0;
	FString Table;
	for (const double Metres : { 2.0e9, 1.0e10, 1.4e11, 9.0e11, 6.0e12 })
	{
		const double ByDistance = LedgerSky::IlluminanceLux(
			System, States[0].PositionMetres + FVector3d(Metres, 0.0, 0.0), 0.0);

		// The angular radius from the same geometry T074 draws with.
		const double Angular = FMath::Asin(
			FMath::Clamp(Star.RadiusMetres / Metres, 0.0, 1.0));
		const double ByDisc = LedgerSky::IlluminanceFromDisc(Temperature, Angular);

		const double Drift = FMath::Abs(ByDisc - ByDistance) / ByDistance;
		Worst = FMath::Max(Worst, Drift);
		Table += FString::Printf(
			TEXT("  %8.3g m: %.6g lux by distance, %.6g by disc (%.3f deg across)\n"),
			Metres, ByDistance, ByDisc,
			FMath::RadiansToDegrees(Angular * 2.0));
	}
	AddInfo(FString::Printf(TEXT("the two roads:\n%s"), *Table));
	AddInfo(FString::Printf(TEXT("worst relative difference %.3e"), Worst));

	// Not exact: one uses R/d and the other sin(asin(R/d)), which are the same
	// number, but the disc road squares a sine where the distance road squares
	// a ratio. At a star's angular size those agree to the last few bits.
	TestTrue(*FString::Printf(
		TEXT("the disc and the distance give the same lux (worst %.3e)"), Worst),
		Worst < 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStarMassDecidesIt,
	"Ledger.Star.ABiggerStarIsBrighterAndBluer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStarMassDecidesIt::RunTest(const FString&)
{
	// Nothing about the star is stored beyond its mass and radius, so this is
	// the check that those two actually decide the light. A version that
	// returned a constant would pass every test above.
	FString Table;
	double PreviousLuminosity = 0.0;
	double PreviousTemperature = 0.0;

	for (const double SolarMasses : { 0.2, 0.5, 1.0, 2.0, 5.0 })
	{
		FLedgerBody Star;
		Star.Kind = ELedgerBodyKind::Star;
		Star.MassKg = 1.98892e30 * SolarMasses;
		// Radius roughly follows mass on the main sequence, which is what makes
		// a heavier star hotter rather than merely larger.
		Star.RadiusMetres = 6.957e8 * FMath::Pow(SolarMasses, 0.8);

		const double Luminosity = LedgerSky::StarLuminosityWatts(Star);
		const double Temperature = LedgerSky::StarTemperatureKelvin(Star);
		Table += FString::Printf(TEXT("  %.1f solar masses: %.4g W (%.4g solar), %.0f K\n"),
			SolarMasses, Luminosity, Luminosity / 3.828e26, Temperature);

		TestTrue(*FString::Printf(
			TEXT("%.1f solar masses is brighter than the one below"), SolarMasses),
			Luminosity > PreviousLuminosity);
		TestTrue(*FString::Printf(
			TEXT("%.1f solar masses is hotter than the one below"), SolarMasses),
			Temperature > PreviousTemperature);
		PreviousLuminosity = Luminosity;
		PreviousTemperature = Temperature;
	}
	AddInfo(FString::Printf(TEXT("across the main sequence:\n%s"), *Table));

	// A sun-like star should come out sun-like, which is the one place this
	// model can be checked against a number somebody else measured.
	FLedgerBody Sun;
	Sun.Kind = ELedgerBodyKind::Star;
	Sun.MassKg = 1.98892e30;
	Sun.RadiusMetres = 6.957e8;
	const double SunTemperature = LedgerSky::StarTemperatureKelvin(Sun);
	AddInfo(FString::Printf(
		TEXT("one solar mass and one solar radius gives %.0f K; the Sun is 5772 K"),
		SunTemperature));
	TestEqual(TEXT("the Sun comes out at the Sun's temperature"),
		SunTemperature, 5772.0, 20.0);

	// And its light at one astronomical unit is the number on the tin.
	const double AtOneAu = LedgerSky::IlluminanceFromDisc(
		SunTemperature, FMath::Asin(6.957e8 / 1.495978707e11));
	AddInfo(FString::Printf(
		TEXT("illuminance at one astronomical unit: %.0f lux; the real figure is "
			 "about 127000 above the atmosphere"), AtOneAu));
	TestTrue(*FString::Printf(TEXT("about 127 klux at 1 au (%.0f)"), AtOneAu),
		AtOneAu > 110000.0 && AtOneAu < 145000.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
