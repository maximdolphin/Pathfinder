#include "LedgerTerrainMath.h"

#include "LedgerTerrainDelta.h"

#include "LedgerLog.h"
#include "LedgerMath.h"

namespace LedgerTerrain
{
	FVector3d FaceToCube(ELedgerCubeFace Face, double U, double V)
	{
		// U and V are in [0,1]; map to [-1,1] across the face.
		const double A = U * 2.0 - 1.0;
		const double B = V * 2.0 - 1.0;

		switch (Face)
		{
		case ELedgerCubeFace::PositiveX: return FVector3d(1.0, A, B);
		case ELedgerCubeFace::NegativeX: return FVector3d(-1.0, -A, B);
		case ELedgerCubeFace::PositiveY: return FVector3d(-A, 1.0, B);
		case ELedgerCubeFace::NegativeY: return FVector3d(A, -1.0, B);
		case ELedgerCubeFace::PositiveZ: return FVector3d(A, B, 1.0);
		case ELedgerCubeFace::NegativeZ: return FVector3d(A, -B, -1.0);
		default: return FVector3d(1.0, 0.0, 0.0);
		}
	}

	void SphereToFace(const FVector3d& UnitSphere, ELedgerCubeFace& OutFace,
		double& OutU, double& OutV)
	{
		// The face is safe to take from the gnomonic answer: the warp scales
		// each axis by a factor that depends only on the other two, and at a
		// corner all three factors are equal, so it never changes which
		// component is largest.
		double TargetU = 0.0;
		double TargetV = 0.0;
		DirectionToFace(UnitSphere, OutFace, TargetU, TargetV);

		// Then undo the warp. Damped rather than multiplicative because the
		// correction passes through zero at the face centre and at its edges,
		// where a ratio would be zero over zero.
		double U = TargetU;
		double V = TargetV;
		// Forty and undamped rather than twelve at 0.8. The warp's derivative in
		// these coordinates runs about 0.7 to 1.3, so an undamped step
		// converges by a factor of at most 0.3 an iteration and a damped one is
		// slower, not safer. Twelve damped steps left 8e-7 of a face -- eight
		// metres, which is enough to put a probe in the wrong patch at the
		// finest LOD, where a patch is 305 m.
		for (int32 Iteration = 0; Iteration < 40; ++Iteration)
		{
			ELedgerCubeFace Landed = OutFace;
			double GotU = 0.0;
			double GotV = 0.0;
			DirectionToFace(CubeToSphere(FaceToCube(OutFace, U, V)), Landed, GotU, GotV);
			if (Landed != OutFace)
			{
				break;
			}
			const double ErrorU = TargetU - GotU;
			const double ErrorV = TargetV - GotV;
			U += ErrorU;
			V += ErrorV;
			if (FMath::Abs(ErrorU) < 1e-13 && FMath::Abs(ErrorV) < 1e-13)
			{
				break;
			}
		}

		OutU = U;
		OutV = V;
	}

	/// Which cube face a direction points at, and where on it.
	void DirectionToFace(const FVector3d& Direction, ELedgerCubeFace& OutFace, double& OutU, double& OutV)
	{
		const double Ax = FMath::Abs(Direction.X);
		const double Ay = FMath::Abs(Direction.Y);
		const double Az = FMath::Abs(Direction.Z);

		double A = 0.0;
		double B = 0.0;

		if (Ax >= Ay && Ax >= Az)
		{
			if (Direction.X > 0.0)
			{
				OutFace = ELedgerCubeFace::PositiveX;
				A = Direction.Y / Ax;
				B = Direction.Z / Ax;
			}
			else
			{
				OutFace = ELedgerCubeFace::NegativeX;
				A = -Direction.Y / Ax;
				B = Direction.Z / Ax;
			}
		}
		else if (Ay >= Az)
		{
			if (Direction.Y > 0.0)
			{
				OutFace = ELedgerCubeFace::PositiveY;
				A = -Direction.X / Ay;
				B = Direction.Z / Ay;
			}
			else
			{
				OutFace = ELedgerCubeFace::NegativeY;
				A = Direction.X / Ay;
				B = Direction.Z / Ay;
			}
		}
		else
		{
			if (Direction.Z > 0.0)
			{
				OutFace = ELedgerCubeFace::PositiveZ;
				A = Direction.X / Az;
				B = Direction.Y / Az;
			}
			else
			{
				OutFace = ELedgerCubeFace::NegativeZ;
				A = Direction.X / Az;
				B = -Direction.Y / Az;
			}
		}

		OutU = FMath::Clamp((A + 1.0) * 0.5, 0.0, 1.0);
		OutV = FMath::Clamp((B + 1.0) * 0.5, 0.0, 1.0);
	}

