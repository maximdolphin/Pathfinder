#include "LedgerNoise.h"

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


namespace LedgerNoise
{
	double Gradient(const FVector3d& Position, uint32 Seed)
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

	double Fractal(
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
			Sum += Gradient(Position * Frequency, Seed + static_cast<uint32>(Octave) * 7919u) * Amplitude;
			Normalisation += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}

		return Normalisation > 0.0 ? Sum / Normalisation : 0.0;
	}

	double Ridged(
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
			double Signal = Gradient(Position * Frequency, Seed + static_cast<uint32>(Octave) * 6151u);
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

	FSample GradientWithDerivative(const FVector3d& Position, uint32 Seed)
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

		FSample Sample;

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

		// Same normalisation as `Gradient`, applied to both.
		Sample.Value *= 1.4;
		Sample.Derivative *= 1.4;
		return Sample;
	}

	double Eroded(
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
			const FSample Sample = GradientWithDerivative(
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

	double ErodedRidged(
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
			const FSample Sample = GradientWithDerivative(
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
}
