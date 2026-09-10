// Cloud altitude is a thermometer reading. T094.

#include "LedgerCloud.h"
#include "LedgerAir.h"
#include "LedgerBody.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerCloudHeights,
	"Ledger.Cloud.TheDecksSitWhereTheThermometerPutsThem",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerCloudHeights::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);
	const int32 Home = LedgerBodies::HomeIndex(System);

	// Earth's own numbers through the same function, because the heights every
	// pilot knows are the ones this has to reproduce.
	const FLedgerAirProfile Earth = LedgerAir::Describe(
		ELedgerAir::NitrogenOxygen, 101325.0, 288.0, 9.807, 6.371e6);
	const FLedgerCloudDecks Decks = LedgerCloud::DecksAt(
		System, Home, Earth, FMath::DegreesToRadians(45.0), 0.0, 0.0);

	AddInfo(FString::Printf(
		TEXT("a dry parcel cools at %.2f K/km and the atmosphere at %.2f"),
		Earth.LapseRateKelvinPerMetre * 1000.0,
		LedgerCloud::EnvironmentalLapseRate(Earth) * 1000.0));
	AddInfo(FString::Printf(
		TEXT("cumulus %.0f to %.0f m, middle %.0f to %.0f, cirrus %.0f to %.0f, "
			 "tropopause %.0f"),
		Decks.Cumulus.BaseMetres, Decks.Cumulus.TopMetres,
		Decks.Middle.BaseMetres, Decks.Middle.TopMetres,
		Decks.Cirrus.BaseMetres, Decks.Cirrus.TopMetres,
		Decks.TropopauseMetres));

	// **The cumulus base is the lifting condensation level, and it is low.**
	// A thousand to two thousand metres is where fair-weather cumulus sit, and
	// the flat bottom is the giveaway that it is a temperature and not a taste.
	TestTrue(*FString::Printf(TEXT("cumulus base is 900-1800 m (%.0f)"),
		Decks.Cumulus.BaseMetres),
		Decks.Cumulus.BaseMetres > 900.0 && Decks.Cumulus.BaseMetres < 1800.0);

	// Cirrus is high because it is ice: minus forty, which on Earth is between
	// six and nine kilometres.
	TestTrue(*FString::Printf(TEXT("cirrus base is 6000-9500 m (%.0f)"),
		Decks.Cirrus.BaseMetres),
		Decks.Cirrus.BaseMetres > 6000.0 && Decks.Cirrus.BaseMetres < 9500.0);

	// And the tropopause is where a jet cruises.
	TestTrue(*FString::Printf(TEXT("the tropopause is 9-13 km (%.0f)"),
		Decks.TropopauseMetres),
		Decks.TropopauseMetres > 9000.0 && Decks.TropopauseMetres < 13000.0);

	// **Three decks, stacked and not overlapping.** The middle one begins where
	// the cumulus glaciates and the cirrus begins above it, which is an ordering
	// that falls out of the temperatures rather than being imposed.
	TestTrue(TEXT("the decks are in order and do not overlap"),
		Decks.Cumulus.BaseMetres < Decks.Cumulus.TopMetres
		&& Decks.Cumulus.TopMetres <= Decks.Middle.BaseMetres
		&& Decks.Middle.BaseMetres < Decks.Middle.TopMetres
		&& Decks.Middle.TopMetres <= Decks.Cirrus.BaseMetres
		&& Decks.Cirrus.BaseMetres < Decks.Cirrus.TopMetres);

	// **The two lapse rates are different and that is the point.** Using the
	// dry rate for the environment puts cirrus at five kilometres instead of
	// eight and a half, which is the sort of error that looks like a style
	// choice about how high the sky is.
	const double Dry = Earth.LapseRateKelvinPerMetre;
	const double Environment = LedgerCloud::EnvironmentalLapseRate(Earth);
	AddInfo(FString::Printf(
		TEXT("cirrus would sit at %.0f m if the environment cooled at the dry "
			 "rate, against %.0f as it is"),
		(288.0 - 233.0) / Dry, Decks.Cirrus.BaseMetres));
	TestTrue(TEXT("the environment cools more slowly than a dry parcel"),
		Environment < Dry * 0.8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerCloudCoverage,
	"Ledger.Cloud.ADeepLowIsOvercastAndARidgeIsNot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerCloudCoverage::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);
	const int32 Home = LedgerBodies::HomeIndex(System);
	const FLedgerAirProfile Air = LedgerAir::For(System, Home, 0.0);
	constexpr double When = 2.5 * 86400.0;

	// Walk a line of longitude and record how the sky changes with the pressure
	// underneath it.
	double Lowest = TNumericLimits<double>::Max();
	double Highest = -TNumericLimits<double>::Max();
	double MostCover = 0.0;
	double LeastCover = 1.0;
	AddInfo(TEXT("longitude   pressure     cumulus   middle"));
	for (int32 Step = 0; Step < 12; ++Step)
	{
		const double Longitude = -LedgerPi + Step * LedgerTwoPi / 12.0;
		const double Latitude = FMath::DegreesToRadians(40.0);
		const double Pressure = LedgerWeather::PressureAt(
			System, Home, Air, Latitude, Longitude, When);
		const FLedgerCloudDecks Decks = LedgerCloud::DecksAt(
			System, Home, Air, Latitude, Longitude, When);

		AddInfo(FString::Printf(TEXT("%+9.0f   %8.0f Pa   %5.2f    %5.2f"),
			FMath::RadiansToDegrees(Longitude), Pressure,
			Decks.Cumulus.Coverage, Decks.Middle.Coverage));

		Lowest = FMath::Min(Lowest, Pressure);
		Highest = FMath::Max(Highest, Pressure);
		MostCover = FMath::Max(MostCover, Decks.Cumulus.Coverage);
		LeastCover = FMath::Min(LeastCover, Decks.Cumulus.Coverage);
	}

	AddInfo(FString::Printf(
		TEXT("pressure ran from %.0f to %.0f Pa and cumulus cover from %.2f "
			 "to %.2f"),
		Lowest, Highest, LeastCover, MostCover));

	// **The sky is not uniformly overcast, and that is the fix T088 needed.**
	// A deck that always covers everything hides whatever is above it, which
	// is how a moon that the ephemeris placed correctly ended up behind cloud
	// in every frame.
	TestTrue(*FString::Printf(TEXT("somewhere is broken cloud (%.2f)"), LeastCover),
		LeastCover < 0.45);
	TestTrue(TEXT("and somewhere else has more of it"), MostCover > LeastCover + 0.1);

	// No air, no cloud. And a body that cannot hold liquid water gets no
	// cumulus, by the same rule that gives it no fog.
	const FLedgerAirProfile Mars = LedgerAir::Describe(
		ELedgerAir::CarbonDioxide, 610.0, 215.0, 3.711, 3.390e6);
	const FLedgerCloudDecks Dusty =
		LedgerCloud::DecksAt(System, Home, Mars, 0.0, 0.0, When);
	TestFalse(TEXT("a carbon-dioxide world gets no water cumulus"),
		Dusty.Cumulus.bPresent);

	const FLedgerAirProfile Nothing;
	const FLedgerCloudDecks Empty =
		LedgerCloud::DecksAt(System, Home, Nothing, 0.0, 0.0, When);
	TestFalse(TEXT("and an airless one gets nothing at all"),
		Empty.Cumulus.bPresent || Empty.Middle.bPresent || Empty.Cirrus.bPresent);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
