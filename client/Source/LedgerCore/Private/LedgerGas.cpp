#include "LedgerGas.h"

#include "LedgerEphemeris.h"
#include "LedgerMath.h"

namespace
{
	// Prefixed, because Unreal compiles this module as one translation unit and
	// a bare `GasBoltzmann` at file scope collides with every function-local one in
	// the blob. That has bitten this project before; the convention is that a
	// file-scope constant carries its file's name.
	constexpr double GasBoltzmann = 1.380649e-23;
	constexpr double GasAtomicMassUnit = 1.66053907e-27;

	/// Hydrogen and helium in the proportions a giant forms with: about 86%
	/// H2 and 14% He by number, which averages a shade over two.
	constexpr double GasHydrogenHeliumMassKg = 2.3 * GasAtomicMassUnit;

	/// Nitrogen, for anything rocky enough to have lost the light gases.
	constexpr double GasNitrogenMassKg = 28.0 * GasAtomicMassUnit;
}

namespace LedgerGas
{
	double MolecularMassKg(const FLedgerBody& Body)
	{
		return Body.Kind == ELedgerBodyKind::GasGiant
			? GasHydrogenHeliumMassKg : GasNitrogenMassKg;
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
		return GasBoltzmann * TemperatureKelvin / (Mass * Gravity);
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
