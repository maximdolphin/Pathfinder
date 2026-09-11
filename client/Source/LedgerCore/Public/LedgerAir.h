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

	/// Aerosol extinction at the datum, per metre, and the height it falls off
	/// over. Dust and haze, which do not scale with the gas.
	double MiePerMetre = 0.0;
	double MieScaleHeightMetres = 0.0;

	/// **What the dust does with the light it intercepts.** T090.
	///
	/// Split into the part that carries on in another direction and the part
	/// that is gone. This is where a carbon-dioxide sky gets its colour: the
	/// gas would give a dark blue one, and the iron oxide suspended in it eats
	/// the blue. Per channel, per metre, at 440, 550 and 680 nm.
	FVector3d MieScatteringPerMetre = FVector3d::ZeroVector;
	FVector3d MieAbsorptionPerMetre = FVector3d::ZeroVector;

	/// How forward-peaked the aerosol's scattering is, 0 to 1. Bigger particles
	/// throw more of the light onwards and less of it sideways.
	double MieAnisotropy = 0.8;

	/// The aerosol's effective radius, metres. What sets how narrow the
	/// diffraction peak around the sun is, channel by channel. T090.
	double AerosolRadiusMetres = 0.0;

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

	/// Mass density at an altitude above the datum, kg/m3.
	///
	/// **The number everything aerodynamic is actually asking for.** Drag,
	/// dynamic pressure, wind noise and terminal velocity are all this times a
	/// speed squared, and it was being open-coded from the number density, the
	/// molecular mass and the scale height wherever it was wanted. Isothermal,
	/// like the rest of the profile: exp(-h/H) and no more.
	///
	/// Zero on an airless body, at or above the top of the air, and below the
	/// datum it keeps rising -- a valley floor does hold denser air.
	LEDGERCORE_API double DensityAt(
		const FLedgerAirProfile& Air, double AltitudeMetres);

	/// Dynamic pressure, pascals: half rho v squared.
	///
	/// Kept next to the density because the pair of them is the whole of what
	/// "how hard is the air hitting this" means, and because a caller who has
	/// to write the half themselves will eventually write it twice.
	LEDGERCORE_API double DynamicPressure(
		const FLedgerAirProfile& Air, double AltitudeMetres,
		double SpeedMetresPerSecond);

	/// Rayleigh scattering at one wavelength, per metre.
	///
	/// beta = 8 pi^3 (n^2 - 1)^2 F / (3 N lambda^4), with the refractivity taken
	/// at the density it is being evaluated at. Everything about the gas enters
	/// through how much it bends light and how anisotropic its molecules are;
	/// everything about the planet enters through N.
	LEDGERCORE_API double RayleighPerMetre(
		ELedgerAir Composition, double NumberDensityPerCubicMetre,
		double WavelengthMetres);

	/// The colour of the sky, as relative RGB, looking through a given number of
	/// air masses. One is straight up; about thirty-eight is the horizon.
	///
	/// **Single scattering, and in air masses rather than metres.** The first
	/// version took a path length and multiplied every coefficient by it, which
	/// quietly assumed the dust and the gas were mixed to the same height. They
	/// are not -- haze sits in the bottom kilometre and the gas goes up eight --
	/// and treating them alike made Earth's zenith a washed-out pale blue by
	/// giving its haze six times the column it has.
	///
	/// So each component is integrated over its own scale height. What is
	/// scattered into the eye goes as the scattering optical depth; what
	/// survives to arrive goes as the total. The channel's brightness is
	/// tau_scatter (1 - e^-tau) / tau, and the colour is what those three do
	/// relative to each other.
	LEDGERCORE_API FVector3d SkyColour(
		const FLedgerAirProfile& Air, double AirMasses);

	/// **The aureole: the dust's forward peak, channel by channel.** T090.
	///
	/// A particle much bigger than the wavelength scatters about half of
	/// what it removes by diffraction, into a peak around the sun whose
	/// width goes as the wavelength over the radius -- two over the size
	/// parameter 2 pi r / lambda, in radians -- so the blue peak is narrower
	/// and, holding the same energy, brighter at its centre. That is the blue
	/// of a Martian sunset, and one Henyey-Greenstein lobe for all three
	/// channels cannot make it. At 440, 550 and 680 nm.
	LEDGERCORE_API FVector3d AureoleWidthRadians(const FLedgerAirProfile& Air);

	/// How much of the sky's scattered light belongs in that peak, per
	/// channel: the diffracted share of what the aerosol scatters (half its
	/// extinction over its albedo, so an absorbing dust puts more of its blue
	/// there), times the aerosol's share of the sky's scattering, and only for
	/// particles well past the wavelength -- faded in between size parameters
	/// 4 and 10, below which the one lobe already describes them. Earth's
	/// sub-micron haze gets almost none; Martian dust most of its blue.
	LEDGERCORE_API FVector3d AureoleFraction(const FLedgerAirProfile& Air);

	/// Single-scattering albedo of the suspended particles: how much of what
	/// they intercept carries on rather than being absorbed. Per channel.
	///
	/// This is the measured optical property that makes Mars butterscotch --
	/// its dust is about a tenth iron oxide, which is nearly opaque in the blue
	/// and nearly clear in the red.
	LEDGERCORE_API FVector3d AerosolAlbedo(ELedgerAir Composition);

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
