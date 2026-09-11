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

	FVector3d AerosolAlbedo(ELedgerAir Composition)
	{
		// Single-scattering albedo at 440, 550 and 680 nm: of the light these
		// particles intercept, how much carries on rather than being absorbed.
		//
		// **This is the whole reason a Martian sky is not blue.** A thin
		// carbon-dioxide atmosphere on its own scatters like any gas and would
		// give a dark blue sky; what makes it butterscotch is a permanent load
		// of dust about a tenth iron oxide, which is nearly opaque in the blue
		// and nearly clear in the red. The numbers are the ones retrieved from
		// Viking and Pathfinder sky brightness.
		switch (Composition)
		{
		// Water and sulphate droplets are nearly clear at every visible
		// wavelength, which is why haze on Earth is white rather than coloured.
		case ELedgerAir::NitrogenOxygen: return FVector3d(0.99, 0.99, 0.98);
		// Iron oxide.
		case ELedgerAir::CarbonDioxide:  return FVector3d(0.63, 0.87, 0.94);
		// Tholins: organic haze, and an even harder blue absorber. Titan's
		// orange is this rather than its nitrogen.
		case ELedgerAir::Nitrogen:       return FVector3d(0.50, 0.72, 0.94);
		// Ammonia ice, bright and close to neutral.
		case ELedgerAir::HydrogenHelium: return FVector3d(0.97, 0.98, 0.99);
		default:                         return FVector3d(1.0, 1.0, 1.0);
		}
	}

	double AerosolAnisotropy(ELedgerAir Composition)
	{
		// Bigger particles throw more of the light onwards. Dust and ice are a
		// micron or two across and strongly forward-peaked; the finer organic
		// haze less so.
		switch (Composition)
		{
		case ELedgerAir::NitrogenOxygen: return 0.80;
		case ELedgerAir::CarbonDioxide:  return 0.75;
		case ELedgerAir::Nitrogen:       return 0.65;
		case ELedgerAir::HydrogenHelium: return 0.85;
		default:                         return 0.80;
		}
	}

	// ponytail: one radius per composition, from the missions that measured
	// them; a size distribution when a sky needs more than one peak.
	static double AerosolRadius(ELedgerAir Composition)
	{
		switch (Composition)
		{
		// Sulphate and sea-salt haze, a few tenths of a micron.
		case ELedgerAir::NitrogenOxygen: return 0.3e-6;
		// Martian dust: 1.5 microns effective (Pathfinder, the rovers' sky
		// brightness).
		case ELedgerAir::CarbonDioxide:  return 1.5e-6;
		// Titan's tholin aggregates, small in the way that matters here.
		case ELedgerAir::Nitrogen:       return 0.5e-6;
		// Ammonia ice.
		case ELedgerAir::HydrogenHelium: return 1.0e-6;
		default:                         return 0.0;
		}
	}

	static const FVector3d AureoleWavelengths(440.0e-9, 550.0e-9, 680.0e-9);

	FVector3d AureoleWidthRadians(const FLedgerAirProfile& Air)
	{
		FVector3d Out(1.0, 1.0, 1.0);
		for (int32 Channel = 0; Channel < 3 && Air.AerosolRadiusMetres > 0.0; ++Channel)
		{
			const double SizeParameter = 2.0 * UE_DOUBLE_PI * Air.AerosolRadiusMetres / AureoleWavelengths[Channel];
			Out[Channel] = 2.0 / SizeParameter;
		}
		return Out;
	}

	FVector3d AureoleFraction(const FLedgerAirProfile& Air)
	{
		FVector3d Out = FVector3d::ZeroVector;
		if (!Air.HasAir() || Air.AerosolRadiusMetres <= 0.0 || Air.MiePerMetre <= 0.0)
		{
			return Out;
		}
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			const double SizeParameter = 2.0 * UE_DOUBLE_PI * Air.AerosolRadiusMetres / AureoleWavelengths[Channel];
			const double Albedo = FMath::Max(Air.MieScatteringPerMetre[Channel] / Air.MiePerMetre, 0.05);
			const double Diffracted = FMath::Min(1.0, 0.5 / Albedo) * FMath::SmoothStep(4.0, 10.0, SizeParameter);
			// Of what the sky scatters, the aerosol's share: each over its own column.
			const double Aerosol = Air.MieScatteringPerMetre[Channel] * Air.MieScaleHeightMetres;
			const double Gas = Air.RayleighPerMetre[Channel] * Air.ScaleHeightMetres;
			Out[Channel] = Aerosol + Gas > 0.0 ? Diffracted * Aerosol / (Aerosol + Gas) : 0.0;
		}
		return Out;
	}

	FVector3d SkyColour(const FLedgerAirProfile& Air, double AirMasses)
	{
		FVector3d Out = FVector3d::ZeroVector;
		if (!Air.HasAir() || !(AirMasses > 0.0))
		{
			return Out;
		}
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			// Each component over its own column: an exponential atmosphere's
			// vertical column is the surface coefficient times the scale height.
			const double Gas =
				Air.RayleighPerMetre[Channel] * Air.ScaleHeightMetres;
			const double DustScatter =
				Air.MieScatteringPerMetre[Channel] * Air.MieScaleHeightMetres;
			const double DustAbsorb =
				Air.MieAbsorptionPerMetre[Channel] * Air.MieScaleHeightMetres;
			const double Ozone =
				Air.OzoneAbsorptionPerMetre * Air.ScaleHeightMetres;

			const double Scatter = (Gas + DustScatter) * AirMasses;
			const double Total = (Gas + DustScatter + DustAbsorb + Ozone) * AirMasses;
			Out[Channel] = Total > 1e-12
				? Scatter * (1.0 - FMath::Exp(-Total)) / Total
				: Scatter;
		}
		const double Largest = FMath::Max3(Out.X, Out.Y, Out.Z);
		return Largest > 0.0 ? Out / Largest : Out;
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

	double DensityAt(const FLedgerAirProfile& Air, double AltitudeMetres)
	{
		if (!Air.HasAir() || !(Air.ScaleHeightMetres > 0.0))
		{
			return 0.0;
		}

		// Above the top there is nothing. The top is twelve scale heights, a
		// millionth of the surface density -- stopping there rather than
		// letting the exponential run on is what makes "in vacuum" a place
		// rather than a very quiet limit.
		if (AltitudeMetres >= Air.TopMetres)
		{
			return 0.0;
		}

		const double Surface =
			Air.NumberDensityPerCubicMetre * Air.MolecularMassKg;
		// T137: faded out over the last scale height rather than stopped, so
		// the air ends without a step -- at orbital speed the millionth left
		// at the top is still a push of kilonewtons on a belly, and a ship
		// coming in would feel it arrive all at once. A smoothstep: the density
		// and its slope both run on into nothing.
		const double Left = FMath::Clamp((Air.TopMetres - AltitudeMetres) / Air.ScaleHeightMetres, 0.0, 1.0);
		return Surface * FMath::Exp(-AltitudeMetres / Air.ScaleHeightMetres) * Left * Left * (3.0 - 2.0 * Left);
	}

	double DynamicPressure(
		const FLedgerAirProfile& Air, double AltitudeMetres, double Speed)
	{
		return 0.5 * DensityAt(Air, AltitudeMetres) * Speed * Speed;
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
		// **Haze and dust do not live at the same height, and that matters more
		// than how much of either there is.**
		//
		// Water haze is condensed out of a wet lower atmosphere and stays in the
		// bottom kilometre or so -- a seventh of the gas's scale height. Dust on
		// a dry world is lofted by wind and mixed through the whole column,
		// which is why Mars's dust scale height is its gas scale height and why
		// a dust storm there darkens the sky from the top down.
		const bool bDusty = Composition == ELedgerAir::CarbonDioxide
			|| Composition == ELedgerAir::Nitrogen;
		Out.MieScaleHeightMetres =
			Out.ScaleHeightMetres * (bDusty ? 1.0 : 0.15);

		Out.MiePerMetre = Composition == ELedgerAir::HydrogenHelium
			? 4.0e-6
			: 2.1e-5 * (Pressure / 101325.0);

		// **A thin atmosphere is not a clean one.** Mars carries a dust optical
		// depth of about half in six millibars while Earth's haze manages a
		// tenth in a thousand, because what suspends dust is wind and what
		// settles it is air resistance, and a thin atmosphere is bad at the
		// second. Scaling the aerosol with the pressure alone would have made
		// the dustiest sky in the solar system the clearest, so a dry world gets
		// a floor: half an optical depth over its own column.
		if (bDusty)
		{
			Out.MiePerMetre = FMath::Max(
				Out.MiePerMetre, 0.5 / Out.MieScaleHeightMetres);
		}

		// Extinction split into what carries on and what is gone.
		const FVector3d Albedo = AerosolAlbedo(Composition);
		Out.MieScatteringPerMetre = Albedo * Out.MiePerMetre;
		Out.MieAbsorptionPerMetre =
			(FVector3d(1.0, 1.0, 1.0) - Albedo) * Out.MiePerMetre;
		Out.MieAnisotropy = AerosolAnisotropy(Composition);
		Out.AerosolRadiusMetres = AerosolRadius(Composition);

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
