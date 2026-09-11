#include "LedgerCaves.h"
#include "LedgerScatter.h"

#include "Misc/CommandLine.h"

#include "LedgerBiome.h"
#include "LedgerClimate.h"
#include "LedgerPlanet.h"
#include "LedgerMath.h"

namespace
{
	/// A hash of a patch key and a cell, split into whatever uniforms are
	/// wanted.
	///
	/// Not FMath::Rand or a seeded stream: a stream has an order, and the order
	/// a patch is generated in is exactly what must not matter. This is a pure
	/// function of where the cell is, so two runs agree without having to agree
	/// about anything else.
	/// SplitMix64's finaliser: every input bit reaches every output bit.
	uint64 Mix(uint64 Z)
	{
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
		return Z ^ (Z >> 31);
	}

	/// **Mixed, not stirred.** This used to add the cell index in and run one
	/// multiply, so two neighbouring cells differed by a fixed multiple of the
	/// constant and their draws by a near-constant amount, about 0.999 mod
	/// one: the jitter, the keep-or-skip and the size all marched in step
	/// along each row of cells. That is the dotted lines of stones in every
	/// capture, and why capping the density below one never removed them.
	uint64 CellHash(uint64 Key, int32 Cell, uint32 Salt)
	{
		return Mix(Mix(Key) ^ Mix((static_cast<uint64>(static_cast<uint32>(Cell)) << 8) ^ Salt));
	}

	/// A uniform in [0,1) from a hash.
	double Uniform(uint64 Hash)
	{
		return static_cast<double>(Hash >> 11) / static_cast<double>(1ull << 53);
	}
}

namespace LedgerScatter
{
	/// The draw a cell makes, for the test that neighbouring cells draw
	/// independently. Same module, so no export.
	double CellUniform(uint64 Key, int32 Cell, uint32 Salt)
	{
		return Uniform(CellHash(Key, Cell, Salt));
	}

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
		// At least the grid: the skirts are appended after it.
		const bool bHasMesh = Job.Vertices.Num() >= Side * Side;
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

		// Where the land was cut for a cave mouth (T056): a grid quad that lost
		// either of its triangles is open. The land is cut triangle by triangle at
		// the centroid, so testing the cave field at a stone's own point left
		// stones on rim triangles cut around it, hanging over the hole. Each
		// triangle is counted to the quad its centroid is in, whichever way the
		// quads are split.
		static const bool bCaveMesh = FParse::Param(FCommandLine::Get(), TEXT("cavemesh"));
		TArray<uint8> QuadTriangles;
		if (bCaveMesh && bHasMesh)
		{
			QuadTriangles.SetNumZeroed(Side * Side);
			for (int32 Triangle = 0; Triangle + 2 < Job.Triangles.Num(); Triangle += 3)
			{
				const int32 I0 = Job.Triangles[Triangle];
				const int32 I1 = Job.Triangles[Triangle + 1];
				const int32 I2 = Job.Triangles[Triangle + 2];
				if (FMath::Max3(I0, I1, I2) >= Side * Side)
				{
					continue;
				}
				const int32 QX = FMath::FloorToInt32((I0 % Side + I1 % Side + I2 % Side) / 3.0);
				const int32 QY = FMath::FloorToInt32((I0 / Side + I1 / Side + I2 / Side) / 3.0);
				++QuadTriangles[QY * Side + QX];
			}
		}

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
				// Not over a cave mouth (T056): not in a quad the land was cut
				// from. A stone placed on the height field there hung in the air
				// over the hole -- the walk's photographs of an open passage had a
				// sky full of floating rocks.
				if (QuadTriangles.Num() > 0)
				{
					const int32 QX = FMath::Clamp(FMath::FloorToInt32(LocalU * (Side - 1)), 0, Side - 2);
					const int32 QY = FMath::Clamp(FMath::FloorToInt32(LocalV * (Side - 1)), 0, Side - 2);
					if (QuadTriangles[QY * Side + QX] < 2)
					{
						continue;
					}
				}
				else if (bCaveMesh && LedgerCaves::Density(Direction, Elevation / 100.0, Elevation / 100.0, Job.Params) > 0.0)
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
				// **And on the drawn surface, a quad away.** The function a cell away
				// is smoother than the mesh it is drawn as, and the stone stands on the
				// mesh: traced from above, 11,676 of 125,669 rainforest stones stood on
				// drawn ground of 30 to 45 degrees and 329 on steeper -- which, seen
				// side-on across a hillside, is a column of boulders climbing it.
				double DrawnSlopeDegrees = 0.0;
				if (bHasMesh)
				{
					const double Du = 1.0 / (Side - 1);
					// The steeper of the quads either side, on each axis: a stone
					// sits on one triangle, and a one-sided difference averaged the
					// face it stands on with the ground beside it -- the first run of
					// this still left 165 rainforest stones on 45-60 degrees.
					const double Here = MeshRadiusAt(LocalU, LocalV);
					const double AlongU = FMath::Max(
						FMath::Abs(MeshRadiusAt(FMath::Min(LocalU + Du, 1.0), LocalV) - Here),
						FMath::Abs(Here - MeshRadiusAt(FMath::Max(LocalU - Du, 0.0), LocalV)));
					const double AlongV = FMath::Max(
						FMath::Abs(MeshRadiusAt(LocalU, FMath::Min(LocalV + Du, 1.0)) - Here),
						FMath::Abs(Here - MeshRadiusAt(LocalU, FMath::Max(LocalV - Du, 0.0))));
					DrawnSlopeDegrees = FMath::RadiansToDegrees(FMath::Atan2(
						FVector2d(AlongU, AlongV).Length() / 100.0, PatchMetres / (Side - 1)));
				}
				if (FMath::Max(SlopeDegrees, DrawnSlopeDegrees) > MaxSlopeDegrees)
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

