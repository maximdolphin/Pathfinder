#include "LedgerAir.h"

#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "LedgerSky.h"

namespace
{
	// Prefixed with the file's name, because this module compiles as one
	// translation unit and a bare `AirBoltzmann` collides with everything.
	constexpr double AirBoltzmann = 1.380649e-23;
	constexpr double AirAtomicMassUnit = 1.66053907e-27;

	/// Loschmidt's number: molecules per cubic metre of an ideal gas at 0 C and
	/// one atmosphere, which is the density refractivity is quoted at.
	constexpr double AirLoschmidt = 2.6867811e25;

	/// The three wavelengths the renderer wants, metres.
	constexpr double AirBlue = 440.0e-9;
	constexpr double AirGreen = 550.0e-9;
	constexpr double AirRed = 680.0e-9;

	double AirRandom(uint32 Seed, int32 Index, int32 Stream)
	{
		uint64 X = static_cast<uint64>(Seed) * 0x9E3779B97F4A7C15ull
			^ static_cast<uint64>(static_cast<uint32>(Index)) * 0xBF58476D1CE4E5B9ull
			^ static_cast<uint64>(static_cast<uint32>(Stream)) * 0x94D049BB133111EBull;
		X ^= X >> 30; X *= 0xBF58476D1CE4E5B9ull;
		X ^= X >> 27; X *= 0x94D049BB133111EBull;
		X ^= X >> 31;
		return static_cast<double>(X >> 11) / 9007199254740992.0;
	}

	double AirBetween(uint32 Seed, int32 Index, int32 Stream, double Low, double High)
	{
		return Low + (High - Low) * AirRandom(Seed, Index, Stream);
	}
}

const TCHAR* LexToString(ELedgerAir Air)
{
	switch (Air)
	{
	case ELedgerAir::NitrogenOxygen: return TEXT("nitrogen-oxygen");
	case ELedgerAir::CarbonDioxide:  return TEXT("carbon dioxide");
	case ELedgerAir::Nitrogen:       return TEXT("nitrogen");
	case ELedgerAir::HydrogenHelium: return TEXT("hydrogen-helium");
	default:                         return TEXT("none");
	}
}

namespace LedgerAir
{
	double Refractivity(ELedgerAir Composition)
	{
		// (n - 1) at 0 C and one atmosphere, near 550 nm. These are measured
		// numbers about gases, not choices: dry air 2.78e-4, carbon dioxide
		// bends light appreciably more, hydrogen and helium appreciably less.
		switch (Composition)
		{
		case ELedgerAir::NitrogenOxygen: return 2.78e-4;
		case ELedgerAir::CarbonDioxide:  return 4.49e-4;
		case ELedgerAir::Nitrogen:       return 2.98e-4;
		case ELedgerAir::HydrogenHelium: return 1.32e-4;
		default:                         return 0.0;
		}
	}

	double KingFactor(ELedgerAir Composition)
	{
		// Molecules are not spheres, and the ones that are not scatter more than
		// the textbook derivation says. Carbon dioxide is the worst offender at
		// 1.15; helium, being a single atom, is exactly 1.
		switch (Composition)
		{
		case ELedgerAir::NitrogenOxygen: return 1.048;
		case ELedgerAir::CarbonDioxide:  return 1.150;
		case ELedgerAir::Nitrogen:       return 1.034;
		case ELedgerAir::HydrogenHelium: return 1.015;
		default:                         return 1.0;
		}
	}

	double GreenhouseDepthPerBar(ELedgerAir Composition)
	{
		// Fitted to the three atmospheres that have both numbers published.
		// Earth needs 0.84 at one bar to turn 255 K into 288; Venus needs 1.36
		// per bar to turn 232 into 737 at ninety-two; Titan 0.67 to turn 82 into
		// 94 at one and a half. Carbon dioxide really is the better blanket, and
		// Venus is hot because there is ninety-two bars of it rather than
		// because it is nearer the sun.
		switch (Composition)
		{
		case ELedgerAir::NitrogenOxygen: return 0.84;
		case ELedgerAir::CarbonDioxide:  return 1.36;
		case ELedgerAir::Nitrogen:       return 0.67;
		// A gas giant's warmth is mostly its own -- Jupiter radiates more than
		// it receives -- and that is a different model. One bar of hydrogen is
		// treated as a modest blanket and the internal heat is not claimed.
		case ELedgerAir::HydrogenHelium: return 1.00;
		default:                         return 0.0;
		}
	}

