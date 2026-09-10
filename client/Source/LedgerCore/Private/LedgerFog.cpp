#include "LedgerFog.h"

#include "LedgerMath.h"

namespace
{
	constexpr double FogStefanBoltzmann = 5.670374e-8;

	/// Eddy diffusivity on a calm night, m^2/s. A still nocturnal boundary layer
	/// is a famously bad mixer -- this is four orders of magnitude below a sunny
	/// afternoon's -- and that is why the cold pool stays thin enough to have a
	/// visible top.
	constexpr double FogEddyDiffusivity = 0.11;

}

namespace LedgerFog
{
	double DewPointDepressionKelvin()
	{
		return 12.0;
	}

	double ThermalInertia(const FLedgerAirProfile& Air)
	{
		// sqrt(k rho c), J m^-2 K^-1 s^-1/2. **Water is what makes the
		// difference**: damp soil conducts heat several times better than dry
		// dust and holds far more of it, so a wet world's ground is a reservoir
		// and a dry world's is a blanket over one. Mars's dust is around 200 to
		// 400 and that -- not its thin air -- is most of why it swings ninety
		// kelvin between noon and dawn.
		return Air.bHasClouds && Air.Composition == ELedgerAir::NitrogenOxygen
			? 2000.0
			: 350.0;
	}

	double InversionDepthMetres(double SecondsInDarkness)
	{
		if (!(SecondsInDarkness > 0.0))
		{
			return 0.0;
		}
		return FMath::Sqrt(2.0 * FogEddyDiffusivity * SecondsInDarkness);
	}

	double NightCoolingKelvin(
		const FLedgerAirProfile& Air, double SecondsInDarkness)
	{
		const double Depth = InversionDepthMetres(SecondsInDarkness);
		if (!Air.HasAir() || !(Depth > 0.0))
		{
			return 0.0;
		}

		// **The sky radiates back, and how much is the greenhouse again.** A
		// night under a thick blanket does not get cold; a night under a thin
		// one does, which is why a desert freezes and why Mars swings ninety
		// kelvin. Reusing the optical depth from T089 means the blanket that
		// made the ground warm is the same blanket that stops it cooling,
		// rather than two numbers that could disagree.
		const double Emissivity = 1.0 - FMath::Exp(
			-LedgerAir::GreenhouseDepthPerBar(Air.Composition)
			* (Air.SurfacePressurePascals / 101325.0));

		const double Net = FogStefanBoltzmann
			* FMath::Pow(Air.SurfaceTemperatureKelvin, 4.0)
			* (1.0 - Emissivity);

		// **What sets the brake is the ground, not the air.**
		//
		// The first version took the heat out of the cold pool's own capacity
		// and said a clear night cools by forty kelvin -- and by fifteen in the
		// first hour, which no night does. The pool is a hundred metres of air
		// weighing a hundred kilogrammes a square metre; the soil under it is
		// an effectively infinite reservoir feeding heat upwards by conduction,
		// and that is what actually limits the drop.
		//
		// The standard result for a semi-infinite solid losing a constant flux
		// is dT = 2 F sqrt(t) / (I sqrt(pi)), where I = sqrt(k rho c) is the
		// thermal inertia. It says a clear night on Earth cools by eighteen
		// kelvin, which is a clear night on Earth.
		const double Inertia = ThermalInertia(Air);
		if (!(Inertia > 0.0))
		{
			return 0.0;
		}
		return 2.0 * Net * FMath::Sqrt(SecondsInDarkness)
			/ (Inertia * FMath::Sqrt(LedgerPi));
	}

	FLedgerFog At(
		const FLedgerAirProfile& Air, double SecondsInDarkness,
		double SolarAltitudeRadians)
	{
		FLedgerFog Out;
		if (!Air.HasAir())
		{
			return Out;
		}

		// **Fog needs something to condense.** A carbon-dioxide world at 400 K
		// and a hydrogen giant have no water, and a body whose air holds no
		// cloud deck holds no fog either -- one rule, from T089, deciding both.
		if (!Air.bHasClouds || Air.Composition != ELedgerAir::NitrogenOxygen)
		{
			return Out;
		}

		const double Cooling = NightCoolingKelvin(Air, SecondsInDarkness);
		if (Cooling < DewPointDepressionKelvin())
		{
			return Out;
		}

		Out.bForms = true;
		Out.DepthMetres = InversionDepthMetres(SecondsInDarkness);

		// How far past the dew point it went decides how thick it is. One kelvin
		// of overshoot is mist; five is the sort of fog that closes an airport.
		const double Overshoot = Cooling - DewPointDepressionKelvin();
		Out.ExtinctionPerMetre = FMath::Clamp(Overshoot * 0.004, 0.0005, 0.05);

		// **And the sun burns it off.** Not instantly: the ground has to warm
		// before the pool does, so it survives the first few degrees of
		// altitude and is gone by ten. Sunrise is where a fog capture has to be
		// taken, and this is why.
		const double Degrees = FMath::RadiansToDegrees(SolarAltitudeRadians);
		Out.Fraction = Degrees <= 0.0
			? 1.0
			: FMath::Clamp(1.0 - (Degrees - 2.0) / 8.0, 0.0, 1.0);
		Out.bForms = Out.Fraction > 0.0;
		return Out;
	}
}
