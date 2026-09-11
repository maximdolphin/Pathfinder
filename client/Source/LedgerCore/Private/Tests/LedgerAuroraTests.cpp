// Aurora at high latitude during a solar event, and not otherwise. T103.

#include "LedgerAurora.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerAuroraOnlyInEvents,
	"Ledger.Aurora.HighLatitudeDuringAnEventAndNotOtherwise",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerAuroraOnlyInEvents::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);
	const int32 Home = LedgerBodies::HomeIndex(System);
	const FLedgerAirProfile Air = LedgerAir::For(System, Home, 0.0);
	const double Dipole = LedgerAurora::DipoleRelative(System.Bodies[Home]);
	TestTrue(TEXT("the home world has a field"), Dipole > 0.1);

	// Two places on the magnetic meridian: one at 62 degrees magnetic, where a
	// moderate storm puts the oval, and one on the magnetic equator.
	const FVector3d Pole = LedgerAurora::MagneticPole(System, Home);
	const FVector3d Across = FVector3d::CrossProduct(Pole, FVector3d::UnitX()).GetSafeNormal();
	auto At = [&](double MagneticLatitudeDegrees)
	{
		const double Colatitude = FMath::DegreesToRadians(90.0 - MagneticLatitudeDegrees);
		return (Pole * FMath::Cos(Colatitude) + Across * FMath::Sin(Colatitude)).GetSafeNormal();
	};
	const FVector3d High = At(62.0);
	const FVector3d Equator = At(0.0);

	int32 EventHours = 0;
	int32 LitInEvents = 0;
	int32 LitOutside = 0;
	int32 LitAtEquator = 0;
	for (int32 Hour = 0; Hour < 24 * 60; ++Hour)
	{
		const double When = Hour * 3600.0;
		const FLedgerAurora Up = LedgerAurora::At(System, Home, Air,
			FMath::Asin(High.Z), FMath::Atan2(High.Y, High.X), When);
		const FLedgerAurora Low = LedgerAurora::At(System, Home, Air,
			FMath::Asin(Equator.Z), FMath::Atan2(Equator.Y, Equator.X), When);
		EventHours += Up.bEvent ? 1 : 0;
		LitInEvents += Up.bEvent && Up.Overhead > 0.2 ? 1 : 0;
		LitOutside += !Up.bEvent && Up.Overhead > 0.0 ? 1 : 0;
		LitAtEquator += Low.Overhead > 0.01 ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("dipole %.2f of Earth's; %d event hours in sixty days, aurora overhead at 62 magnetic in %d of them, %d hours lit outside an event, %d at the equator"),
		Dipole, EventHours, LitInEvents, LitOutside, LitAtEquator));
	TestTrue(TEXT("the star has events in sixty days"), EventHours > 0);
	TestTrue(TEXT("aurora is overhead at high latitude during them"), LitInEvents > 0);
	TestEqual(TEXT("and never outside them"), LitOutside, 0);
	TestEqual(TEXT("and never at the magnetic equator"), LitAtEquator, 0);

	// A moon with a day of weeks and no dynamo worth the name has none.
	const int32 Moon = LedgerBodies::FirstChildOfKind(System, Home, ELedgerBodyKind::Moon);
	if (System.Bodies.IsValidIndex(Moon))
	{
		AddInfo(FString::Printf(TEXT("the moon's dipole: %.5f of Earth's"), LedgerAurora::DipoleRelative(System.Bodies[Moon])));
	}
	return true;
}

#endif