	double SurfaceTemperatureKelvin(
		ELedgerAir Composition, double Pressure, double Equilibrium)
	{
		if (Composition == ELedgerAir::None || !(Pressure > 0.0))
		{
			return Equilibrium;
		}
		const double Tau = GreenhouseDepthPerBar(Composition) * (Pressure / 101325.0);
		return Equilibrium * FMath::Pow(1.0 + 0.75 * Tau, 0.25);
	}

	double SpecificHeat(ELedgerAir Composition)
	{
		// J/(kg K), constant pressure. Hydrogen is enormous because the
		// molecules are light -- the same energy per molecule buys far more
		// energy per kilogramme -- which is why a gas giant's lapse rate is
		// gentle despite its gravity.
		switch (Composition)
		{
		case ELedgerAir::NitrogenOxygen: return 1005.0;
		case ELedgerAir::CarbonDioxide:  return 846.0;
		case ELedgerAir::Nitrogen:       return 1040.0;
		case ELedgerAir::HydrogenHelium: return 11000.0;
		default:                         return 1005.0;
		}
	}

	double MolecularMassKg(ELedgerAir Composition)
	{
		switch (Composition)
		{
		case ELedgerAir::NitrogenOxygen: return 28.97 * AirAtomicMassUnit;
		case ELedgerAir::CarbonDioxide:  return 43.34 * AirAtomicMassUnit;
		case ELedgerAir::Nitrogen:       return 28.01 * AirAtomicMassUnit;
		case ELedgerAir::HydrogenHelium: return 2.30 * AirAtomicMassUnit;
		default:                         return 28.97 * AirAtomicMassUnit;
		}
	}

	double RayleighPerMetre(
		ELedgerAir Composition, double Density, double Wavelength)
	{
		if (!(Density > 0.0) || !(Wavelength > 0.0))
		{
			return 0.0;
		}

		// **Refractivity is quoted at Loschmidt's density and scales with it.**
		// Miss that and the whole thing is off by whatever ratio the planet's
		// surface density happens to bear to Earth's, which is the sort of error
		// that looks like a taste question about how blue the sky should be.
		const double N = Refractivity(Composition) * (Density / AirLoschmidt);
		const double Squared = (2.0 * N) * (2.0 * N);   // (n^2 - 1) ~= 2(n - 1)

		const double Lambda4 = Wavelength * Wavelength * Wavelength * Wavelength;
		return 8.0 * LedgerPi * LedgerPi * LedgerPi * Squared
			* KingFactor(Composition) / (3.0 * Density * Lambda4);
	}

	FLedgerAirProfile Describe(
		ELedgerAir Composition, double Pressure, double Temperature,
		double Gravity, double PlanetRadiusMetres)
	{
		FLedgerAirProfile Out;
		Out.Composition = Composition;
		if (Composition == ELedgerAir::None || !(Pressure > 0.0)
			|| !(Temperature > 0.0) || !(Gravity > 0.0))
		{
			Out.Composition = ELedgerAir::None;
			return Out;
		}

		Out.SurfacePressurePascals = Pressure;
		Out.SurfaceTemperatureKelvin = Temperature;
		Out.MolecularMassKg = MolecularMassKg(Composition);
		Out.NumberDensityPerCubicMetre = Pressure / (AirBoltzmann * Temperature);
		Out.ScaleHeightMetres =
			AirBoltzmann * Temperature / (Out.MolecularMassKg * Gravity);
		Out.LapseRateKelvinPerMetre = Gravity / SpecificHeat(Composition);

		Out.RayleighPerMetre = FVector3d(
			RayleighPerMetre(Composition, Out.NumberDensityPerCubicMetre, AirBlue),
			RayleighPerMetre(Composition, Out.NumberDensityPerCubicMetre, AirGreen),
			RayleighPerMetre(Composition, Out.NumberDensityPerCubicMetre, AirRed));

		// Aerosols are not the gas. They sit low -- a fifth of the gas's scale
		// height is the usual figure for haze -- and how much there is depends
		// on whether there is anything to lift: dust needs a dry surface and
		// wind, which a gas giant has no surface for.
		Out.MieScaleHeightMetres = Out.ScaleHeightMetres * 0.15;
		Out.MiePerMetre = Composition == ELedgerAir::HydrogenHelium
			? 4.0e-6
			: 2.1e-5 * (Pressure / 101325.0);

		// **Ozone exists because oxygen does.** No oxygen, no ozone layer, and
		// no violet twilight band -- which is a visible difference between a
		// breathable sky and a carbon-dioxide one rather than a setting.
		Out.OzoneAbsorptionPerMetre =
			Composition == ELedgerAir::NitrogenOxygen ? 1.881e-6 : 0.0;

		// Twelve scale heights: a millionth of the surface density, which is
		// where the renderer can stop without a visible edge.
		Out.TopMetres = Out.ScaleHeightMetres * 12.0;

		// Something has to condense. Water needs a temperature it can be liquid
		// at; a carbon-dioxide world that cold gets a thin dry haze instead of a
		// deck, and a gas giant's ammonia deck is high and thick.
		if (Composition == ELedgerAir::HydrogenHelium)
		{
			Out.bHasClouds = true;
			Out.CloudBaseMetres = Out.ScaleHeightMetres * 1.0;
			Out.CloudTopMetres = Out.ScaleHeightMetres * 3.0;
			Out.CloudCoverage = 0.85;
		}
		else if (Temperature > 250.0 && Temperature < 330.0
			&& Composition != ELedgerAir::CarbonDioxide)
		{
			Out.bHasClouds = true;
			// The lifting condensation level: how far you must rise for the
			// temperature to fall to where the water already present condenses.
			// A dry world's is high and a humid world's is low, and using the
			// lapse rate for it means the deck moves when the climate does.
			Out.CloudBaseMetres = 12.0 / Out.LapseRateKelvinPerMetre;
			Out.CloudTopMetres = Out.CloudBaseMetres + Out.ScaleHeightMetres * 0.5;
			Out.CloudCoverage = 0.55;
		}
		else
		{
			Out.bHasClouds = false;
			Out.CloudCoverage = 0.0;
		}

		// A deck cannot start inside the ground, however the arithmetic came out.
		Out.CloudBaseMetres = FMath::Min(
			Out.CloudBaseMetres, PlanetRadiusMetres * 0.02);
		Out.CloudTopMetres = FMath::Max(
			Out.CloudTopMetres, Out.CloudBaseMetres * 1.2);
		return Out;
	}

