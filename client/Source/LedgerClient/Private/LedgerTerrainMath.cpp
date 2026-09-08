#include "LedgerTerrainMath.h"

namespace
{
	/// Integer hash. Cheap, well-mixed, and identical on every platform — which
	/// a `FMath::RandInit`-style generator would not be.
	FORCEINLINE uint32 Hash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7FEB352Du;
		X ^= X >> 15;
		X *= 0x846CA68Bu;
		X ^= X >> 16;
		return X;
	}

	FORCEINLINE uint32 Hash3(int32 X, int32 Y, int32 Z, uint32 Seed)
	{
		return Hash(static_cast<uint32>(X) * 0x9E3779B9u
			^ Hash(static_cast<uint32>(Y) * 0x85EBCA6Bu
				^ Hash(static_cast<uint32>(Z) * 0xC2B2AE35u ^ Seed)));
	}

	/// One of twelve edge-midpoint gradients, selected by hash. The classic
	/// Perlin set — evenly distributed, and each dot product is two adds.
	FORCEINLINE double GradientDot(uint32 H, double X, double Y, double Z)
	{
		switch (H & 15u)
		{
		case 0:  return  X + Y;
		case 1:  return -X + Y;
		case 2:  return  X - Y;
		case 3:  return -X - Y;
		case 4:  return  X + Z;
		case 5:  return -X + Z;
		case 6:  return  X - Z;
		case 7:  return -X - Z;
		case 8:  return  Y + Z;
		case 9:  return -Y + Z;
		case 10: return  Y - Z;
		case 11: return -Y - Z;
		case 12: return  X + Y;
		case 13: return -Y + Z;
		case 14: return -X + Y;
		default: return -Y - Z;
		}
	}

	/// Quintic smoothstep. C2 continuous, so normals derived from this do not
	/// band the way cubic smoothstep's do.
	FORCEINLINE double Fade(double T)
	{
		return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
	}

	/// d/dt of `Fade`: 30t^2(t-1)^2.
	FORCEINLINE double FadeDerivative(double T)
	{
		const double TMinusOne = T - 1.0;
		return 30.0 * T * T * TMinusOne * TMinusOne;
	}

	/// The same twelve gradients as `GradientDot`, as vectors.
	FORCEINLINE FVector3d GradientVector(uint32 H)
	{
		switch (H & 15u)
		{
		case 0:  return FVector3d( 1,  1,  0);
		case 1:  return FVector3d(-1,  1,  0);
		case 2:  return FVector3d( 1, -1,  0);
		case 3:  return FVector3d(-1, -1,  0);
		case 4:  return FVector3d( 1,  0,  1);
		case 5:  return FVector3d(-1,  0,  1);
		case 6:  return FVector3d( 1,  0, -1);
		case 7:  return FVector3d(-1,  0, -1);
		case 8:  return FVector3d( 0,  1,  1);
		case 9:  return FVector3d( 0, -1,  1);
		case 10: return FVector3d( 0,  1, -1);
		case 11: return FVector3d( 0, -1, -1);
		case 12: return FVector3d( 1,  1,  0);
		case 13: return FVector3d( 0, -1,  1);
		case 14: return FVector3d(-1,  1,  0);
		default: return FVector3d( 0, -1, -1);
		}
	}
}

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

	double GradientNoise(const FVector3d& Position, uint32 Seed)
	{
		const double Fx = FMath::Floor(Position.X);
		const double Fy = FMath::Floor(Position.Y);
		const double Fz = FMath::Floor(Position.Z);

		const int32 Ix = static_cast<int32>(Fx);
		const int32 Iy = static_cast<int32>(Fy);
		const int32 Iz = static_cast<int32>(Fz);

		const double Rx = Position.X - Fx;
		const double Ry = Position.Y - Fy;
		const double Rz = Position.Z - Fz;

		const double Tx = Fade(Rx);
		const double Ty = Fade(Ry);
		const double Tz = Fade(Rz);

		double Accumulated = 0.0;
		for (int32 Dz = 0; Dz < 2; ++Dz)
		{
			const double Wz = Dz ? Tz : 1.0 - Tz;
			for (int32 Dy = 0; Dy < 2; ++Dy)
			{
				const double Wy = Dy ? Ty : 1.0 - Ty;
				for (int32 Dx = 0; Dx < 2; ++Dx)
				{
					const double Wx = Dx ? Tx : 1.0 - Tx;
					const uint32 H = Hash3(Ix + Dx, Iy + Dy, Iz + Dz, Seed);
					// Gradient dotted with the offset from *that* corner.
					const double Contribution = GradientDot(H, Rx - Dx, Ry - Dy, Rz - Dz);
					Accumulated += Contribution * Wx * Wy * Wz;
				}
			}
		}

		// Gradient noise over the 12 edge gradients lands within about ±0.7.
		return FMath::Clamp(Accumulated * 1.4, -1.0, 1.0);
	}

	double FractalNoise(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double Lacunarity,
		double Gain)
	{
		double Sum = 0.0;
		double Amplitude = 1.0;
		double Frequency = 1.0;
		double Normalisation = 0.0;

		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			Sum += GradientNoise(Position * Frequency, Seed + static_cast<uint32>(Octave) * 7919u) * Amplitude;
			Normalisation += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}

		return Normalisation > 0.0 ? Sum / Normalisation : 0.0;
	}

	double RidgedNoise(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double Lacunarity,
		double Gain)
	{
		double Sum = 0.0;
		double Amplitude = 0.5;
		double Frequency = 1.0;
		double Normalisation = 0.0;
		// Carries the previous octave's ridge forward, so detail only appears
		// where a ridge already is. This is what connects peaks into ranges
		// instead of scattering them evenly across the continent.
		double Weight = 1.0;

		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			double Signal = GradientNoise(Position * Frequency, Seed + static_cast<uint32>(Octave) * 6151u);
			Signal = 1.0 - FMath::Abs(Signal);
			Signal *= Signal;
			Signal *= Weight;

			Weight = FMath::Clamp(Signal * 2.2, 0.0, 1.0);

			Sum += Signal * Amplitude;
			Normalisation += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}

		return Normalisation > 0.0 ? (Sum / Normalisation) * 2.0 - 1.0 : 0.0;
	}

	FNoiseSample GradientNoiseWithDerivative(const FVector3d& Position, uint32 Seed)
	{
		const double Fx = FMath::Floor(Position.X);
		const double Fy = FMath::Floor(Position.Y);
		const double Fz = FMath::Floor(Position.Z);

		const int32 Ix = static_cast<int32>(Fx);
		const int32 Iy = static_cast<int32>(Fy);
		const int32 Iz = static_cast<int32>(Fz);

		const double Rx = Position.X - Fx;
		const double Ry = Position.Y - Fy;
		const double Rz = Position.Z - Fz;

		const double Tx = Fade(Rx);
		const double Ty = Fade(Ry);
		const double Tz = Fade(Rz);
		const double Dx = FadeDerivative(Rx);
		const double Dy = FadeDerivative(Ry);
		const double Dz = FadeDerivative(Rz);

		FNoiseSample Sample;

		for (int32 Cz = 0; Cz < 2; ++Cz)
		{
			const double Wz = Cz ? Tz : 1.0 - Tz;
			const double DWz = Cz ? Dz : -Dz;
			for (int32 Cy = 0; Cy < 2; ++Cy)
			{
				const double Wy = Cy ? Ty : 1.0 - Ty;
				const double DWy = Cy ? Dy : -Dy;
				for (int32 Cx = 0; Cx < 2; ++Cx)
				{
					const double Wx = Cx ? Tx : 1.0 - Tx;
					const double DWx = Cx ? Dx : -Dx;

					const FVector3d Gradient = GradientVector(Hash3(Ix + Cx, Iy + Cy, Iz + Cz, Seed));
					const FVector3d Offset(Rx - Cx, Ry - Cy, Rz - Cz);
					const double Dot = FVector3d::DotProduct(Gradient, Offset);

					const double Weight = Wx * Wy * Wz;
					Sample.Value += Dot * Weight;

					// Product rule: the gradient contributes through the dot
					// product, and the interpolation weights contribute through
					// the fade curve.
					Sample.Derivative.X += Gradient.X * Weight + Dot * DWx * Wy * Wz;
					Sample.Derivative.Y += Gradient.Y * Weight + Dot * Wx * DWy * Wz;
					Sample.Derivative.Z += Gradient.Z * Weight + Dot * Wx * Wy * DWz;
				}
			}
		}

		// Same normalisation as `GradientNoise`, applied to both.
		Sample.Value *= 1.4;
		Sample.Derivative *= 1.4;
		return Sample;
	}

	double ErodedNoise(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double ErosionStrength,
		double Lacunarity,
		double Gain)
	{
		double Sum = 0.0;
		double Amplitude = 1.0;
		double Frequency = 1.0;
		double Normalisation = 0.0;
		// Accumulated slope of everything coarser than the current octave.
		FVector3d Slope = FVector3d::ZeroVector;

		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			const FNoiseSample Sample = GradientNoiseWithDerivative(
				Position * Frequency, Seed + static_cast<uint32>(Octave) * 7919u);

			Slope += Sample.Derivative * Frequency * Amplitude;

			// Damping. Steep ground accumulates less detail — the same place
			// water would have carried it away from.
			const double Damping = 1.0 / (1.0 + ErosionStrength * Slope.SquaredLength());

			Sum += Sample.Value * Amplitude * Damping;
			Normalisation += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}

		return Normalisation > 0.0 ? Sum / Normalisation : 0.0;
	}

	double ErodedRidgedNoise(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double ErosionStrength,
		double Lacunarity,
		double Gain)
	{
		double Sum = 0.0;
		double Amplitude = 0.5;
		double Frequency = 1.0;
		double Normalisation = 0.0;
		double Weight = 1.0;
		FVector3d Slope = FVector3d::ZeroVector;

		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			const FNoiseSample Sample = GradientNoiseWithDerivative(
				Position * Frequency, Seed + static_cast<uint32>(Octave) * 6151u);

			Slope += Sample.Derivative * Frequency * Amplitude;
			const double Damping = 1.0 / (1.0 + ErosionStrength * Slope.SquaredLength());

			double Signal = 1.0 - FMath::Abs(Sample.Value);
			Signal *= Signal;
			Signal *= Weight;

			// The ridge continuity term, unchanged: detail only where a ridge
			// already runs, so peaks connect into ranges.
			Weight = FMath::Clamp(Signal * 2.2, 0.0, 1.0);

			Sum += Signal * Amplitude * Damping;
			Normalisation += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}

		return Normalisation > 0.0 ? (Sum / Normalisation) * 2.0 - 1.0 : 0.0;
	}

	double Elevation(const FVector3d& UnitSphere, const FLedgerTerrainParams& Params)
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
			FractalNoise(UnitSphere * 2.1 + FVector3d(19.3, 7.1, 3.7), Seed ^ 0xA1u, 3),
			FractalNoise(UnitSphere * 2.1 + FVector3d(5.2, 23.9, 11.4), Seed ^ 0xB2u, 3),
			FractalNoise(UnitSphere * 2.1 + FVector3d(31.7, 2.8, 17.5), Seed ^ 0xC3u, 3));
		const FVector3d Warped = UnitSphere + Warp * 0.26;

		// Continents — 40,000 km down to about 1,200 km. Decides where land is,
		// and nothing else should.
		const double Continent = FractalNoise(Warped * 1.25, Seed, 6);

		// How far above sea level, in [0,1]. Everything below is ocean floor.
		const double Land = FMath::Clamp((Continent - Params.SeaLevel) / (1.0 - Params.SeaLevel), 0.0, 1.0);

		if (Continent < Params.SeaLevel)
		{
			// Ocean floor. Deepens away from the coast and stays smooth — an
			// eroded seabed has no ridges on it.
			const double Depth = (Params.SeaLevel - Continent) / FMath::Max(0.05, Params.SeaLevel + 1.0);
			const double Seabed = FractalNoise(Warped * 60.0, Seed ^ 0x2B2Bu, 3);
			return (-FMath::Pow(Depth, 0.75) * 0.55 + Seabed * 0.012) * Params.MaxElevation;
		}

		// Where the ranges are — 3,300 km provinces, so a continent has orogenic
		// belts and stable interiors rather than uniform crumple everywhere.
		const double Province = FMath::Clamp(
			FractalNoise(Warped * 12.0, Seed ^ 0x7E7Eu, 3) * 1.4 + 0.35, 0.0, 1.0);
		const double RangeMask = FMath::Pow(Land, 1.4) * Province;

		// Mountains — 130 km ranges resolving to about 500 m, eroded. The slope
		// damping is what turns a ridged multifractal from uniform crumple into
		// something with drainage: valleys widen and smooth, ridges stay sharp.
		const double Ridges = FMath::Max(0.0,
			ErodedRidgedNoise(Warped * 310.0, Seed ^ 0x5A5Au, 8, 0.85));
		const double Mountains = Ridges * RangeMask;

		// Foothills and valleys — 8 km down to 1 km, eroded harder. This band
		// carries most of what reads as catchment at flying altitude.
		const double Mid = ErodedNoise(Warped * 5200.0, Seed ^ 0x3C3Cu, 5, 1.6);

		// Rock and gully detail — 270 m down to about 70 m, eroded hardest so it
		// collects in gullies instead of pebbling every surface evenly.
		const double Micro = ErodedNoise(UnitSphere * 150000.0, Seed ^ 0x1F1Fu, 4, 2.4);

		// Amplitudes fall off hard with frequency.
		//
		// Real landscapes are dominated by their largest features; the small
		// ones ride on top. Giving the 3 km and 270 m bands amplitudes anywhere
		// near the mountain band's turned the whole surface into uniform
		// crumpled foil — busy everywhere, structured nowhere. Each band here is
		// roughly a fifth of the one above it.
		const double Height =
			Land * 0.26
			+ Mountains * 0.64
			+ Mid * (0.014 + Mountains * 0.034)
			+ Micro * (0.0010 + Mountains * 0.0030);

		return Height * Params.MaxElevation;
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
