// A steep entry burns and a shallow one does not. T100.
//
// Both start at 120 km at 7.8 km/s through the same air, through the same
// flight model the ship flies; only the angle differs. Both pass through every
// altitude on the way down, so a burn triggered by altitude would burn both or
// neither. What differs is how fast the density-velocity integral arrives: the
// steep one meets thick air still fast, the skin takes heat faster than it can
// radiate it, and it goes over the limit; the shallow one sheds its speed high
// up, takes more heat in total, and radiates it as it comes.

#include "LedgerFlightModel.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FEntry
	{
		FLedgerHeatState Heat;
		double PeakFluxAltitudeKm = 0.0;
		double LowestKm = 0.0;
		double Seconds = 0.0;
	};

	FEntry Enter(double GammaDegrees)
	{
		FLedgerGravityField Field;
		Field.Radius = 637100000.0;
		Field.SurfaceGravity = 981.0;
		Field.DragScaleHeight = 800000.0;
		Field.AtmosphericDrag = 0.55;
		Field.BallisticKgPerM2 = 300.0;
		Field.SeaLevelDensity = 1.225;

		FLedgerFlightState State;
		State.Position = FVector3d(0.0, 0.0, Field.Radius + 120000.0 * 100.0);
		const double Gamma = FMath::DegreesToRadians(GammaDegrees);
		State.Velocity = FVector3d(FMath::Cos(Gamma), 0.0, FMath::Sin(Gamma)) * 780000.0;

		const FLedgerHeatShield Shield;
		FEntry Out;
		for (double Seconds = 0.0; Seconds < 3000.0; Seconds += LedgerFlight::FixedStep)
		{
			LedgerFlight::Integrate(State, Field, LedgerFlight::FixedStep);
			const double Km = LedgerFlight::AltitudeAbove(State, Field) / 100000.0;
			const double Before = Out.Heat.PeakFluxWattsPerM2;
			LedgerFlight::Heat(Out.Heat, Shield, 1.225 * FMath::Exp(-Km / 8.0),
				State.Velocity.Length() / 100.0, 250.0, LedgerFlight::FixedStep);
			if (Out.Heat.PeakFluxWattsPerM2 > Before)
			{
				Out.PeakFluxAltitudeKm = Km;
			}
			Out.LowestKm = Km;
			Out.Seconds = Seconds;
			if (Km < 10.0)
			{
				break;
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerEntryHeat,
	"Ledger.Flight.ASteepEntryBurnsAndAShallowOneDoesNot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerEntryHeat::RunTest(const FString&)
{
	const FEntry Steep = Enter(-8.0);
	const FEntry Shallow = Enter(-1.0);
	for (const FEntry* Entry : { &Steep, &Shallow })
	{
		AddInfo(FString::Printf(
			TEXT("%s: peak %.2f MW/m2 at %.1f km, skin %.0f K, took in %.0f MJ/m2 and radiated %.0f, hull lost %.2f, %.0f s to 10 km"),
			Entry == &Steep ? TEXT("steep -8") : TEXT("shallow -1"),
			Entry->Heat.PeakFluxWattsPerM2 / 1.0e6, Entry->PeakFluxAltitudeKm, Entry->Heat.PeakKelvin,
			Entry->Heat.LoadJoulesPerM2 / 1.0e6, Entry->Heat.RadiatedJoulesPerM2 / 1.0e6,
			Entry->Heat.Damage, Entry->Seconds));
	}

	TestTrue(TEXT("both come all the way down, through every altitude"),
		Steep.LowestKm < 10.0 && Shallow.LowestKm < 10.0);
	TestTrue(TEXT("the steep entry burns"), Steep.Heat.Damage > 0.0);
	TestTrue(TEXT("the shallow entry does not"), Shallow.Heat.Damage == 0.0);
	TestTrue(TEXT("the steep one meets a higher flux"),
		Steep.Heat.PeakFluxWattsPerM2 > Shallow.Heat.PeakFluxWattsPerM2);
	TestTrue(TEXT("though the shallow one takes in more heat in total"),
		Shallow.Heat.LoadJoulesPerM2 > Steep.Heat.LoadJoulesPerM2);

	// The skin holds what came in less what went out, and nothing else.
	const FLedgerHeatShield Shield;
	const double Held = Shield.HeatCapacity * (Shallow.Heat.SkinKelvin - 250.0);
	const double Balance = Shallow.Heat.LoadJoulesPerM2 - Shallow.Heat.RadiatedJoulesPerM2;
	TestTrue(TEXT("the skin's heat is the integral in less the integral out"),
		FMath::Abs(Held - Balance) <= 1.0e-6 * FMath::Max(Shallow.Heat.LoadJoulesPerM2, 1.0));
	return true;
}

#endif
