// Fog has a top, and the top is what makes it fog. T091.

#include "LedgerFog.h"
#include "LedgerAir.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFogNight,
	"Ledger.Fog.AClearNightMakesFogAndACloudyOneDoesNot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFogNight::RunTest(const FString&)
{
	const FLedgerAirProfile Earth = LedgerAir::Describe(
		ELedgerAir::NitrogenOxygen, 101325.0, 288.0, 9.807, 6.371e6);

	AddInfo(TEXT("hours dark   cold pool   ground cooled   fog"));
	double Deepest = 0.0;
	for (int32 Hours = 1; Hours <= 10; Hours += 3)
	{
		const double Seconds = Hours * 3600.0;
		const FLedgerFog Fog = LedgerFog::At(Earth, Seconds, -0.2);
		Deepest = FMath::Max(Deepest, Fog.DepthMetres);
		AddInfo(FString::Printf(
			TEXT("%8d   %6.0f m    %8.1f K      %s"),
			Hours, LedgerFog::InversionDepthMetres(Seconds),
			LedgerFog::NightCoolingKelvin(Earth, Seconds),
			Fog.bForms
				? *FString::Printf(TEXT("yes, %.0f m deep, visibility %.0f m"),
					Fog.DepthMetres, 3.0 / FMath::Max(Fog.ExtinctionPerMetre, 1e-9))
				: TEXT("no")));
	}

	// **A night's cooling has to be a night's cooling.** Ten to twenty kelvin
	// under a clear sky is what a still night actually does. The first version
	// said forty over ten hours and fifteen in the first one, because it drew
	// the heat out of the cold pool's own capacity -- a hundred kilogrammes of
	// air per square metre -- instead of out of the ground underneath it.
	const double Overnight = LedgerFog::NightCoolingKelvin(Earth, 10.0 * 3600.0);
	TestTrue(*FString::Printf(TEXT("ten hours cools the ground by %.1f K"), Overnight),
		Overnight > 5.0 && Overnight < 30.0);

	// And the cold pool is tens of metres deep, not tens of kilometres. That is
	// the number the whole feature rests on: a pool with a top a hundred metres
	// up fills a valley and leaves a ridge clear.
	AddInfo(FString::Printf(TEXT("the cold pool is %.0f m deep by dawn"), Deepest));
	TestTrue(*FString::Printf(TEXT("the pool has a top in the tens of metres (%.0f)"),
		Deepest), Deepest > 20.0 && Deepest < 400.0);

	// **A blanket stops it.** Ninety bars of carbon dioxide radiate almost
	// everything back, so the ground barely cools -- which is why Venus has no
	// night-time and Mars swings ninety kelvin.
	const FLedgerAirProfile Venus = LedgerAir::Describe(
		ELedgerAir::CarbonDioxide, 9.2e6, 737.0, 8.870, 6.052e6);
	const FLedgerAirProfile Mars = LedgerAir::Describe(
		ELedgerAir::CarbonDioxide, 610.0, 215.0, 3.711, 3.390e6);
	const double VenusNight = LedgerFog::NightCoolingKelvin(Venus, 10.0 * 3600.0);
	const double MarsNight = LedgerFog::NightCoolingKelvin(Mars, 10.0 * 3600.0);
	AddInfo(FString::Printf(
		TEXT("ten hours of darkness cools Venus by %.2f K and Mars by %.1f K"),
		VenusNight, MarsNight));
	TestTrue(TEXT("a thick blanket keeps the night warm"), VenusNight < 1.0);
	TestTrue(TEXT("a thin one does not"), MarsNight > VenusNight * 10.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerFogBurnOff,
	"Ledger.Fog.TheSunBurnsItOffAndSomeWorldsNeverHaveIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerFogBurnOff::RunTest(const FString&)
{
	const FLedgerAirProfile Earth = LedgerAir::Describe(
		ELedgerAir::NitrogenOxygen, 101325.0, 288.0, 9.807, 6.371e6);
	const double Night = 10.0 * 3600.0;

	AddInfo(TEXT("sun altitude   fog remaining"));
	double Previous = 2.0;
	for (int32 Degrees = -5; Degrees <= 15; Degrees += 5)
	{
		const FLedgerFog Fog = LedgerFog::At(
			Earth, Night, FMath::DegreesToRadians(static_cast<double>(Degrees)));
		AddInfo(FString::Printf(TEXT("%+11d    %.2f"), Degrees, Fog.Fraction));
		TestTrue(TEXT("fog never thickens as the sun climbs"), Fog.Fraction <= Previous);
		Previous = Fog.Fraction;
	}

	TestEqual(TEXT("before sunrise it is all there"),
		LedgerFog::At(Earth, Night, -0.1).Fraction, 1.0);
	TestEqual(TEXT("and by fifteen degrees it is gone"),
		LedgerFog::At(Earth, Night, FMath::DegreesToRadians(15.0)).Fraction, 0.0);

	// **No water, no fog.** The rule is the one that decides cloud decks in
	// T089, so a world cannot have one without the other.
	const FLedgerAirProfile Mars = LedgerAir::Describe(
		ELedgerAir::CarbonDioxide, 610.0, 215.0, 3.711, 3.390e6);
	const FLedgerAirProfile Giant = LedgerAir::Describe(
		ELedgerAir::HydrogenHelium, 101325.0, 165.0, 24.79, 6.991e7);
	TestFalse(TEXT("a carbon-dioxide world gets no ground fog"),
		LedgerFog::At(Mars, Night, -0.1).bForms);
	TestFalse(TEXT("nor does a gas giant"),
		LedgerFog::At(Giant, Night, -0.1).bForms);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
