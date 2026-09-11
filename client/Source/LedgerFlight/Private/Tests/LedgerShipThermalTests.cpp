// Full power with the radiators in, and the schedule the model predicts. T111.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipOverheats,
	"Ledger.Ships.RadiatorsRetractedTheShipOverheatsOnSchedule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipOverheats::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}
	const int32 Reactor = Ship.FindComponent(TEXT("reactor"));
	const int32 Radiator = Ship.FindComponent(TEXT("radiator"));

	// Run a ship at full power for a while, a second at a time.
	auto Run = [&Ship](FLedgerShipState& State, double Seconds, int32 Watch, double UntilK, double& OutReachedAt)
	{
		OutReachedAt = -1.0;
		for (double Elapsed = 0.0; Elapsed < Seconds; Elapsed += 1.0)
		{
			LedgerShipSystems::SolvePower(Ship, State);
			LedgerShipSystems::StepThermal(Ship, State, 1.0);
			if (OutReachedAt < 0.0 && State.Components[Watch].TemperatureK >= UntilK)
			{
				OutReachedAt = Elapsed + 1.0;
			}
		}
	};

	// Deployed: the loop takes the reactor's 200 kW and more; it holds.
	FLedgerShipState Cool = FLedgerShipState::For(Ship);
	double Reached = 0.0;
	Run(Cool, 1800.0, Reactor, FLedgerThermal::ThrottleK, Reached);
	AddInfo(FString::Printf(TEXT("radiator out: reactor at %.1f K after half an hour"), Cool.Components[Reactor].TemperatureK));
	TestTrue(TEXT("with the radiator out the reactor never reaches its throttle point"), Reached < 0.0);

	// Retracted: the prediction first, from the same numbers the model uses.
	// Heat in Q is the reactor's share of its output, less what a retracted
	// radiator still takes; it loses k per kelvin to the hull; it holds C per
	// kelvin. T(t) = T0 + (Q/k)(1 - exp(-k t / C)), so it reaches the throttle
	// point at t = -(C/k) ln(1 - k (Tthrottle - T0) / Q).
	// The reactor follows its load, so what it makes is what the ship draws.
	FLedgerShipState Load = FLedgerShipState::For(Ship);
	LedgerShipSystems::SolvePower(Ship, Load);
	const double Output = Load.Components[Reactor].PowerKw;
	const double Rating = Ship.Components[Reactor].Param(TEXT("outputKw"));
	const double Q = Output * FLedgerThermal::HeatShare(Ship.Components[Reactor])
		- FMath::Min(Output * FLedgerThermal::HeatShare(Ship.Components[Reactor]),
			Ship.Components[Radiator].Param(TEXT("rejectKw")) * FLedgerThermal::RetractedShare);
	const double C = Ship.Components[Reactor].MassKg * FLedgerThermal::KjPerKgK;
	const double K = FLedgerThermal::PassiveKwPerK;
	const double Predicted = -(C / K) * FMath::Loge(1.0 - K * (FLedgerThermal::ThrottleK - FLedgerThermal::AmbientK) / Q);

	FLedgerShipState Hot = FLedgerShipState::For(Ship);
	Hot.Components[Radiator].bDeployed = false;
	Run(Hot, 3600.0, Reactor, FLedgerThermal::ThrottleK, Reached);
	AddInfo(FString::Printf(TEXT("radiator in: the reactor reaches %.0f K at %.0f s against %.0f s predicted, and ends at %.0f K, condition %.2f"),
		FLedgerThermal::ThrottleK, Reached, Predicted, Hot.Components[Reactor].TemperatureK, Hot.Components[Reactor].Condition));
	TestTrue(TEXT("with the radiator in it overheats"), Reached > 0.0);
	TestTrue(TEXT("on the schedule the model predicts, within two per cent"), FMath::Abs(Reached - Predicted) <= 0.02 * Predicted + 1.0);

	// Throttled, it gives less: the power the reactor makes once it is hot.
	FLedgerShipState Probe = Hot;
	const FLedgerPowerReport Report = LedgerShipSystems::SolvePower(Ship, Probe);
	TestTrue(TEXT("and once over the throttle point it makes less than its rating"), Report.SupplyKw < Rating);
	return true;
}

#endif