	FVector3d CubeToSphere(const FVector3d& OnCube)
	{
		const double X2 = OnCube.X * OnCube.X;
		const double Y2 = OnCube.Y * OnCube.Y;
		const double Z2 = OnCube.Z * OnCube.Z;

		return FVector3d(
			OnCube.X * FMath::Sqrt(1.0 - (Y2 + Z2) * 0.5 + (Y2 * Z2) / 3.0),
			OnCube.Y * FMath::Sqrt(1.0 - (Z2 + X2) * 0.5 + (Z2 * X2) / 3.0),
			OnCube.Z * FMath::Sqrt(1.0 - (X2 + Y2) * 0.5 + (X2 * Y2) / 3.0));
	}

	/// Depth as a fraction of the deepest ocean, given how far past the
	/// coastline the continental field has fallen.
	///
	/// Bathymetry is not a bowl. It is a nearly flat shelf, an abrupt break at
	/// around 140 m, a steep slope down to a few kilometres, and then an
	/// abyssal plain flatter than any surface on land. What was here before was
	/// `pow(depth, 0.75)` — a concave curve, steepest at the shore and
	/// flattening outward, which is that profile turned inside out. It gave
	/// every coast a drop-off at the waterline and no shelf at all.
	static double BathymetricProfile(double Offshore)
	{
		constexpr double ShelfEnd = 0.10;        // where the break sits
		constexpr double SlopeEnd = 0.26;        // foot of the continental slope
		constexpr double ShelfFraction = 0.024;  // 140 m of 5800
		constexpr double SlopeFraction = 0.60;   // 3500 m of 5800

		if (Offshore < ShelfEnd)
		{
			return (Offshore / ShelfEnd) * ShelfFraction;
		}
		if (Offshore < SlopeEnd)
		{
			const double Along = (Offshore - ShelfEnd) / (SlopeEnd - ShelfEnd);
			return ShelfFraction
				+ FMath::SmoothStep(0.0, 1.0, Along) * (SlopeFraction - ShelfFraction);
		}
		const double Along = (Offshore - SlopeEnd) / (1.0 - SlopeEnd);
		return SlopeFraction + Along * (1.0 - SlopeFraction);
	}

	/// Deepest ocean as a fraction of MaxElevation: 5.8 km against a 9 km
	/// ceiling, which is roughly the ratio Earth runs.
	constexpr double AbyssalFraction = 0.644;

	double OffshoreParameter(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params)
	{
		const uint32 Seed = Params.Seed;
		const FVector3d Warp(
			LedgerNoise::Fractal(UnitSphere * 2.1 + FVector3d(19.3, 7.1, 3.7), Seed ^ 0xA1u, 3),
			LedgerNoise::Fractal(UnitSphere * 2.1 + FVector3d(5.2, 23.9, 11.4), Seed ^ 0xB2u, 3),
			LedgerNoise::Fractal(UnitSphere * 2.1 + FVector3d(31.7, 2.8, 17.5), Seed ^ 0xC3u, 3));
		const double Continent = LedgerNoise::Fractal((UnitSphere + Warp * 0.26) * 1.25, Seed, 6);
		return FMath::Max(0.0,
			(Params.SeaLevel - Continent) / FMath::Max(0.05, Params.SeaLevel + 1.0));
	}

