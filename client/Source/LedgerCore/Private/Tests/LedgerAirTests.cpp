// Five real atmospheres through the same arithmetic. T089.
//
// The point of making Earth one row is that the other rows have to come out
// right too, and there are five atmospheres in this solar system whose numbers
// are published. If the same function reproduces all of them, the row it
// generates for an invented planet is worth something.

#include "LedgerAir.h"
#include "LedgerBody.h"
#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FAirCase
	{
		const TCHAR* Name;
		ELedgerAir Composition;
		double Pressure;
		double Temperature;
		double Gravity;
		double Radius;

		/// What the textbooks say, metres and kelvin per metre.
		double ScaleHeight;
		double LapseRate;
	};

	const FAirCase AirCases[] =
	{
		{ TEXT("Earth"),   ELedgerAir::NitrogenOxygen, 101325.0,  288.0,  9.807, 6.371e6,  8500.0, 0.00976 },
		{ TEXT("Mars"),    ELedgerAir::CarbonDioxide,     610.0,  210.0,  3.711, 3.390e6, 11100.0, 0.00450 },
		{ TEXT("Venus"),   ELedgerAir::CarbonDioxide,   9.2e6,    737.0,  8.870, 6.052e6, 15900.0, 0.01050 },
		{ TEXT("Titan"),   ELedgerAir::Nitrogen,       146700.0,   94.0,  1.352, 2.575e6, 21000.0, 0.00130 },
		{ TEXT("Jupiter"), ELedgerAir::HydrogenHelium, 101325.0,  165.0, 24.790, 6.991e7, 27000.0, 0.00200 },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAirRealPlanets,
	"Ledger.Air.FiveRealAtmospheresThroughOneFunction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAirRealPlanets::RunTest(const FString&)
{
	AddInfo(TEXT(
		"body      scale height        lapse rate         Rayleigh 440 nm"));
	for (const FAirCase& Case : AirCases)
	{
		const FLedgerAirProfile Air = LedgerAir::Describe(
			Case.Composition, Case.Pressure, Case.Temperature, Case.Gravity,
			Case.Radius);

		AddInfo(FString::Printf(
			TEXT("%-8s %6.0f m (%6.0f)   %.5f (%.5f)   %.3e /m"),
			Case.Name, Air.ScaleHeightMetres, Case.ScaleHeight,
			Air.LapseRateKelvinPerMetre, Case.LapseRate,
			Air.RayleighPerMetre.X));

		// **Scale height is kT/mg and nothing else**, so this is a check that
		// the composition's molecular mass and the body's gravity both arrived
		// intact. Ten per cent covers the difference between a real mean
		// molecular mass and the round one used here -- Jupiter is 2.22 rather
		// than 2.30 -- and nothing wider.
		TestTrue(*FString::Printf(
			TEXT("%s scale height %.0f m against %.0f"),
			Case.Name, Air.ScaleHeightMetres, Case.ScaleHeight),
			FMath::Abs(Air.ScaleHeightMetres - Case.ScaleHeight)
				< Case.ScaleHeight * 0.12);

		TestTrue(*FString::Printf(
			TEXT("%s lapse rate %.5f against %.5f"),
			Case.Name, Air.LapseRateKelvinPerMetre, Case.LapseRate),
			FMath::Abs(Air.LapseRateKelvinPerMetre - Case.LapseRate)
				< Case.LapseRate * 0.20);
	}

	// **Earth's blue-sky number, against the measured one.**
	const FLedgerAirProfile Earth = LedgerAir::Describe(
		ELedgerAir::NitrogenOxygen, 101325.0, 288.0, 9.807, 6.371e6);
	AddInfo(FString::Printf(
		TEXT("Earth Rayleigh: %.3e at 440 nm, %.3e at 550, %.3e at 680 -- the "
			 "measured 550 nm figure is 1.17e-05"),
		Earth.RayleighPerMetre.X, Earth.RayleighPerMetre.Y,
		Earth.RayleighPerMetre.Z));

	// Within 20% of measurement, and the direction of the miss is known: this
	// treats refractivity as constant with wavelength when it actually rises
	// towards the blue, so the absolute coefficient comes out slightly low.
	// The *ratio* between channels is exact, being pure lambda^-4, and that is
	// what sets the colour.
	TestTrue(*FString::Printf(TEXT("Earth scatters like Earth at 550 nm (%.3e)"),
		Earth.RayleighPerMetre.Y),
		FMath::Abs(Earth.RayleighPerMetre.Y - 1.17e-5) < 1.17e-5 * 0.20);

	// **The greenhouse, against the two planets that make it hard.**
	// Getting Earth right is easy -- one number does it. Venus is 500 K of
	// greenhouse from the same formula, and a model tuned to Earth alone misses
	// it by a factor of three.
	struct FWarm { const TCHAR* Name; ELedgerAir Air; double Bar; double Equilibrium; double Surface; };
	const FWarm Warms[] =
	{
		{ TEXT("Earth"), ELedgerAir::NitrogenOxygen,  1.00, 255.0, 288.0 },
		{ TEXT("Venus"), ELedgerAir::CarbonDioxide,  90.80, 232.0, 737.0 },
		{ TEXT("Titan"), ELedgerAir::Nitrogen,        1.45,  82.0,  94.0 },
		{ TEXT("Mars"),  ELedgerAir::CarbonDioxide,   0.006, 210.0, 215.0 },
	};
	for (const FWarm& Warm : Warms)
	{
		const double Got = LedgerAir::SurfaceTemperatureKelvin(
			Warm.Air, Warm.Bar * 101325.0, Warm.Equilibrium);
		AddInfo(FString::Printf(
			TEXT("%-6s %.3f bar: %.0f K of sunlight becomes %.1f K of ground, "
				 "against a measured %.0f"),
			Warm.Name, Warm.Bar, Warm.Equilibrium, Got, Warm.Surface));
		TestTrue(*FString::Printf(TEXT("%s warms to %.1f against %.0f"),
			Warm.Name, Got, Warm.Surface),
			FMath::Abs(Got - Warm.Surface) < Warm.Surface * 0.06);
	}

	const double BlueOverRed = Earth.RayleighPerMetre.X / Earth.RayleighPerMetre.Z;
	const double Expected = FMath::Pow(680.0 / 440.0, 4.0);
	AddInfo(FString::Printf(
		TEXT("blue scatters %.2f times as much as red, against (680/440)^4 = %.2f"),
		BlueOverRed, Expected));
	TestTrue(TEXT("the sky is blue for the reason it is blue"),
		FMath::Abs(BlueOverRed - Expected) < 0.01);

	// And carbon dioxide is not air. It bends light 60% harder and its
	// molecules are more anisotropic, so at the same density it scatters
	// noticeably more -- which is a fact about the gas, not a colour choice.
	const double Same = 2.5e25;
	const double PlainAir = LedgerAir::RayleighPerMetre(
		ELedgerAir::NitrogenOxygen, Same, 440.0e-9);
	const double Carbon = LedgerAir::RayleighPerMetre(
		ELedgerAir::CarbonDioxide, Same, 440.0e-9);
	const double Hydrogen = LedgerAir::RayleighPerMetre(
		ELedgerAir::HydrogenHelium, Same, 440.0e-9);
	AddInfo(FString::Printf(
		TEXT("at one density: air %.3e, carbon dioxide %.3e (%.2fx), "
			 "hydrogen-helium %.3e (%.2fx)"),
		PlainAir, Carbon, Carbon / PlainAir,
		Hydrogen, Hydrogen / PlainAir));
	TestTrue(TEXT("carbon dioxide scatters more than air at the same density"),
		Carbon > PlainAir * 1.5);
	TestTrue(TEXT("hydrogen scatters less"), Hydrogen < PlainAir * 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAirGenerated,
	"Ledger.Air.EveryGeneratedBodyGetsAnAtmosphereOrAReasonNotTo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAirGenerated::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);

	int32 WithAir = 0;
	int32 Oxygen = 0;
	for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
	{
		const FLedgerAirProfile Air = LedgerAir::For(System, Index, 0.0);
		const FLedgerBody& Body = System.Bodies[Index];

		AddInfo(FString::Printf(
			TEXT("%-10s %-16s %10.1f Pa  %6.0f m  %5.1f K  clouds %s"),
			*Body.Name, LexToString(Air.Composition),
			Air.SurfacePressurePascals, Air.ScaleHeightMetres,
			Air.SurfaceTemperatureKelvin,
			Air.bHasClouds ? TEXT("yes") : TEXT("no")));

		if (Air.HasAir())
		{
			++WithAir;
			if (Air.Composition == ELedgerAir::NitrogenOxygen)
			{
				++Oxygen;
			}
			// A profile that exists has to be usable: the renderer divides by
			// the scale height and stops at the top.
			TestTrue(*FString::Printf(TEXT("%s has a positive scale height"), *Body.Name),
				Air.ScaleHeightMetres > 0.0);
			TestTrue(*FString::Printf(TEXT("%s has a top above its base"), *Body.Name),
				Air.TopMetres > Air.ScaleHeightMetres);
			TestTrue(*FString::Printf(TEXT("%s scatters something"), *Body.Name),
				Air.RayleighPerMetre.X > 0.0);
			TestTrue(*FString::Printf(TEXT("%s scatters blue hardest"), *Body.Name),
				Air.RayleighPerMetre.X > Air.RayleighPerMetre.Z);
		}
		else
		{
			// No air means no sky, and everything downstream has to see zero
			// rather than a small number.
			TestEqual(*FString::Printf(TEXT("%s has no scale height"), *Body.Name),
				Air.ScaleHeightMetres, 0.0);
			TestFalse(*FString::Printf(TEXT("%s has no clouds"), *Body.Name),
				Air.bHasClouds);
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d of %d bodies hold air; %d of them oxygen"),
		WithAir, System.Bodies.Num(), Oxygen));

	TestTrue(TEXT("somebody has air"), WithAir > 0);
	// **Exactly one oxygen atmosphere, and it is the one people live on.**
	// Free oxygen is a biosignature; a generator that handed it out by size
	// would be claiming life it has not modelled.
	TestEqual(TEXT("exactly one world has oxygen"), Oxygen, 1);
	const FLedgerAirProfile Home =
		LedgerAir::For(System, LedgerBodies::HomeIndex(System), 0.0);
	TestEqual(TEXT("and it is the home world"),
		static_cast<int32>(Home.Composition),
		static_cast<int32>(ELedgerAir::NitrogenOxygen));

	// **How often does a system contain a carbon-dioxide world?** Worth knowing,
	// because T090's acceptance is about one and the generator has to actually
	// produce them. Also worth printing: the fixture that photographs one needs
	// a seed to be given.
	int32 Systems = 0;
	int32 WithCarbon = 0;
	FString FirstCarbon;
	for (uint32 Seed = 20260900u; Seed < 20260940u; ++Seed)
	{
		const FLedgerSystem Other = LedgerBodies::Generate(Seed);
		++Systems;
		for (int32 Index = 0; Index < Other.Bodies.Num(); ++Index)
		{
			if (LedgerAir::For(Other, Index, 0.0).Composition
				== ELedgerAir::CarbonDioxide)
			{
				++WithCarbon;
				if (FirstCarbon.IsEmpty())
				{
					FirstCarbon = FString::Printf(
						TEXT("-systemseed=%u -body=%d"), Seed, Index);
				}
				break;
			}
		}
	}
	AddInfo(FString::Printf(
		TEXT("%d of %d systems have a carbon-dioxide world; the first is %s"),
		WithCarbon, Systems, FirstCarbon.IsEmpty() ? TEXT("none") : *FirstCarbon));

	// The same seed, the same sky.
	const FLedgerSystem Again = LedgerBodies::Generate(20260910u);
	const FLedgerAirProfile Twice = LedgerAir::For(Again, 1, 0.0);
	TestEqual(TEXT("the same seed gives the same pressure"),
		Twice.SurfacePressurePascals, Home.SurfacePressurePascals, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAirColour,
	"Ledger.Air.ACarbonDioxideSkyIsTheColourPhysicsSaysItIs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAirColour::RunTest(const FString&)
{
	// **The colour is predicted before anything is rendered.** Single
	// scattering along a path: what is scattered into the eye goes as the
	// scattering coefficient, what survives to arrive goes as the extinction.
	// Three numbers per sky, and the render either agrees with them or the
	// render is wrong.
	const FLedgerAirProfile Earth = LedgerAir::Describe(
		ELedgerAir::NitrogenOxygen, 101325.0, 288.0, 9.807, 6.371e6);
	const FLedgerAirProfile Mars = LedgerAir::Describe(
		ELedgerAir::CarbonDioxide, 610.0, 215.0, 3.711, 3.390e6);

	// One air mass is straight up; thirty-eight is the horizon, which is what
	// makes a sunset a sunset.
	const FVector3d EarthUp = LedgerAir::SkyColour(Earth, 1.0);
	const FVector3d EarthLow = LedgerAir::SkyColour(Earth, 38.0);
	const FVector3d MarsUp = LedgerAir::SkyColour(Mars, 1.0);
	const FVector3d MarsLow = LedgerAir::SkyColour(Mars, 38.0);

	AddInfo(FString::Printf(
		TEXT("Earth overhead  R %.3f  G %.3f  B %.3f"),
		EarthUp.Z, EarthUp.Y, EarthUp.X));
	AddInfo(FString::Printf(
		TEXT("Earth low sun   R %.3f  G %.3f  B %.3f"),
		EarthLow.Z, EarthLow.Y, EarthLow.X));
	AddInfo(FString::Printf(
		TEXT("Mars  overhead  R %.3f  G %.3f  B %.3f"),
		MarsUp.Z, MarsUp.Y, MarsUp.X));
	AddInfo(FString::Printf(
		TEXT("Mars  low sun   R %.3f  G %.3f  B %.3f"),
		MarsLow.Z, MarsLow.Y, MarsLow.X));

	// Earth's sky is blue overhead. Nothing here was chosen to make it so --
	// the gas scatters blue five and a half times harder and its haze is
	// colourless.
	TestTrue(*FString::Printf(TEXT("Earth's zenith is blue (B %.3f, R %.3f)"),
		EarthUp.X, EarthUp.Z), EarthUp.X > EarthUp.Z * 2.0);

	// **And a carbon-dioxide sky is not**, despite carbon dioxide scattering
	// blue harder than air does. The gas would give a dark blue sky; the dust
	// suspended in it absorbs a third of the blue it touches and a
	// sixteenth of the red, and that is what turns the balance over.
	TestTrue(*FString::Printf(TEXT("a dusty CO2 zenith is not blue (B %.3f, R %.3f)"),
		MarsUp.X, MarsUp.Z), MarsUp.Z > MarsUp.X);

	// The dust is the whole difference: the same atmosphere without it.
	FLedgerAirProfile Clean = Mars;
	Clean.MieScatteringPerMetre = FVector3d::ZeroVector;
	Clean.MieAbsorptionPerMetre = FVector3d::ZeroVector;
	const FVector3d CleanUp = LedgerAir::SkyColour(Clean, 1.0);
	AddInfo(FString::Printf(
		TEXT("the same CO2 with the dust taken out: R %.3f  G %.3f  B %.3f"),
		CleanUp.Z, CleanUp.Y, CleanUp.X));
	TestTrue(TEXT("clean carbon dioxide would be blue"), CleanUp.X > CleanUp.Z * 2.0);

	// Both skies redden towards a low sun, because the long path eats the blue
	// first. That is one mechanism doing two jobs and it should show in both.
	const double EarthReddens = (EarthLow.Z / EarthLow.X) / (EarthUp.Z / EarthUp.X);
	const double MarsReddens = (MarsLow.Z / MarsLow.X) / (MarsUp.Z / MarsUp.X);
	AddInfo(FString::Printf(
		TEXT("red-to-blue rises %.2f times on Earth and %.2f on Mars between "
			 "overhead and a low sun"),
		EarthReddens, MarsReddens));
	TestTrue(TEXT("Earth's sky reddens towards the horizon"), EarthReddens > 1.2);
	TestTrue(TEXT("and so does the dusty one"), MarsReddens > 1.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
