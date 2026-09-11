// The aureole's numbers, before anything is rendered with them. T090.

#include "LedgerAir.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAureoleBlueWhereTheDustIsBig,
	"Ledger.Air.TheAureoleIsBlueWhereTheDustIsBig",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAureoleBlueWhereTheDustIsBig::RunTest(const FString& Parameters)
{
	const FLedgerAirProfile Mars = LedgerAir::Describe(ELedgerAir::CarbonDioxide, 610.0, 210.0, 3.711, 3.390e6);
	const FLedgerAirProfile Earth = LedgerAir::Describe(ELedgerAir::NitrogenOxygen, 101325.0, 288.0, 9.807, 6.371e6);
	const FVector3d Width = LedgerAir::AureoleWidthRadians(Mars);
	const FVector3d Share = LedgerAir::AureoleFraction(Mars);
	const FVector3d EarthShare = LedgerAir::AureoleFraction(Earth);
	AddInfo(FString::Printf(TEXT("Mars: width %.3f %.3f %.3f rad, share %.3f %.3f %.3f (440, 550, 680 nm); Earth share %.3f %.3f %.3f"),
		Width.X, Width.Y, Width.Z, Share.X, Share.Y, Share.Z, EarthShare.X, EarthShare.Y, EarthShare.Z));

	// Narrower in the blue, as the wavelength over the radius says.
	TestTrue(TEXT("the peak is narrower in the blue"), Width.X < Width.Y && Width.Y < Width.Z);
	// And more of the blue in it: the dust absorbs blue, so what it does scatter
	// of the blue is mostly diffraction.
	TestTrue(TEXT("the blue share is the largest"), Share.X > Share.Z && Share.Z > 0.1);
	// Sub-micron haze is what one lobe already describes.
	TestTrue(TEXT("Earth's haze is left to the engine's lobe"), EarthShare.GetMax() < 0.05);
	return true;
}

#endif
