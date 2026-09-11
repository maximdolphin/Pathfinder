// Rain on the canopy, running down and then back; fog, and the heater. T101.

#include "LedgerCanopy.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	void Expose(FLedgerCanopy& Canopy, const FLedgerCanopyWeather& Outside, double Seconds)
	{
		for (double Elapsed = 0.0; Elapsed < Seconds; Elapsed += 1.0 / 60.0)
		{
			LedgerFlight::Expose(Canopy, Outside, 1.0 / 60.0);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerCanopyWeathering,
	"Ledger.Flight.RainStreaksTheCanopyAndTheHeaterClearsTheFog",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerCanopyWeathering::RunTest(const FString&)
{
	FLedgerCanopyWeather Rain;
	Rain.RainMillimetresPerHour = 4.0;
	Rain.AmbientKelvin = 285.0;
	Rain.CabinDewPointKelvin = 288.0;

	FLedgerCanopy Slow;
	Rain.AirspeedMetresPerSecond = 10.0;
	Expose(Slow, Rain, 30.0);
	TestTrue(TEXT("rain wets the canopy"), Slow.Water > 0.3);
	TestTrue(TEXT("slow, it runs down the glass"), Slow.Flow < 0.0);

	FLedgerCanopy Fast;
	Rain.AirspeedMetresPerSecond = 120.0;
	Expose(Fast, Rain, 30.0);
	TestTrue(TEXT("fast, it still streaks, and the streaks run back"), Fast.Water > 0.1 && Fast.Flow > 0.0);

	FLedgerCanopy Wiped = Slow;
	Wiped.bWipers = true;
	Rain.AirspeedMetresPerSecond = 10.0;
	Expose(Wiped, Rain, 10.0);
	TestTrue(TEXT("the wipers take most of the water"), Wiped.Water < 0.5 * Slow.Water);

	FLedgerCanopyWeather Cold;
	Cold.AmbientKelvin = 280.0;
	Cold.CabinDewPointKelvin = 288.0;
	FLedgerCanopy Fogged;
	Expose(Fogged, Cold, 120.0);
	TestTrue(TEXT("glass colder than the cabin's dew point fogs"), Fogged.Fog > 0.9);
	Fogged.bHeater = true;
	Expose(Fogged, Cold, 60.0);
	TestTrue(TEXT("and the heater clears it"), Fogged.Fog < 0.05);

	FLedgerCanopyWeather Freezing = Rain;
	Freezing.AmbientKelvin = 265.0;
	FLedgerCanopy Iced;
	Expose(Iced, Freezing, 60.0);
	TestTrue(TEXT("rain on glass below freezing ices it"), Iced.Ice > 0.2);
	Iced.bHeater = true;
	Expose(Iced, Freezing, 120.0);
	TestTrue(TEXT("and the heater melts it"), Iced.Ice < 0.05);

	AddInfo(FString::Printf(TEXT("slow water %.2f flow %+.2f; fast water %.2f flow %+.2f; wiped %.2f"),
		Slow.Water, Slow.Flow, Fast.Water, Fast.Flow, Wiped.Water));
	return true;
}

#endif