				// **Plants, on sites of their own.** Not the stone's draw: a cell holds
				// at most one stone, and undergrowth is many things close together. The
				// dominant biome says what grows and the blend says how much, so a
				// boundary thins out rather than switching species along a line.
				double PlantDensity = 0.0;
				int32 Dominant = 0;
				for (int32 Index = 0; Index < Weights.Num(); ++Index)
				{
					PlantDensity += Weights[Index] * (*Biomes)[Index].PlantDensity;
					Dominant = Weights[Index] > Weights[Dominant] ? Index : Dominant;
				}
				PlantDensity *= 1.0 - 0.8 * LedgerClimate::SnowCover(Climate);
				const TArray<uint8>& Kinds = (*Biomes)[Dominant].Plants;
				for (int32 Site = 0; bHasMesh && PlantDensity > 0.0 && Kinds.Num() > 0 && Site < PlantSites * PlantSites; ++Site)
				{
					const uint64 SiteHash = CellHash(Job.Key, Cell, 16u + static_cast<uint32>(Site));
					if (Uniform(SiteHash) >= PlantDensity)
					{
						continue;
					}
					const uint8 Kind = Kinds[static_cast<int32>(Mix(SiteHash) % static_cast<uint64>(Kinds.Num()))];
					const double SiteU = (CellU + ((Site % PlantSites) + Uniform(Mix(SiteHash ^ 1ull))) / PlantSites) / Cells;
					const double SiteV = (CellV + ((Site / PlantSites) + Uniform(Mix(SiteHash ^ 2ull))) / PlantSites) / Cells;
					for (int32 Stem = 0; Stem < PlantClump[Kind - 1]; ++Stem)
					{
						const uint64 StemHash = Mix(SiteHash + 3ull + static_cast<uint64>(Stem));
						// A tuft is a metre and a half across, not a cell.
						const double Spread = Stem == 0 ? 0.0 : 150.0 / Job.WorldSize;
						const double StemU = FMath::Clamp(SiteU + (Uniform(Mix(StemHash ^ 1ull)) - 0.5) * 2.0 * Spread, 0.0, 1.0);
						const double StemV = FMath::Clamp(SiteV + (Uniform(Mix(StemHash ^ 2ull)) - 0.5) * 2.0 * Spread, 0.0, 1.0);
						if (QuadTriangles.Num() > 0)
						{
							const int32 QX = FMath::Clamp(FMath::FloorToInt32(StemU * (Side - 1)), 0, Side - 2);
							const int32 QY = FMath::Clamp(FMath::FloorToInt32(StemV * (Side - 1)), 0, Side - 2);
							if (QuadTriangles[QY * Side + QX] < 2)
							{
								continue;
							}
						}
						const double StemRadius = MeshRadiusAt(StemU, StemV);
						if (StemRadius <= RadiusCm)
						{
							continue;
						}
						const FVector3d Up = LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(Job.Face,
							Job.U + StemU * Job.Extent, Job.V + StemV * Job.Extent));
						const double StemYaw = Uniform(Mix(StemHash ^ 3ull)) * LedgerTwoPi;
						FLedgerScatterInstance Plant;
						Plant.Position = FVector3f(Up * StemRadius - Job.Centre);
						// Upright, not leaning with the slope: a plant grows towards the
						// light whatever the ground under it does.
						Plant.Rotation = FQuat4f(FRotationMatrix::MakeFromZX(FVector(Up),
							FVector(East * FMath::Cos(StemYaw) + North * FMath::Sin(StemYaw))).ToQuat());
						Plant.Scale = static_cast<float>(0.7 + 0.6 * Uniform(Mix(StemHash ^ 4ull)));
						Plant.Variant = static_cast<uint8>(Mix(StemHash ^ 5ull) & 0xFFull);
						Plant.Kind = Kind;
						Job.Scatter.Add(Plant);
					}
				}

				// **Capped below one, and that is what stops the grid.**
				//
				// There is jitter inside each cell already, and it is not
				// enough on its own: a cell is filled when a uniform draw falls
				// under the density, so a biome asking for 1.0 fills every cell
				// on the grid and jitter inside a full lattice is still a
				// lattice. The rainforest capture in T437 is rows of stones
				// marching in step across a hillside, from a rule that has had
				// anti-grid jitter in it the whole time.
				//
				// At 0.55 nearly half the cells are empty and the gaps are
				// where the pattern goes. Density below that is untouched, so
				// the sparse biomes this was tuned on do not move.
				constexpr double MaxFill = 0.55;
				if (Uniform(CellHash(Job.Key, Cell, 3u)) >= FMath::Min(Density, MaxFill))
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

				const double Yaw = Uniform(CellHash(Job.Key, Cell, 4u)) * LedgerTwoPi;
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