	/// The generated field, before anything anybody did to it.
	/// The near-field band, faded one octave at a time.
	///
	/// **Whole-band fading is what made the ground slide.** The first version
	/// of this attenuated all five octaves together with one smoothstep, so
	/// depth 16 carried 56% of the band and depth 17 carried 99% -- and the
	/// surface between two adjacent LOD rings differed by 43% of the whole
	/// band at once. Flying moves those rings across the ground continuously,
	/// so the terrain re-formed under the camera as they passed: waves, and a
	/// texture that appeared to slide.
	///
	/// Morphing could not hide it, and the reason is worth writing down.
	/// A morph target is the bilinear average of the patch's own samples --
	/// what the parent's surface would be *if the parent were a smoothed copy
	/// of this one*. Under whole-band fading it is not: the parent samples a
	/// materially different field. The morph therefore eased the surface
	/// towards a shape the parent never drew, and the discrepancy appeared as
	/// motion at every transition.
	///
	/// Per octave, each fades out around its own Nyquist limit. An octave of
	/// wavelength W is carried in full while the grid resolves it at two
	/// samples per wavelength, and is gone by one sample per wavelength where
	/// it is pure aliasing. Adjacent depths then differ by at most one
	/// octave's amplitude -- a sixteenth of the band at the finest, not
	/// half of it -- and that is inside what a morph can absorb.
	constexpr int32 NearFieldOctaves = 5;

	/// 30 m, from 2*pi*R / 1334000 on a 6,371 km planet.
	constexpr double NearFieldBaseWavelength = 30.0;
	constexpr double NearFieldBaseFrequency = 1334000.0;

	double NearFieldBand(
		const FVector3d& UnitSphere, uint32 Seed, double SampleSpacingMetres)
	{
		double Sum = 0.0;
		double Normalisation = 0.0;
		double Amplitude = 1.0;
		double Frequency = NearFieldBaseFrequency;
		double Wavelength = NearFieldBaseWavelength;

		for (int32 Octave = 0; Octave < NearFieldOctaves; ++Octave)
		{
			// Zero spacing means "no grid": the whole field, which is what
			// SurfaceRadiusAt and the diagnostics want.
			const double Strength = SampleSpacingMetres <= 0.0
				? 1.0
				: 1.0 - FMath::SmoothStep(
					Wavelength * 0.5, Wavelength, SampleSpacingMetres);

			Normalisation += Amplitude;
			if (Strength > 0.0)
			{
				Sum += LedgerNoise::Fractal(
					UnitSphere * Frequency,
					Seed + static_cast<uint32>(Octave) * 2731u, 1)
					* Amplitude * Strength;
			}

			Amplitude *= 0.5;
			Frequency *= 2.0;
			Wavelength *= 0.5;
		}

		return Normalisation > 0.0 ? Sum / Normalisation : 0.0;
	}

