#include "LedgerScatter.h"

#include "Misc/CommandLine.h"

#include "LedgerBiome.h"
#include "LedgerClimate.h"
#include "LedgerPlanet.h"

namespace
{
	/// A hash of a patch key and a cell, split into whatever uniforms are
	/// wanted.
	///
	/// Not FMath::Rand or a seeded stream: a stream has an order, and the order
	/// a patch is generated in is exactly what must not matter. This is a pure
	/// function of where the cell is, so two runs agree without having to agree
	/// about anything else.
	uint64 CellHash(uint64 Key, int32 Cell, uint32 Salt)
	{
		uint64 Hash = Key * 0x9E3779B97F4A7C15ull;
		Hash ^= static_cast<uint64>(Cell) + 0x165667B19E3779F9ull + (Hash << 6) + (Hash >> 2);
		Hash ^= static_cast<uint64>(Salt) * 0xD6E8FEB86659FD93ull;
		Hash ^= Hash >> 33;
		Hash *= 0xFF51AFD7ED558CCDull;
		Hash ^= Hash >> 29;
		return Hash;
	}

	/// A uniform in [0,1) from a hash.
	double Uniform(uint64 Hash)
	{
		return static_cast<double>(Hash >> 11) / static_cast<double>(1ull << 53);
	}
}

namespace LedgerScatter
{
	void ScatterPatch(FLedgerPatchJob& Job)
	{
		Job.Scatter.Reset();

		// Near patches, and only the finest of those.
		//
		// Both conditions, and the collision one is not redundant: dropping it
		// while trying to thin the scatter with distance quietly doubled the
		// forest and put trees across a desert the reference captures had bare.
		// The size limit is about how big an instance is on screen;
		// bWithCollision is the planet's own statement about how close the
		// patch is, and they are not the same set.
		// `-noscatter` places nothing. A control arm, not a setting: when
		// something on the ground looks wrong, the first question is whether it
		// is the ground or the things standing on it, and that is not
		// answerable by looking harder at a photograph containing both.
		static const bool bDisabled =
			FParse::Param(FCommandLine::Get(), TEXT("noscatter"));

		const double SizeRatio = FMath::Max(1.0, Job.WorldSize / FinestWorldSize);
		if (bDisabled || !Job.bWithCollision || SizeRatio > MaxSizeRatio)
		{
			return;
		}
		const int32 Cells = FMath::Max(4,
			FMath::RoundToInt32(CellsAcross / FMath::Sqrt(SizeRatio)));

		const TArray<FLedgerBiome>* Biomes = Job.Biomes.IsValid() ? Job.Biomes.Get() : nullptr;
		if (Biomes == nullptr || Biomes->Num() == 0)
		{
			return;
		}

		const double RadiusCm = Job.Params.Radius;

		// The patch's own mesh, if it has been generated yet. Instances are
		// placed on *it* rather than on the height function.
		//
		// **Because the two are not the same surface.** The mesh is a bilinear
		// interpolation of the function sampled every 4.8 m, and on the
		// roughest patch this planet has that interpolation sits up to 1.22 m
		// away from the function it interpolates. A tree placed at the exact
		// value is therefore up to 1.22 m above or below the ground a player
		// can see -- which is precisely "floating or half-buried", and it was
		// invisible until the test stopped measuring the function against
		// itself.
		const int32 Side = FMath::Max(3, Job.Side | 1);
		const bool bHasMesh = Job.Vertices.Num() == Side * Side;
		auto MeshRadiusAt = [&Job, Side](double LocalU, double LocalV)
		{
			const double GridU = FMath::Clamp(LocalU, 0.0, 1.0) * (Side - 1);
			const double GridV = FMath::Clamp(LocalV, 0.0, 1.0) * (Side - 1);
			const int32 X0 = FMath::Clamp(FMath::FloorToInt32(GridU), 0, Side - 2);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(GridV), 0, Side - 2);
			const double FracX = GridU - X0;
			const double FracY = GridV - Y0;

			auto Corner = [&Job, Side](int32 X, int32 Y)
			{
				return (FVector3d(Job.Vertices[Y * Side + X]) + Job.Centre).Length();
			};
			return FMath::Lerp(
				FMath::Lerp(Corner(X0, Y0), Corner(X0 + 1, Y0), FracX),
				FMath::Lerp(Corner(X0, Y0 + 1), Corner(X0 + 1, Y0 + 1), FracX), FracY);
		};

		// The patch's own footprint in metres, for the slope estimate below.
		const double PatchMetres = Job.WorldSize / 100.0;
		const double CellMetres = FMath::Max(1.0, PatchMetres / Cells);

