// Shields against thrust, and running cold and quiet. T119 and T120.

#include "LedgerShipDefinition.h"
#include "LedgerShipSystems.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipShields,
	"Ledger.Ships.RaisingShieldsTakesPowerFromThrust",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipShields::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}
	const int32 Shield = Ship.FindComponent(TEXT("shield"));
	const int32 Engine = Ship.FindComponent(TEXT("main_engine"));
	const int32 Reactor = Ship.FindComponent(TEXT("reactor"));
	if (!TestTrue(TEXT("it has a shield"), Shield != INDEX_NONE))
	{
		return false;
	}

	FLedgerShipState Down = FLedgerShipState::For(Ship);
	const FLedgerPowerReport Lowered = LedgerShipSystems::SolvePower(Ship, Down);
	FLedgerShipState Up = FLedgerShipState::For(Ship);
	Up.Components[Shield].bOn = true;
	const FLedgerPowerReport Raised = LedgerShipSystems::SolvePower(Ship, Up);
	AddInfo(FString::Printf(TEXT("power left for thrust: %.0f kW with the shield down, %.0f kW with it up"),
		Lowered.ThrustHeadroomKw, Raised.ThrustHeadroomKw));
	TestEqual(TEXT("raising the shield takes its draw from what thrust can have"),
		Lowered.ThrustHeadroomKw - Raised.ThrustHeadroomKw, Ship.Components[Shield].Param(TEXT("drawKw")));

	// On a tired reactor the difference is the engine itself.
	FLedgerShipState Tired = FLedgerShipState::For(Ship);
	Tired.Components[Reactor].Condition = 0.7;
	LedgerShipSystems::SolvePower(Ship, Tired);
	const bool bEngineBefore = Tired.Components[Engine].bPowered;
	Tired.Components[Shield].bOn = true;
	LedgerShipSystems::SolvePower(Ship, Tired);
	AddInfo(FString::Printf(TEXT("a reactor at 70%%: main engine powered %d before the shield, %d after"),
		bEngineBefore ? 1 : 0, Tired.Components[Engine].bPowered ? 1 : 0));
	TestTrue(TEXT("on a tired reactor, raising the shield sheds the main engine"), bEngineBefore && !Tired.Components[Engine].bPowered);

	// Charged, the facing sector takes a hit and the others are untouched.
	for (int32 Second = 0; Second < 60; ++Second)
	{
		LedgerShipSystems::SolvePower(Ship, Up);
		LedgerShipSystems::StepShields(Ship, Up, 1.0);
	}
	const double Aft = Up.Components[Shield].SectorKj[1];
	const double Through = LedgerShipSystems::AbsorbHit(Ship, Up, FVector3d(1.0, 0.1, 0.0), 1.0e6);
	AddInfo(FString::Printf(TEXT("charged: %.0f kJ a sector; a megajoule from ahead leaves %.0f kJ fore and passes %.0f kJ"),
		Aft, Up.Components[Shield].SectorKj[0], Through / 1000.0));
	TestTrue(TEXT("the fore sector takes the hit from ahead"), Up.Components[Shield].SectorKj[0] < Aft);
	TestTrue(TEXT("and the aft sector is untouched"), Up.Components[Shield].SectorKj[1] == Aft);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipSignature,
	"Ledger.Ships.RunningColdAndQuietShortensDetectionRange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipSignature::RunTest(const FString&)
{
	FLedgerShipDefinition Ship;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("the courier loads"), LedgerShips::Load(LedgerShips::DefaultDirectory(), TEXT("courier"), Ship, Errors)))
	{
		return false;
	}
	auto Run = [&Ship](FLedgerShipState& State)
	{
		for (int32 Second = 0; Second < 600; ++Second)
		{
			LedgerShipSystems::SolvePower(Ship, State);
			LedgerShipSystems::StepThermal(Ship, State, 1.0);
		}
		return LedgerShipSystems::Signature(Ship, State);
	};
	FLedgerShipState Loud = FLedgerShipState::For(Ship);
	Loud.Components[Ship.FindComponent(TEXT("shield"))].bOn = true;
	const FLedgerSignature Hot = Run(Loud);

	// Cold and quiet: shield down, drives off, radiator in.
	FLedgerShipState Quiet = FLedgerShipState::For(Ship);
	Quiet.Components[Ship.FindComponent(TEXT("main_engine"))].bOn = false;
	Quiet.Components[Ship.FindComponent(TEXT("rcs"))].bOn = false;
	Quiet.Components[Ship.FindComponent(TEXT("radiator"))].bDeployed = false;
	const FLedgerSignature Cold = Run(Quiet);

	AddInfo(FString::Printf(TEXT("everything on: %.0f kW heat, %.0f kW emitted, %.1f m2; seen at %.0f km (heat %.0f, emission %.0f, radar %.0f)"),
		Hot.ThermalKw, Hot.EmissionKw, Hot.CrossSectionM2, Hot.DetectionRangeKm(), Hot.ThermalRangeKm, Hot.EmissionRangeKm, Hot.RadarRangeKm));
	AddInfo(FString::Printf(TEXT("cold and quiet: %.0f kW heat, %.0f kW emitted, %.1f m2; seen at %.0f km (heat %.0f, emission %.0f, radar %.0f)"),
		Cold.ThermalKw, Cold.EmissionKw, Cold.CrossSectionM2, Cold.DetectionRangeKm(), Cold.ThermalRangeKm, Cold.EmissionRangeKm, Cold.RadarRangeKm));
	TestTrue(TEXT("running cold and quiet shortens the range it is seen at"), Cold.DetectionRangeKm() < 0.7 * Hot.DetectionRangeKm());
	TestTrue(TEXT("and every part of it falls"), Cold.ThermalKw < Hot.ThermalKw && Cold.EmissionKw < Hot.EmissionKw && Cold.CrossSectionM2 < Hot.CrossSectionM2);
	return true;
}

#endif
