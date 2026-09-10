// What a body's air is made of, and everything that follows from it. T089.
//
// **Earth's numbers become one row rather than the only row.** The renderer had
// a scattering coefficient of 0.0331 and a scale height of 8 km written into it,
// which is exactly right for one planet and silently wrong for every other. This
// is the row Earth occupies, and the arithmetic that generates the others.
//
// Almost all of it is derivable. Scale height is kT/mg, lapse rate is g/cp, and
// the Rayleigh coefficient falls out of how much the gas bends light and how
// much of it there is per cubic metre -- which is why a carbon-dioxide sky is a
// different colour from a nitrogen one for a reason rather than by decoration.
//
// The exception is surface pressure, and it is worth being blunt about: it is
// not a function of mass and radius. Mars and Titan are nearly the same size and
// differ by a factor of two hundred, because pressure records a history of
// outgassing and loss rather than a fact of geometry. So it is generated, and
// only bounded by the physics -- a body that cannot hold a gas does not get one.

#pragma once

#include "CoreMinimal.h"

#include "LedgerBody.h"

/// What the air is mostly made of.
///
/// **Free oxygen is a biosignature, not a size.** Nothing about a planet's mass
/// or distance produces an oxygen atmosphere; on Earth it is the output of three
/// billion years of photosynthesis and it would be gone in a few million years
/// without it. So the home world has oxygen because the game assumes people
/// breathe there, and no other generated body gets it. That is an honest
/// dependency on a fact this project has not modelled rather than a rule
/// dressed up as one.
enum class ELedgerAir : uint8
{
	None,
	NitrogenOxygen,
	CarbonDioxide,
	Nitrogen,
	HydrogenHelium,
};

LEDGERCORE_API const TCHAR* LexToString(ELedgerAir Air);

/// One body's atmosphere, as the numbers everything downstream needs.
struct FLedgerAirProfile
{
	ELedgerAir Composition = ELedgerAir::None;

	/// Pressure at the datum, pascals. 101325 is one Earth atmosphere.
	double SurfacePressurePascals = 0.0;

	/// Temperature at the datum, kelvin.
	double SurfaceTemperatureKelvin = 0.0;

	/// kT/mg: the height over which density falls by e.
	double ScaleHeightMetres = 0.0;

	/// g/cp, kelvin per metre. The dry adiabatic rate -- how fast it cools with
	/// height when nothing is condensing.
	double LapseRateKelvinPerMetre = 0.0;

	/// Mean molecular mass, kg.
	double MolecularMassKg = 0.0;

	/// Molecules per cubic metre at the datum.
	double NumberDensityPerCubicMetre = 0.0;

	/// Rayleigh scattering coefficient at the datum, per metre, at 440, 550 and
	/// 680 nm. The blue-sky number: Earth's is about 3.3e-5 at 440 nm.
	FVector3d RayleighPerMetre = FVector3d::ZeroVector;

	/// Aerosol scattering at the datum, per metre, and the height it falls off
	/// over. Dust and haze, which do not scale with the gas.
	double MiePerMetre = 0.0;
	double MieScaleHeightMetres = 0.0;

	/// Ozone, or whatever else absorbs where the gas does not. Zero unless
	/// there is oxygen to make it out of.
	double OzoneAbsorptionPerMetre = 0.0;

	/// Where the air effectively stops, metres. Twelve scale heights, where a
	/// millionth of the surface density is left.
	double TopMetres = 0.0;

	/// Whether anything condenses into a cloud deck here, and where.
	bool bHasClouds = false;
	double CloudBaseMetres = 0.0;
	double CloudTopMetres = 0.0;

	/// How much of the sky the deck covers, 0 to 1. Not every world is overcast,
	/// and a deck that always is hides everything above it.
	double CloudCoverage = 0.0;

	bool HasAir() const { return Composition != ELedgerAir::None; }
};

namespace LedgerAir
{
	/// The profile for a body in a system, at a time.
	///
	/// Deterministic from the system's seed and the body's index, so two
	/// processes describe the same sky.
	LEDGERCORE_API FLedgerAirProfile For(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch);

	/// The profile for numbers given directly, so the real planets can be run
	/// through the same arithmetic as the generated ones.
	LEDGERCORE_API FLedgerAirProfile Describe(
		ELedgerAir Composition, double SurfacePressurePascals,
		double SurfaceTemperatureKelvin, double SurfaceGravity,
		double PlanetRadiusMetres);

	/// Rayleigh scattering at one wavelength, per metre.
	///
	/// beta = 8 pi^3 (n^2 - 1)^2 F / (3 N lambda^4), with the refractivity taken
	/// at the density it is being evaluated at. Everything about the gas enters
	/// through how much it bends light and how anisotropic its molecules are;
	/// everything about the planet enters through N.
	LEDGERCORE_API double RayleighPerMetre(
		ELedgerAir Composition, double NumberDensityPerCubicMetre,
		double WavelengthMetres);

	/// How much warmer the ground is than the sunlight alone would make it.
	///
	/// **The greenhouse effect, as the grey-atmosphere result rather than as a
	/// fudge:** T_surface = T_equilibrium (1 + 3 tau / 4)^(1/4), with the
	/// infrared optical depth tau proportional to how much gas there is and how
	/// hard that gas absorbs. It reproduces Earth (255 K becomes 288) and Venus
	/// (232 becomes 737) from the same two lines, which is the test worth
	/// passing -- Venus is 500 K of greenhouse and any model that gets Earth
	/// right by tuning gets Venus wrong by a factor of three.
	LEDGERCORE_API double SurfaceTemperatureKelvin(
		ELedgerAir Composition, double SurfacePressurePascals,
		double EquilibriumKelvin);

	/// Infrared optical depth per bar, which is what makes one gas a stronger
	/// blanket than another.
	LEDGERCORE_API double GreenhouseDepthPerBar(ELedgerAir Composition);

	/// Specific heat at constant pressure, J/(kg K). The lapse rate's other half.
	LEDGERCORE_API double SpecificHeat(ELedgerAir Composition);

	/// Refractivity (n - 1) at standard temperature and pressure.
	LEDGERCORE_API double Refractivity(ELedgerAir Composition);

	/// The King correction factor for molecular anisotropy: how much more a
	/// non-spherical molecule scatters than the simple derivation says.
	LEDGERCORE_API double KingFactor(ELedgerAir Composition);
}
