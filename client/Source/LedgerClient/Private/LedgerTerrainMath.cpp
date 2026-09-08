#include "LedgerTerrainMath.h"

namespace
{
	/// Integer hash. Cheap, well-mixed, and identical on every platform — which
	/// a `FMath::RandInit`-style generator would not be.
	uint32 Hash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7FEB352Du;
		X ^= X >> 15;
		X *= 0x846CA68Bu;
		X ^= X >> 16;
		return X;
	}

	uint32 Hash3(int32 X, int32 Y, int32 Z, uint32 Seed)
	{
		return Hash(static_cast<uint32>(X) * 0x9E3779B9u
			^ Hash(static_cast<uint32>(Y) * 0x85EBCA6Bu
				^ Hash(static_cast<uint32>(Z) * 0xC2B2AE35u ^ Seed)));
	}

	double UnitFromHash(uint32 H)
	{
		return static_cast<double>(H & 0x00FFFFFFu) / static_cast<double>(0x00FFFFFF);
	}

	/// Quintic smoothstep. C2 continuous, so the normals derived from this do
	/// not band the way cubic smoothstep's do.
	double Fade(double T)
	{
		return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
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

	double ValueNoise(const FVector3d& Position, uint32 Seed)
	{
		const double Fx = FMath::Floor(Position.X);
		const double Fy = FMath::Floor(Position.Y);
		const double Fz = FMath::Floor(Position.Z);

		const int32 Ix = static_cast<int32>(Fx);
		const int32 Iy = static_cast<int32>(Fy);
		const int32 Iz = static_cast<int32>(Fz);

		const double Tx = Fade(Position.X - Fx);
		const double Ty = Fade(Position.Y - Fy);
		const double Tz = Fade(Position.Z - Fz);

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
					const double Corner = UnitFromHash(Hash3(Ix + Dx, Iy + Dy, Iz + Dz, Seed));
					Accumulated += Corner * Wx * Wy * Wz;
				}
			}
		}

		return Accumulated * 2.0 - 1.0;
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
			Sum += ValueNoise(Position * Frequency, Seed + static_cast<uint32>(Octave) * 7919u) * Amplitude;
			Normalisation += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}

		return Normalisation > 0.0 ? Sum / Normalisation : 0.0;
	}

	double Elevation(const FVector3d& UnitSphere, uint32 Seed, double MaxElevation)
	{
		// Continents: low frequency, high amplitude. This is what makes the
		// planet read as a planet from orbit rather than as uniform crumple.
		const double Continents = FractalNoise(UnitSphere * 1.6, Seed, 5);

		// Ridges: fold the noise about zero so the creases point up. Weighted by
		// continent height so ocean floors stay smooth and the mountains sit on
		// the landmasses instead of everywhere.
		const double Ridged = 1.0 - FMath::Abs(FractalNoise(UnitSphere * 7.0, Seed ^ 0x5A5Au, 5));
		const double LandMask = FMath::Clamp(Continents * 2.0, 0.0, 1.0);

		// Sea level flattens everything below it, so coastlines are legible.
		const double Combined = Continents * 0.7 + Ridged * Ridged * LandMask * 0.45;
		const double Shaped = Combined < 0.0 ? Combined * 0.35 : Combined;

		return Shaped * MaxElevation;
	}

	FVector3d SurfacePoint(
		const FVector3d& UnitSphere,
		double Radius,
		uint32 Seed,
		double MaxElevation)
	{
		return UnitSphere * (Radius + Elevation(UnitSphere, Seed, MaxElevation));
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