	double GeneratedElevation(
		const FVector3d& UnitSphere, const FLedgerTerrainParams& Params,
		double SampleSpacingMetres)
	{
		const uint32 Seed = Params.Seed;

		// **Frequencies are chosen against the planet's actual circumference.**
		//
		// A frequency of `f` on the unit sphere has a wavelength of
		// `2*pi*R / f`. On a 6,371 km planet that makes f=1 a 40,000 km feature
		// and f=400 a 100 km one. The first version of this function used f=90
		// as its *highest* band — a 440 km wavelength — so from a kilometre up
		// every visible thing was one smooth gradient and the terrain read as a
		// painted sphere. The bands below run from continents down to 70 m.
		//
		// The same numbers on a 60 km planet gave visible mountains, which is
		// exactly why they survived so long: they were right for a world that no
		// longer exists.

		// Domain warp. Sampling the noise at a position that has itself been
		// displaced by noise is the single cheapest thing that stops terrain
		// looking procedural: it bends coastlines and drags ranges into curves
		// instead of leaving everything isotropic and blobby.
		const FVector3d Warp(
			LedgerNoise::Fractal(UnitSphere * 2.1 + FVector3d(19.3, 7.1, 3.7), Seed ^ 0xA1u, 3),
			LedgerNoise::Fractal(UnitSphere * 2.1 + FVector3d(5.2, 23.9, 11.4), Seed ^ 0xB2u, 3),
			LedgerNoise::Fractal(UnitSphere * 2.1 + FVector3d(31.7, 2.8, 17.5), Seed ^ 0xC3u, 3));
		const FVector3d Warped = UnitSphere + Warp * 0.26;

		// Continents — 40,000 km down to about 1,200 km. Decides where land is,
		// and nothing else should.
		const double Continent = LedgerNoise::Fractal(Warped * 1.25, Seed, 6);

		// How far above sea level, in [0,1]. Everything below is ocean floor.
		const double Land = FMath::Clamp((Continent - Params.SeaLevel) / (1.0 - Params.SeaLevel), 0.0, 1.0);

		if (Continent < Params.SeaLevel)
		{
			// Ocean floor. See BathymetricProfile: a shelf, a break, a slope and
			// a plain, rather than one curve from the shore to the bottom.
			const double Offshore = (Params.SeaLevel - Continent) / FMath::Max(0.05, Params.SeaLevel + 1.0);
			const double Profile = BathymetricProfile(Offshore);

			// Relief follows the profile. Sediment blankets the shelf, the slope
			// is the one part of an ocean floor with any gradient worth the
			// name, and the abyssal plain is the flattest surface on the planet.
			const double Relief = 0.004 + 0.030 * FMath::Sin(
				LedgerPi * FMath::Clamp((Offshore - 0.06) / 0.22, 0.0, 1.0));

			const double Seabed = LedgerNoise::Fractal(Warped * 60.0, Seed ^ 0x2B2Bu, 3);
			return (-Profile * AbyssalFraction + Seabed * Relief) * Params.MaxElevation;
		}

		// Where the ranges are — 3,300 km provinces, so a continent has orogenic
		// belts and stable interiors rather than uniform crumple everywhere.
		const double Province = FMath::Clamp(
			LedgerNoise::Fractal(Warped * 12.0, Seed ^ 0x7E7Eu, 3) * 1.4 + 0.35, 0.0, 1.0);
		// Land^0.7, not Land^1.4.
		//
		// The exponent decides whether ranges can exist anywhere but on the
		// highest ground. At 1.4 they could not: the continent mask itself
		// tops out at 0.581, so Land^1.4 capped the range mask at 0.472, and
		// the survey found no point where a strong ridge and high land
		// coincided at all -- the two are independent fields. Renormalising
		// the ridge noise (T428) took the planet from 1,372 m to 1,911 m and
		// then stopped, because this was the next thing in the way.
		//
		// 0.7 lets a range stand on ordinary continent while still keeping it
		// off the shelf and out of the sea, which is what the mask is for.
		const double RangeMask = FMath::Pow(Land, 0.7) * Province;

		// Mountains — 130 km ranges resolving to about 500 m, eroded. The slope
		// damping is what turns a ridged multifractal from uniform crumple into
		// something with drainage: valleys widen and smooth, ridges stay sharp.
		const double Ridges = RidgeStrength(
			LedgerNoise::ErodedRidged(Warped * 310.0, Seed ^ 0x5A5Au, 8, 0.85));
		const double Mountains = Ridges * RangeMask;

		// Foothills and valleys — 8 km down to 1 km, eroded harder. This band
		// carries most of what reads as catchment at flying altitude.
		const double Mid = LedgerNoise::Eroded(Warped * 5200.0, Seed ^ 0x3C3Cu, 5, 1.6);

		// Rock and gully detail — 270 m down to about 70 m, eroded hardest so it
		// collects in gullies instead of pebbling every surface evenly.
		const double Micro = LedgerNoise::Eroded(UnitSphere * 150000.0, Seed ^ 0x1F1Fu, 4, 2.4);

		// Amplitudes fall off hard with frequency.
		//
		// Real landscapes are dominated by their largest features; the small
		// ones ride on top. Giving the 3 km and 270 m bands amplitudes anywhere
		// near the mountain band's turned the whole surface into uniform
		// crumpled foil — busy everywhere, structured nowhere. Each band here is
		// roughly a fifth of the one above it.
		double Height =
			Land * 0.26
			+ Mountains * 0.64
			+ Mid * (0.014 + Mountains * 0.034)
			+ Micro * (0.0010 + Mountains * 0.0030);

		// Near field -- 30 m down to about 1.9 m. T429.
		//
		// Everything above stops at a 33 m wavelength, which is why the ground
		// was a plane for thirty metres in every direction from wherever you
		// were standing. Nothing in the height function described anything
		// smaller than a house.
		//
		// The amplitude is deliberately tiny: 0.00017 of MaxElevation is about
		// 1.5 m, and it wants to stay that way. This band is the difference
		// between ground and a plane at walking distance and it is invisible
		// from a kilometre up; making it larger would put bumps on mountains
		// that are supposed to read as mountains.
		//
		// **Each octave is faded out on the grid that stops resolving it**, and
		// that is load-bearing rather than an optimisation. A patch at 4.8 m
		// spacing sampling a 1.9 m wavelength does not get less detail, it gets
		// a different random surface each time it is rebuilt at a different
		// depth -- which is a shimmer, not a texture. Fading the whole band as
		// one unit was worse and shipped for a few hours: see NearFieldBand.
		Height += NearFieldBand(UnitSphere, Seed ^ 0x4D4Du, SampleSpacingMetres)
			* 0.00017;

		return Height * Params.MaxElevation;
	}