		TArray<double> Weights;
		for (int32 CellV = 0; CellV < Cells; ++CellV)
		{
			for (int32 CellU = 0; CellU < Cells; ++CellU)
			{
				const int32 Cell = CellV * Cells + CellU;

				// Jitter inside the cell, so the result is not a grid. A grid
				// is the single loudest tell that ground was generated, and it
				// survives any amount of density tuning.
				const double JitterU = Uniform(CellHash(Job.Key, Cell, 1u));
				const double JitterV = Uniform(CellHash(Job.Key, Cell, 2u));

				const double LocalU = (CellU + JitterU) / Cells;
				const double LocalV = (CellV + JitterV) / Cells;
				const FVector3d Direction = LedgerTerrain::CubeToSphere(
					LedgerTerrain::FaceToCube(Job.Face,
						Job.U + LocalU * Job.Extent, Job.V + LocalV * Job.Extent));

				const double Elevation = LedgerTerrain::Elevation(Direction, Job.Params);
				if (Elevation <= 0.0)
				{
					continue;
				}

				// Slope from two neighbours a cell away. Cheap and local, and
				// it is what keeps things off cliffs -- the biome's own slope
				// limit is about which ground it is, not about whether a tree
				// would fall over.
				FVector3d East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Direction);
				if (East.IsNearlyZero())
				{
					East = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), Direction);
				}
				East.Normalize();
				const FVector3d North = FVector3d::CrossProduct(Direction, East).GetSafeNormal();

				const double StepArc = (CellMetres * 100.0) / RadiusCm;
				const double RiseEast = LedgerTerrain::Elevation(
					(Direction * FMath::Cos(StepArc) + East * FMath::Sin(StepArc)).GetSafeNormal(),
					Job.Params) - Elevation;
				const double RiseNorth = LedgerTerrain::Elevation(
					(Direction * FMath::Cos(StepArc) + North * FMath::Sin(StepArc)).GetSafeNormal(),
					Job.Params) - Elevation;

				const double SlopeDegrees = FMath::RadiansToDegrees(FMath::Atan2(
					FVector2d(RiseEast, RiseNorth).Length() / 100.0, CellMetres));
				if (SlopeDegrees > MaxSlopeDegrees)
				{
					continue;
				}

				// What grows here. The same climate the ground is painted from,
				// so a forest floor has trees on it and a desert does not,
				// without a second rule to keep in step with the first.
				FLedgerClimate Climate =
					LedgerClimate::At(Direction, Job.Params, Job.SeasonPhase);
				LedgerBiomes::Weigh(*Biomes, Climate, SlopeDegrees, Weights);

				double Density = 0.0;
				for (int32 Index = 0; Index < Weights.Num(); ++Index)
				{
					Density += Weights[Index] * (*Biomes)[Index].ScatterDensity;
				}

				// Under deep snow there is nothing to see. Not zero at the
				// first flake -- a wood in winter is still a wood -- but a
				// metre of cover buries the undergrowth this scatters.
				Density *= 1.0 - 0.8 * LedgerClimate::SnowCover(Climate);

				if (Uniform(CellHash(Job.Key, Cell, 3u)) >= Density)
				{
					continue;
				}

				// Sitting on the drawn ground, not near it. See MeshRadiusAt:
				// the mesh and the height function are different surfaces, and
				// the one that matters is the one being drawn.
				const double SurfaceRadius = bHasMesh
					? MeshRadiusAt(LocalU, LocalV)
					: RadiusCm + Elevation;
				const FVector3d Surface = Direction * SurfaceRadius;

				// The surface normal from the two rises, so a tree on a slope
				// leans with it instead of standing plumb out of a hillside.
				const FVector3d Normal = (Direction
					- East * (RiseEast / (CellMetres * 100.0))
					- North * (RiseNorth / (CellMetres * 100.0))).GetSafeNormal();

				const double Yaw = Uniform(CellHash(Job.Key, Cell, 4u)) * 2.0 * PI;
				const FVector3d Facing =
					(East * FMath::Cos(Yaw) + North * FMath::Sin(Yaw)).GetSafeNormal();

				FLedgerScatterInstance Instance;
				Instance.Position = FVector3f(Surface - Job.Centre);
				Instance.Rotation = FQuat4f(FRotationMatrix::MakeFromZX(
					FVector(Normal), FVector(Facing)).ToQuat());
				// Squared, so most stones are small.
				//
				// This was 0.75 to 1.45, which was right when the scatter mesh
				// was an eighteen metre tree and is wrong now it is a one metre
				// stone: it made every instance a boulder, and a field of
				// identically-sized boulders is the one distribution real
				// ground never has. Squaring a uniform puts the mass at the
				// bottom -- half the stones under 36 cm, one in ten over 70 cm,
				// none over a metre -- which is roughly how a clast size
				// distribution actually sorts. Cubed was tried first and was an
				// overcorrection: it put half the field under 22 cm, which at
				// three metres is not a stone, it is a speckle.
				const double Roll = Uniform(CellHash(Job.Key, Cell, 5u));
				Instance.Scale = static_cast<float>(0.15 + Roll * Roll * 0.85);

				// The whole byte. How many meshes there actually are is the
				// planet's business, not this function's -- it was masked to
				// one bit for the two tree variants, which silently meant a
				// third rock could never be placed.
				Instance.Variant = static_cast<uint8>(
					CellHash(Job.Key, Cell, 6u) & 0xFFull);
				Job.Scatter.Add(Instance);
			}
		}
	}
}
