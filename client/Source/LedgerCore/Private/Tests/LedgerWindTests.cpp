// The wind on the way down. T093.

#include "LedgerWeather.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerWindProfile,
	"Ledger.Wind.ItSlowsAndTurnsOnTheWayDown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerWindProfile::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);
	const int32 Home = LedgerBodies::HomeIndex(System);
	const FLedgerBody& Body = System.Bodies[Home];
	const FLedgerAirProfile Air = LedgerAir::For(System, Home, 0.0);

	const double Latitude = FMath::DegreesToRadians(45.0);
	const double Longitude = FMath::DegreesToRadians(30.0);
	constexpr double When = 2.0 * 86400.0;

	const FVector2D Free = LedgerWeather::WindAt(
		System, Home, Air, Latitude, Longitude, When);
	const double Depth = LedgerWeather::BoundaryLayerMetres(Body, Latitude);

	AddInfo(FString::Printf(
		TEXT("the free wind at 45 N is %.1f m/s, and the friction layer is "
			 "%.0f m deep"),
		Free.Size(), Depth));

	AddInfo(TEXT("height    speed   fraction   backed by"));
	double Previous = 0.0;
	for (const double Height : { 2.0, 10.0, 50.0, 200.0, 1000.0, 5000.0 })
	{
		const FVector2D Wind = LedgerWeather::WindAtAltitude(
			System, Home, Air, Latitude, Longitude, Height, When);
		const double Turned = FMath::RadiansToDegrees(
			FMath::Atan2(Wind.Y, Wind.X) - FMath::Atan2(Free.Y, Free.X));
		AddInfo(FString::Printf(TEXT("%6.0f m  %6.1f    %5.2f    %+6.1f deg"),
			Height, Wind.Size(), Wind.Size() / FMath::Max(Free.Size(), 1e-9),
			FMath::UnwindDegrees(Turned)));

		// **Monotonic**, because a wind that got faster on the way down would
		// be a wind nobody could stand in the same room as.
		TestTrue(*FString::Printf(TEXT("wind at %.0f m is at least the wind below"),
			Height), Wind.Size() >= Previous - 1e-9);
		Previous = Wind.Size();
	}

	// **A ten-metre mast reads about two thirds of the free wind.** That is the
	// number every wind atlas is written in, and it is a consequence of the log
	// profile rather than a coefficient: ln(10/0.05) / ln(1000/0.05) is 0.53,
	// and a taller boundary layer pushes it up.
	const FVector2D AtTen = LedgerWeather::WindAtAltitude(
		System, Home, Air, Latitude, Longitude, 10.0, When);
	const double Fraction = AtTen.Size() / FMath::Max(Free.Size(), 1e-9);
	AddInfo(FString::Printf(
		TEXT("ten metres reads %.0f%% of the free wind"), Fraction * 100.0));
	TestTrue(*FString::Printf(TEXT("a ten-metre mast reads 40 to 75%% (%.0f)"),
		Fraction * 100.0), Fraction > 0.40 && Fraction < 0.75);

	// **And it is turned.** Friction breaks the geostrophic balance so the
	// surface wind crosses the isobars towards the low; twenty to thirty degrees
	// is the land figure. Above the layer there is no turn at all.
	const FVector2D Surface = LedgerWeather::WindAtAltitude(
		System, Home, Air, Latitude, Longitude, 2.0, When);
	const double SurfaceTurn = FMath::Abs(FMath::UnwindDegrees(
		FMath::RadiansToDegrees(
			FMath::Atan2(Surface.Y, Surface.X) - FMath::Atan2(Free.Y, Free.X))));
	AddInfo(FString::Printf(
		TEXT("at two metres it is turned %.1f degrees across the isobars"),
		SurfaceTurn));
	TestTrue(*FString::Printf(TEXT("the surface wind backs 15 to 30 degrees (%.1f)"),
		SurfaceTurn), SurfaceTurn > 15.0 && SurfaceTurn < 30.0);

	const FVector2D Aloft = LedgerWeather::WindAtAltitude(
		System, Home, Air, Latitude, Longitude, Depth * 2.0, When);
	TestEqual(TEXT("above the friction layer it is exactly the free wind"),
		static_cast<double>((Aloft - Free).Size()), 0.0, 1e-9);

	// **The turn is the hemisphere's.** North and south back opposite ways,
	// because the sign comes from Coriolis and not from a constant.
	const FVector2D FreeSouth = LedgerWeather::WindAt(
		System, Home, Air, -Latitude, Longitude, When);
	const FVector2D SurfaceSouth = LedgerWeather::WindAtAltitude(
		System, Home, Air, -Latitude, Longitude, 2.0, When);
	const double SouthTurn = FMath::UnwindDegrees(FMath::RadiansToDegrees(
		FMath::Atan2(SurfaceSouth.Y, SurfaceSouth.X)
		- FMath::Atan2(FreeSouth.Y, FreeSouth.X)));
	const double NorthTurn = FMath::UnwindDegrees(FMath::RadiansToDegrees(
		FMath::Atan2(Surface.Y, Surface.X) - FMath::Atan2(Free.Y, Free.X)));
	AddInfo(FString::Printf(
		TEXT("north backs %+.1f degrees and south %+.1f"), NorthTurn, SouthTurn));
	TestTrue(TEXT("the two hemispheres back opposite ways"),
		(NorthTurn > 0.0) != (SouthTurn > 0.0));

	// No air, no wind. An airless body must answer zero rather than a small
	// number, because everything downstream multiplies by it.
	const FLedgerAirProfile Nothing;
	TestEqual(TEXT("an airless body has no wind"),
		static_cast<double>(LedgerWeather::WindAtAltitude(
			System, Home, Nothing, Latitude, Longitude, 10.0, When).Size()),
		0.0, 0.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