	double Elevation(
		const FVector3d& UnitSphere, const FLedgerTerrainParams& Params,
		double SampleSpacingMetres)
	{
		const double Generated =
			GeneratedElevation(UnitSphere, Params, SampleSpacingMetres);
		if (!Params.Delta.IsValid())
		{
			// The overwhelmingly common case, and the second half of T062's
			// acceptance: on a planet nobody has dug, a modified terrain costs
			// one pointer test.
			return Generated;
		}
		return Params.Delta->Apply(UnitSphere, Generated / 100.0) * 100.0;
	}

	FLedgerElevationTerms ElevationTerms(
		const FVector3d& UnitSphere, const FLedgerTerrainParams& Params)
	{
		// Deliberately a copy of the terms above rather than a refactor of
		// Elevation to return them: Elevation is on the hot path and this is a
		// diagnostic. If they drift apart the survey stops meaning anything, so
		// the pairing is called out here and in the header.
		const uint32 Seed = Params.Seed;
		const FVector3d Warp(
			LedgerNoise::Fractal(UnitSphere * 2.1 + FVector3d(19.3, 7.1, 3.7), Seed ^ 0xA1u, 3),
			LedgerNoise::Fractal(UnitSphere * 2.1 + FVector3d(5.2, 23.9, 11.4), Seed ^ 0xB2u, 3),
			LedgerNoise::Fractal(UnitSphere * 2.1 + FVector3d(31.7, 2.8, 17.5), Seed ^ 0xC3u, 3));
		const FVector3d Warped = UnitSphere + Warp * 0.26;

		FLedgerElevationTerms Terms;
		Terms.Continent = LedgerNoise::Fractal(Warped * 1.25, Seed, 6);
		Terms.Land = FMath::Clamp(
			(Terms.Continent - Params.SeaLevel) / (1.0 - Params.SeaLevel), 0.0, 1.0);
		Terms.Province = FMath::Clamp(
			LedgerNoise::Fractal(Warped * 12.0, Seed ^ 0x7E7Eu, 3) * 1.4 + 0.35, 0.0, 1.0);
		Terms.RidgesRaw = LedgerNoise::ErodedRidged(
			Warped * 310.0, Seed ^ 0x5A5Au, 8, 0.85);
		Terms.Ridges = RidgeStrength(Terms.RidgesRaw);
		Terms.Mountains = Terms.Ridges * FMath::Pow(Terms.Land, 0.7) * Terms.Province;
		Terms.HeightFraction = Terms.Land * 0.26 + Terms.Mountains * 0.64;
		return Terms;
	}

	double ScreenSpaceError(
		double NodeWorldSize,
		double DistanceToCamera,
		double ViewportWidthPixels,
		double HorizontalFovRadians)
	{
		// A node closer than its own size is effectively wrapped around the
		// camera; clamp so the error stays finite rather than special-casing it.
		const double Distance = FMath::Max(DistanceToCamera, 1.0);
		const double HalfFovTangent = FMath::Tan(HorizontalFovRadians * 0.5);
		if (HalfFovTangent <= 0.0)
		{
			return 0.0;
		}

		// Projected size of the node's residual geometric error. The error a
		// node removes by subdividing is proportional to its own extent.
		return (NodeWorldSize / Distance) * (ViewportWidthPixels / (2.0 * HalfFovTangent));
	}
}
