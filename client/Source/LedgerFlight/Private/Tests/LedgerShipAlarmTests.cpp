// Which system is in trouble, from the sound alone. T126.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipAlarms,
	"Ledger.Ships.EverySystemInTroubleSoundsItsOwnAlarm",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipAlarms::RunTest(const FString&)
{
	// Every kind sounds different: no two share a tone and a rhythm.
	constexpr int32 Kinds = static_cast<int32>(ELedgerAlarm::ServiceDue) + 1;
	TSet<FString> Voices;
	for (int32 Kind = 0; Kind < Kinds; ++Kind)
	{
		double Hz = 0.0;
		double On = 0.0;
		double Off = 0.0;
		int32 Urgency = 0;
		LedgerShipSystems::AlarmVoice(static_cast<ELedgerAlarm>(Kind), Hz, On, Off, Urgency);
		Voices.Add(FString::Printf(TEXT("%.0f/%.2f/%.2f"), Hz, On, Off));
	}
	TestEqual(TEXT("every alarm kind has a voice of its own"), Voices.Num(), Kinds);

	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}
	auto Raised = [&Ship](FLedgerShipState& State, ELedgerAirWarning Air)
	{
		const FLedgerPowerReport Power = LedgerShipSystems::SolvePower(Ship, State);
		return LedgerShipSystems::Alarms(Ship, State, Power, Air);
	};
	auto Has = [](const TArray<FLedgerAlarm>& Alarms, ELedgerAlarm Kind)
	{
		return Alarms.ContainsByPredicate([Kind](const FLedgerAlarm& Alarm) { return Alarm.Kind == Kind; });
	};

	FLedgerShipState Quiet = FLedgerShipState::For(Ship);
	TestEqual(TEXT("a healthy ship is silent"), Raised(Quiet, ELedgerAirWarning::None).Num(), 0);

	struct FTrouble
	{
		const TCHAR* What;
		ELedgerAlarm Expect;
		TFunction<void(FLedgerShipState&)> Make;
		ELedgerAirWarning Air;
	};
	const FTrouble Troubles[] =
	{
		{ TEXT("an overheating reactor"), ELedgerAlarm::ReactorOverheat, [&Ship](FLedgerShipState& S) { S.Components[Ship.FindComponent(TEXT("reactor"))].TemperatureK = 450.0; }, ELedgerAirWarning::None },
		{ TEXT("an overloaded bus"), ELedgerAlarm::PowerShed, [&Ship](FLedgerShipState& S) { S.Components[Ship.FindComponent(TEXT("reactor"))].Condition = 0.25; }, ELedgerAirWarning::None },
		{ TEXT("a nearly dry tank"), ELedgerAlarm::FuelLow, [&Ship](FLedgerShipState& S) { S.Components[Ship.FindComponent(TEXT("tank"))].ContentsKg = 10.0; }, ELedgerAirWarning::None },
		{ TEXT("a breach"), ELedgerAlarm::HullBreach, [&Ship](FLedgerShipState& S) { LedgerShipSystems::HitHull(Ship, S, FVector3d(-3.0, 0.0, 0.0), 1.0e6); }, ELedgerAirWarning::None },
		{ TEXT("thin air"), ELedgerAlarm::LowOxygen, [](FLedgerShipState&) {}, ELedgerAirWarning::LowOxygen },
		{ TEXT("stale air"), ELedgerAlarm::HighCarbonDioxide, [](FLedgerShipState&) {}, ELedgerAirWarning::HighCarbonDioxide },
		{ TEXT("a failing hull"), ELedgerAlarm::StructuralFailure, [](FLedgerShipState& S) { S.HullIntegrity = 0.1; }, ELedgerAirWarning::None },
		{ TEXT("a destroyed coupling"), ELedgerAlarm::ComponentDestroyed, [&Ship](FLedgerShipState& S) { S.Components[Ship.FindComponent(TEXT("coupling_aft"))].Condition = 0.0; }, ELedgerAirWarning::None },
		{ TEXT("a worn engine"), ELedgerAlarm::ServiceDue, [&Ship](FLedgerShipState& S) { S.Components[Ship.FindComponent(TEXT("main_engine"))].Condition = 0.3; }, ELedgerAirWarning::None },
	};
	FString Heard = TEXT("\n");
	for (const FTrouble& Trouble : Troubles)
	{
		FLedgerShipState State = FLedgerShipState::For(Ship);
		Trouble.Make(State);
		const TArray<FLedgerAlarm> Alarms = Raised(State, Trouble.Air);
		TestTrue(FString::Printf(TEXT("%s sounds its own alarm"), Trouble.What), Has(Alarms, Trouble.Expect));
		if (Alarms.Num() > 0)
		{
			Heard += FString::Printf(TEXT("  %-24s %4.0f Hz, %.2f s on, %.2f s off: %s\n"),
				Trouble.What, Alarms[0].ToneHz, Alarms[0].OnSeconds, Alarms[0].OffSeconds, *Alarms[0].Says);
		}
	}
	AddInfo(FString::Printf(TEXT("what each sounds like, most urgent first:%s"), *Heard));
	return true;
}

#endif