	FLedgerAirProfile For(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch)
	{
		FLedgerAirProfile Out;
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return Out;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];

		const double Gravity = LedgerEphemeris::GravitationalConstant * Body.MassKg
			/ (Body.RadiusMetres * Body.RadiusMetres);
		const double Temperature =
			LedgerSky::EquilibriumTemperatureKelvin(System, BodyIndex, SecondsFromEpoch);

		if (Body.Kind == ELedgerBodyKind::GasGiant)
		{
			// There is no surface, so the datum is where the pressure is one bar
			// -- the convention every gas giant's numbers are quoted at, and the
			// one T079 already built the descent around.
			return Describe(ELedgerAir::HydrogenHelium, 101325.0,
				SurfaceTemperatureKelvin(
					ELedgerAir::HydrogenHelium, 101325.0, Temperature),
				Gravity, Body.RadiusMetres);
		}

		if (!LedgerSky::RetainsAtmosphere(System, BodyIndex, SecondsFromEpoch))
		{
			return Out;
		}

		// Oxygen only where the game says people live. See the note on ELedgerAir.
		const ELedgerAir Composition = BodyIndex == LedgerBodies::HomeIndex(System)
			? ELedgerAir::NitrogenOxygen
			: (Temperature < 200.0 ? ELedgerAir::Nitrogen : ELedgerAir::CarbonDioxide);

		// **The one number that is generated rather than derived.** Mars and
		// Titan are within a few per cent of each other in radius and differ by
		// two hundred times in pressure, so there is no function of mass and
		// temperature that produces both. What physics does say is that a body
		// which barely holds its air has less of it, so the range is tied to
		// the retention margin from T078 and left wide.
		const double Escape = LedgerSky::EscapeVelocity(Body);
		const double Thermal = FMath::Sqrt(
			3.0 * AirBoltzmann * Temperature / MolecularMassKg(Composition));
		const double Margin = FMath::Clamp((Escape / Thermal - 8.0) / 20.0, 0.0, 1.0);

		double Pressure = 101325.0
			* FMath::Pow(10.0, AirBetween(System.Seed, BodyIndex, 91, -2.2, 0.5))
			* (0.15 + 0.85 * Margin);

		if (Composition == ELedgerAir::NitrogenOxygen)
		{
			// **The home world is near a bar because people breathe there**, the
			// same admission as the oxygen. Two hundred millibars of anything is
			// a spacesuit world, and the game has already decided this one is
			// not. It is still generated, just inside a band a person survives.
			Pressure = 101325.0 * AirBetween(System.Seed, BodyIndex, 92, 0.8, 1.3);
		}

		// The blanket goes on before the thermometer is read: pressure decides
		// the greenhouse, and the greenhouse decides the temperature that
		// everything else -- scale height, cloud base, the lot -- is built from.
		const double Surface =
			SurfaceTemperatureKelvin(Composition, Pressure, Temperature);

		return Describe(Composition, Pressure, Surface, Gravity,
			Body.RadiusMetres);
	}
}
