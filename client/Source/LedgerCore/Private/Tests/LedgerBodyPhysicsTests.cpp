// What a body is like to stand on. T078.
//
// A moon is a body with a different radius, different gravity and no air. None
// of those are stored: they are the mass and the radius, read differently.

#include "LedgerSky.h"
#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	double SurfaceGravity(const FLedgerBody& Body)
	{
		return LedgerEphemeris::GravitationalConstant * Body.MassKg
			/ (Body.RadiusMetres * Body.RadiusMetres);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerBodyGravity,
	"Ledger.Body.AMoonIsLighterUnderfootThanItsPlanet",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerBodyGravity::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);

	FString Table;
	for (int32 Index = 1; Index < System.Bodies.Num(); ++Index)
	{
		const FLedgerBody& Body = System.Bodies[Index];
		Table += FString::Printf(
			TEXT("  body %d (%s): radius %.0f km, gravity %.2f m/s^2 (%.3f g), "
				 "escape %.0f m/s, %.0f K, air %s\n"),
			Index, LexToString(Body.Kind), Body.RadiusMetres / 1000.0,
			SurfaceGravity(Body), SurfaceGravity(Body) / 9.80665,
			LedgerSky::EscapeVelocity(Body),
			LedgerSky::EquilibriumTemperatureKelvin(System, Index, 0.0),
			LedgerSky::RetainsAtmosphere(System, Index, 0.0) ? TEXT("yes") : TEXT("none"));
	}
	AddInfo(FString::Printf(TEXT("the bodies of this system:\n%s"), *Table));

	const int32 MoonIndex = LedgerBodies::FirstChildOfKind(
		System, 1, ELedgerBodyKind::Moon);
	TestTrue(TEXT("there is a moon to land on"), System.Bodies.IsValidIndex(MoonIndex));
	const FLedgerBody& Planet = System.Bodies[1];
	const FLedgerBody& Moon = System.Bodies[MoonIndex];

	TestTrue(*FString::Printf(
		TEXT("the moon is smaller (%.0f km against %.0f km)"),
		Moon.RadiusMetres / 1000.0, Planet.RadiusMetres / 1000.0),
		Moon.RadiusMetres < Planet.RadiusMetres);

	// **Low gravity, and low by the amount the masses and radii say.** Not a
	// setting: a fraction of a g that nobody chose.
	const double Ratio = SurfaceGravity(Moon) / SurfaceGravity(Planet);
	AddInfo(FString::Printf(
		TEXT("the moon pulls at %.3f of the planet's surface gravity"), Ratio));
	TestTrue(*FString::Printf(TEXT("the moon is measurably lighter (%.3f)"), Ratio),
		Ratio < 0.5);
	TestTrue(TEXT("but it still pulls"), SurfaceGravity(Moon) > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerBodyAtmosphere,
	"Ledger.Body.WhoKeepsAnAtmosphereIsDerivedNotDeclared",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerBodyAtmosphere::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	TestTrue(TEXT("the planet has air"), LedgerSky::RetainsAtmosphere(System, 1, 0.0));
	TestFalse(TEXT("the moon has none"), LedgerSky::RetainsAtmosphere(System,
		LedgerBodies::FirstChildOfKind(System, 1, ELedgerBodyKind::Moon), 0.0));

	// **The rule against the real cases, which is the only reason to trust it
	// on invented ones.** Escape velocity against the thermal speed of
	// nitrogen.
	//
	// The threshold was six -- the textbook figure -- and Mercury failed this
	// test at 6.8, which is the whole point of having it. The six cases sort
	// with a clean gap between Mercury at 6.8 and Titan at 9.1, and eight sits
	// in the middle of it. The number came out of the cases rather than being
	// chosen and then defended.
	//
	// Titan is the case that makes this worth doing properly. It is SMALLER
	// than the Moon and has a thicker atmosphere than Earth, because it is
	// cold -- so any rule that looks only at size gets it backwards, and a
	// stored boolean would have to be told.
	struct FKnown
	{
		const TCHAR* Name;
		double MassKg;
		double RadiusMetres;
		double TemperatureKelvin;
		bool bExpected;
	};
	const FKnown Known[] = {
		{ TEXT("Earth"),  5.972e24, 6.371e6, 255.0, true  },
		{ TEXT("Moon"),   7.342e22, 1.737e6, 270.0, false },
		{ TEXT("Mars"),   6.417e23, 3.390e6, 210.0, true  },
		{ TEXT("Titan"),  1.345e23, 2.575e6,  94.0, true  },
		{ TEXT("Mercury"),3.301e23, 2.440e6, 440.0, false },
		{ TEXT("Ceres"),  9.394e20, 4.700e5, 235.0, false },
	};

	constexpr double Boltzmann = 1.380649e-23;
	constexpr double NitrogenMassKg = 4.6517e-26;

	FString Table;
	for (const FKnown& Case : Known)
	{
		FLedgerBody Body;
		Body.MassKg = Case.MassKg;
		Body.RadiusMetres = Case.RadiusMetres;

		const double Escape = LedgerSky::EscapeVelocity(Body);
		const double Thermal = FMath::Sqrt(
			3.0 * Boltzmann * Case.TemperatureKelvin / NitrogenMassKg);
		const double Ratio = Escape / Thermal;
		const bool bKeeps = Ratio > 8.0;

		Table += FString::Printf(TEXT("  %-8s escape %6.0f m/s, thermal %4.0f m/s, "
			"ratio %5.1f -> %s (expected %s)\n"),
			Case.Name, Escape, Thermal, Ratio,
			bKeeps ? TEXT("air") : TEXT("none"),
			Case.bExpected ? TEXT("air") : TEXT("none"));

		TestEqual(*FString::Printf(TEXT("%s"), Case.Name), bKeeps, Case.bExpected);
	}
	AddInfo(FString::Printf(TEXT("the rule against bodies somebody has been to:\n%s"),
		*Table));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerBodyEquilibrium,
	"Ledger.Body.EquilibriumTemperatureFallsWithDistance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerBodyEquilibrium::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);

	// A planet and its moon are at very nearly the same distance from the star,
	// so they must come out at very nearly the same temperature -- which is the
	// check that this is about distance and not about size.
	const double PlanetK = LedgerSky::EquilibriumTemperatureKelvin(System, 1, 0.0);
	const double MoonK = LedgerSky::EquilibriumTemperatureKelvin(System,
		LedgerBodies::FirstChildOfKind(System, 1, ELedgerBodyKind::Moon), 0.0);
	AddInfo(FString::Printf(TEXT("planet %.1f K, moon %.1f K"), PlanetK, MoonK));
	TestEqual(TEXT("a moon is as warm as the planet it orbits"), MoonK, PlanetK, 2.0);

	// And the falloff is the fourth root of the inverse square, so four times
	// the distance is half the temperature.
	FLedgerSystem Scaled = System;
	Scaled.Bodies[1].Orbit.SemiMajorAxisMetres *= 16.0;
	const double FarK = LedgerSky::EquilibriumTemperatureKelvin(Scaled, 1, 0.0);
	AddInfo(FString::Printf(
		TEXT("sixteen times as far: %.1f K against %.1f K, ratio %.4f"),
		FarK, PlanetK, FarK / PlanetK));
	TestEqual(TEXT("sixteen times the distance is a quarter the temperature"),
		FarK / PlanetK, 0.25, 0.01);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
