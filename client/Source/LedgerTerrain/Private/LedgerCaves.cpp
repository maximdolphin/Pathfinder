#include "LedgerCaves.h"

#include "LedgerNoise.h"

namespace
{
	/// Where the cave field is sampled, in tunnel-spacing units.
	///
	/// Position on the sphere at the *surface* radius plus altitude, so a
	/// tunnel that runs level stays level rather than following the ground.
	/// Scaled to metres first: the terrain works in centimetres and a noise
	/// wavelength expressed in centimetres is a number nobody can check.
	FVector3d CaveSpace(const FVector3d& UnitSphere, double AltitudeMetres,
		const FLedgerTerrainParams& Params)
	{
		const double RadiusMetres = Params.Radius / 100.0;
		return (UnitSphere * (RadiusMetres + AltitudeMetres))
			/ LedgerCaves::TunnelSpacingMetres;
	}

	/// The low-frequency mask that decides where cave country is.
	double Country(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params)
	{
		// Forty tunnel spacings, so a cave region is roughly ninety kilometres
		// across -- big enough to be somewhere you go rather than something you
		// trip over.
		const FVector3d Position = UnitSphere * (Params.Radius / 100.0)
			/ (LedgerCaves::TunnelSpacingMetres * 40.0);
		return LedgerNoise::Fractal(Position, Params.Seed ^ 0xCA7E5u, 3);
	}

	/// The threshold `Country` has to clear, chosen so that the fraction of the
	/// planet above it is about CaveCountryFraction.
	///
	/// Fractal noise here is roughly symmetric about zero with most of its mass
	/// inside [-0.5, 0.5]; 0.16 puts a little over a fifth above it. Measured
	/// rather than derived -- see Ledger.Caves.CaveCountryIsAboutAFifth, which
	/// fails if the noise ever changes shape underneath this.
	constexpr double CountryThreshold = 0.16;
}

namespace LedgerCaves
{
	double Density(const FVector3d& UnitSphere, double AltitudeMetres,
		double GroundMetres, const FLedgerTerrainParams& Params)
	{
		// Outside the shell there is no cave, and saying so first is what keeps
		// this cheap for the ninety-odd per cent of the planet that is either
		// open sky or deep rock. Measured from the ground: a shell fixed to sea
		// level would be under the sea on a coast and hundreds of metres down a
		// hillside.
		const double AboveGround = AltitudeMetres - GroundMetres;
		if (AboveGround > ShellAboveMetres || AboveGround < -ShellBelowMetres)
		{
			return -1000.0;
		}

		const double CountryValue = Country(UnitSphere, Params);
		if (CountryValue <= CountryThreshold)
		{
			return -1000.0;
		}

		// How firmly this is cave country, in [0,1]. Tunnels taper out at the
		// edge of a region rather than being cut off mid-passage.
		const double Region = FMath::Min(1.0, (CountryValue - CountryThreshold) / 0.25);

		// Two independent fields. Where both are near zero the point is on a
		// curve in three dimensions, which is a tunnel.
		//
		// **Divided by their own gradients, which is the whole difficulty.** A
		// noise value is not a distance: how far `n = 0.01` is from `n = 0`
		// depends entirely on how fast n is changing there. The first version
		// compared the raw values against a radius converted by assuming the
		// field moves by one over one tunnel spacing, and the survey found 154
		// open cells in a two-kilometre box -- a passage two cells long. The
		// assumption was out by more than an order of magnitude and there was
		// no way to see that from the constants.
		//
		// `GradientWithDerivative` gives the slope exactly and for free, so
		// `value / |gradient|` is a real distance in scaled units and the
		// radius below is a radius in metres.
		const FVector3d Position = CaveSpace(UnitSphere, AltitudeMetres, Params);
		const LedgerNoise::FSample First =
			LedgerNoise::GradientWithDerivative(Position, Params.Seed ^ 0x7A11u);
		const LedgerNoise::FSample Second = LedgerNoise::GradientWithDerivative(
			Position + FVector3d(31.7, -17.3, 53.1), Params.Seed ^ 0x1CE9u);

		const double FirstSlope = FMath::Max(First.Derivative.Length(), 0.05);
		const double SecondSlope = FMath::Max(Second.Derivative.Length(), 0.05);
		const double AlongFirst = First.Value / FirstSlope;
		const double AlongSecond = Second.Value / SecondSlope;

		const double FromAxisMetres = FMath::Sqrt(
			AlongFirst * AlongFirst + AlongSecond * AlongSecond) * TunnelSpacingMetres;

		// Taper towards the roof of the shell rather than ending flat, so a
		// passage that reaches the surface opens into it instead of stopping at
		// an invisible ceiling.
		const double Roof = FMath::Clamp(
			(ShellAboveMetres - AboveGround) / 40.0, 0.0, 1.0);

		return PassageRadiusMetres * Region * Roof - FromAxisMetres;
	}

	bool MightContainCaves(const FVector3d& UnitSphere, double RadiusMetres,
		const FLedgerTerrainParams& Params)
	{
		// Only the region mask, and only generously. This has to be allowed to
		// say "maybe" and must never say "no" where a cave exists, so it tests
		// the mask well below its real threshold -- the mask varies over ninety
		// kilometres, and a patch is far smaller than the margin that buys.
		const double CountryValue = Country(UnitSphere, Params);
		const double Margin = FMath::Min(0.15, RadiusMetres / 90000.0);
		return CountryValue > CountryThreshold - Margin;
	}
}
