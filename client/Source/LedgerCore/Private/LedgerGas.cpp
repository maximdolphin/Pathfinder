#include "LedgerGas.h"

#include "LedgerEphemeris.h"
#include "LedgerMath.h"

namespace
{
	constexpr double Boltzmann = 1.380649e-23;
	constexpr double AtomicMassUnit = 1.66053907e-27;

	/// Hydrogen and helium in the proportions a giant forms with: about 86%
	/// H2 and 14% He by number, which averages a shade over two.
	constexpr double HydrogenHeliumMassKg = 2.3 * AtomicMassUnit;

	/// Nitrogen, for anything rocky enough to have lost the light gases.
	constexpr double NitrogenMassKg = 28.0 * AtomicMassUnit;
}

namespace LedgerGas
{
	double MolecularMassKg(const FLedgerBody& Body)
	{
		return Body.Kind == ELedgerBodyKind::GasGiant
			? HydrogenHeliumMassKg : NitrogenMassKg;
	}

	double GravityAt(const FLedgerBody& Body, double DepthMetres)
	{
		if (!(Body.MassKg > 0.0) || !(Body.RadiusMetres > 0.0))
		{
			return 0.0;
		}
		// Below the datum the radius shrinks, so gravity would rise -- except
		// that the mass enclosed shrinks too, and for a body this centrally
		// condensed the two roughly cancel over the first few hundred
		// kilometres. Held constant at the datum's value, which is right to a
		// per cent over the range anything survives and wrong in a way that
		// would need an interior model to fix.
		const double Radius = Body.RadiusMetres;
		return LedgerEphemeris::GravitationalConstant * Body.MassKg / (Radius * Radius);
	}

	double ScaleHeightMetres(const FLedgerBody& Body, double TemperatureKelvin)
	{
		const double Gravity = GravityAt(Body, 0.0);
		const double Mass = MolecularMassKg(Body);
		if (!(Gravity > 0.0) || !(Mass > 0.0) || !(TemperatureKelvin > 0.0))
		{
			return 0.0;
		}
		return Boltzmann * TemperatureKelvin / (Mass * Gravity);
	}

	double PressurePascals(
		const FLedgerBody& Body, double TemperatureKelvin, double DepthMetres)
	{
		const double Height = ScaleHeightMetres(Body, TemperatureKelvin);
		if (!(Height > 0.0))
		{
			return 0.0;
		}
		// Positive depth is downwards, so pressure grows with it.
		return OneBarPascals * FMath::Exp(DepthMetres / Height);
	}

	double DepthForPressure(
		const FLedgerBody& Body, double TemperatureKelvin, double Pascals)
	{
		const double Height = ScaleHeightMetres(Body, TemperatureKelvin);
		if (!(Height > 0.0) || !(Pascals > 0.0))
		{
			return 0.0;
		}
		return Height * FMath::Loge(Pascals / OneBarPascals);
	}
}
