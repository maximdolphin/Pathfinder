#include "LedgerPatchGenerator.h"

#include "LedgerBiome.h"
#include "LedgerCaveMesh.h"
#include "LedgerPatchDisk.h"
#include "LedgerTerrainVis.h"
#include "LedgerCaves.h"
#include "LedgerLog.h"
#include "LedgerScatter.h"
#include "Misc/CommandLine.h"
#include "LedgerClimate.h"
#include "LedgerPlanet.h"
#include "LedgerTerrainMath.h"

namespace
{
	/// Alpha is SNOW COVER, not opacity, and this returns none of it.
	///
	/// It returned 255. Alpha carries snow cover for the material to read
	/// (T060), so a patch coloured by this path -- the fallback, used whenever
	/// there are no biomes loaded -- claimed full snow everywhere, and the snow
	/// overlay believed it. Nothing read the channel until the overlay existed,
	/// which is why a wrong constant sat here harmlessly for two milestones.
	///
	/// Zero is the honest answer: with no biomes there is no climate to ask,
	/// and unknown cover is better rendered as none than as a metre of it.
	FColor Blend(const FColor& A, const FColor& B, double T)
	{
		const double Alpha = FMath::Clamp(T, 0.0, 1.0);
		return FColor(
			static_cast<uint8>(FMath::Lerp<double>(A.R, B.R, Alpha)),
			static_cast<uint8>(FMath::Lerp<double>(A.G, B.G, Alpha)),
			static_cast<uint8>(FMath::Lerp<double>(A.B, B.B, Alpha)),
			0);
	}

	/// Surface colour from height and steepness.
	///
	/// **Superseded by the biome weights, and kept for the case where no biomes
	/// load.** A fresh clone with an empty Config/Biomes still gets a planet
	/// that reads as terrain rather than as flat white, and the fallback is
	/// visibly a colour ramp, so nobody mistakes it for the real thing.
	///
	/// Deliberately dark: these are albedos, and albedo near 1.0 blows out the
	/// moment a sun hits it. Slope matters as much as height — a cliff is bare
	/// rock at any altitude, and keying colour on height alone gives the banded
	/// contour-map look that reads as procedural immediately.
	FColor SurfaceColour(double Elevation, double MaxElevation, double Steepness)
	{
		if (Elevation < 0.0)
		{
			const double Depth = FMath::Clamp(-Elevation / (MaxElevation * 0.45), 0.0, 1.0);
			return Blend(FColor(28, 62, 84), FColor(6, 16, 34), FMath::Pow(Depth, 0.6));
		}

		const double Height = FMath::Clamp(Elevation / MaxElevation, 0.0, 1.0);

		FColor Ground;
		if (Height < 0.015)
		{
			Ground = Blend(FColor(126, 116, 92), FColor(74, 88, 52), Height / 0.015);         // beach -> grass
		}
		else if (Height < 0.16)
		{
			Ground = Blend(FColor(96, 108, 66), FColor(76, 88, 54), (Height - 0.015) / 0.145); // grass -> forest
		}
		else if (Height < 0.42)
		{
			Ground = Blend(FColor(76, 88, 54), FColor(104, 94, 72), (Height - 0.16) / 0.26);  // forest -> scrub
		}
		else if (Height < 0.68)
		{
			Ground = Blend(FColor(84, 76, 60), FColor(96, 92, 88), (Height - 0.42) / 0.26);   // scrub -> rock
		}
		else
		{
			Ground = Blend(FColor(96, 92, 88), FColor(206, 210, 216), (Height - 0.68) / 0.32); // rock -> snow
		}

		// Steep ground is bare. Snow does not hold on a cliff either, so this is
		// applied last and wins.
		const double Bare = FMath::SmoothStep(0.45, 0.78, Steepness);
		return Blend(Ground, FColor(78, 72, 66), Bare);
	}
}

namespace
{
	/// How many climate samples across a patch. Three, on a 2x2 cell grid.
	///
	/// **Because climate is expensive, and elevation is the reason.** One
	/// LedgerClimate::At marches forty steps upwind and samples the height
	/// field at every one, so a climate sample costs forty times what a vertex
	/// costs. Per vertex on a 33x33 patch that is 43,560 height samples against
	/// the patch's own 1,089 -- a fortyfold increase in the one cost this
	/// project has spent the most effort measuring.
	///
	/// Nine samples bilinearly interpolated cost 360, about a third on top. The
	/// approximation is worst in the middle and harmless at both ends: at fine
	/// LOD a patch spans metres and climate really is constant across it, at
	/// coarse LOD the patch is far enough away that a smooth gradient is right
	/// anyway. Corner samples sit at the patch's true corners, so two patches
	/// at the same depth agree exactly along their shared edge.
	///
	/// Slope is NOT interpolated. It is per vertex, and it is what makes a
	/// cliff bare rock in the middle of a rainforest.
	///
	/// **Scaled with the patch, because "climate is constant across it" is only
	/// true of a small one.** Three samples across a root patch is three across
	/// a whole cube face, and the orbit and space captures came back with
	/// flat-coloured continents: correct interpolation of a grid far too coarse
	/// to have a biome boundary in it. A patch's grid is now proportional to
	/// its extent, so the handful of coarse patches pay for a real sampling and
	/// the thousands of fine ones still pay for three.
	int32 ClimateGridFor(double Extent)
	{
		return FMath::Clamp(FMath::RoundToInt32(3.0 + Extent * 20.0), 3, 13);
	}
}

void LedgerGeneratePatch(FLedgerPatchJob& Job)
{
	const double Started = FPlatformTime::Seconds();

	const int32 Side = FMath::Max(3, Job.Side | 1); // force odd
	const int32 VertexCount = Side * Side;
	const double Inverse = 1.0 / static_cast<double>(Side - 1);

	Job.Vertices.SetNumUninitialized(VertexCount);
	Job.Normals.SetNumZeroed(VertexCount);
	Job.UVs.SetNumUninitialized(VertexCount);
	Job.MorphUVs.SetNumUninitialized(VertexCount);
	Job.Colors.SetNumUninitialized(VertexCount);
	Job.Tangents.SetNumUninitialized(VertexCount);

	// ---- the disk cache ---------------------------------------------------
	//
	// Everything expensive about this patch, if this machine has built it
	// before: the elevation grid, the vertex colours the climate produced, the
	// palette and the scatter. What is left below is arithmetic.
	// What this patch can resolve, which decides how much of the near-field
	// detail band it is allowed to see (T429). WorldSize is centimetres across
	// the whole patch; Side vertices leave Side-1 gaps.
	const double SpacingMetres =
		(Job.WorldSize / 100.0) / FMath::Max(1, FMath::Max(3, Job.Side | 1) - 1);

	const bool bFromDisk = LedgerPatchDisk::Load(Job);

	TArray<double>& Elevations = Job.Elevations;
	if (!bFromDisk)
	{
		Elevations.SetNumUninitialized(VertexCount);
	}

	for (int32 Y = 0; Y < Side; ++Y)
	{
		for (int32 X = 0; X < Side; ++X)
		{
			double LocalU = static_cast<double>(X) * Inverse;
			double LocalV = static_cast<double>(Y) * Inverse;

			const double U = Job.U + LocalU * Job.Extent;
			const double V = Job.V + LocalV * Job.Extent;
			const FVector3d UnitSphere = LedgerTerrain::CubeToSphere(
				LedgerTerrain::FaceToCube(Job.Face, U, V));

			const int32 Index = Y * Side + X;

			// **A stitched edge is sampled at its neighbour's spacing.** The height
			// depends on the spacing -- the near-field band a patch may carry is
			// decided by what it can resolve (T429) -- so a vertex this patch and
			// its coarser neighbour share was two different heights, one each,
			// and putting the in-between vertices on the neighbour's line could not
			// close an edge whose end points already disagreed. On a stitched edge
			// the shared vertices take the neighbour's spacing, so they are the
			// neighbour's vertices exactly; a corner on two stitched edges takes
			// the coarser of the two.
			uint8 EdgeLevel = 0;
			if (X == 0) { EdgeLevel = FMath::Max(EdgeLevel, Job.StitchLeft); }
			if (X == Side - 1) { EdgeLevel = FMath::Max(EdgeLevel, Job.StitchRight); }
			if (Y == 0) { EdgeLevel = FMath::Max(EdgeLevel, Job.StitchBottom); }
			if (Y == Side - 1) { EdgeLevel = FMath::Max(EdgeLevel, Job.StitchTop); }
			const double VertexSpacing = SpacingMetres * static_cast<double>(1 << FMath::Min<int32>(EdgeLevel, 16));

			// The one line the whole disk cache exists for.
			const double Elevation = bFromDisk
				? Elevations[Index]
				: LedgerTerrain::Elevation(UnitSphere, Job.Params, VertexSpacing);
			const FVector3d Surface = UnitSphere * (Job.Params.Radius + Elevation);
			// Relative to the node centre: this is what keeps float precision
			// local, and it is why the same code works at planetary scale.
			Job.Vertices[Index] = FVector(Surface - Job.Centre);
			Job.UVs[Index] = FVector2D(LocalU, LocalV);
			if (!bFromDisk)
			{
				Elevations[Index] = Elevation;
			}
		}
	}

	// ---- edge stitching --------------------------------------------------
	//
	// **Onto the coarser neighbour's edge, however much coarser it is.** A
	// neighbour k levels coarser has a vertex every 2^k of ours along the
	// shared edge, and between two of them its edge is a straight line, so
	// every vertex of ours between two it keeps is put on that line -- by
	// position and by elevation, so the morph targets below agree. The old
	// stitch collapsed odd onto even, which is one level and nothing more,
	// and T049 measured what it left beside a neighbour six levels coarser:
	// 37,608 edge vertices more than a centimetre off, the worst 41 km. It
	// also left a degenerate triangle per odd vertex; this leaves none.
	auto StitchEdge = [&Job, &Elevations, Side](uint8 Level, auto IndexAt)
	{
		if (Level == 0)
		{
			return;
		}
		const int32 Step = FMath::Min(1 << FMath::Min<int32>(Level, 16), Side - 1);
		for (int32 I = 0; I < Side; ++I)
		{
			const int32 Offset = I % Step;
			if (Offset == 0)
			{
				continue;
			}
			const int32 J0 = I - Offset;
			const int32 J1 = FMath::Min(J0 + Step, Side - 1);
			const double T = static_cast<double>(I - J0) / static_cast<double>(J1 - J0);
			const int32 A = IndexAt(J0);
			const int32 B = IndexAt(J1);
			const int32 C = IndexAt(I);
			Job.Vertices[C] = FMath::Lerp(Job.Vertices[A], Job.Vertices[B], T);
			Elevations[C] = FMath::Lerp(Elevations[A], Elevations[B], T);
		}
	};
	StitchEdge(Job.StitchLeft, [Side](int32 I) { return I * Side; });
	StitchEdge(Job.StitchRight, [Side](int32 I) { return I * Side + Side - 1; });
	StitchEdge(Job.StitchBottom, [](int32 I) { return I; });
	StitchEdge(Job.StitchTop, [Side](int32 I) { return (Side - 1) * Side + I; });

	// ---- geomorph targets ------------------------------------------------
	//
	// A vertex at an odd grid position does not exist in the parent LOD. When
	// this node collapses, that vertex vanishes and its neighbourhood snaps to
	// whatever the parent's coarser sampling says — which is the pop.
	//
	// So each vertex records where the parent *would* have put it: even ones do
	// not move, edge-odd ones sit at the midpoint of their two even neighbours,
	// and centre-odd ones at the average of the four surrounding evens. The
	// shader blends toward that as the node approaches its collapse threshold,
	// so by the moment it collapses the two meshes already agree.
	for (int32 Y = 0; Y < Side; ++Y)
	{
		for (int32 X = 0; X < Side; ++X)
		{
			const int32 Index = Y * Side + X;
			const bool bOddX = (X & 1) != 0;
			const bool bOddY = (Y & 1) != 0;

			double Coarse = Elevations[Index];
			if (bOddX && bOddY)
			{
				Coarse = 0.25 * (Elevations[(Y - 1) * Side + (X - 1)]
					+ Elevations[(Y - 1) * Side + (X + 1)]
					+ Elevations[(Y + 1) * Side + (X - 1)]
					+ Elevations[(Y + 1) * Side + (X + 1)]);
			}
			else if (bOddX)
			{
				Coarse = 0.5 * (Elevations[Y * Side + (X - 1)] + Elevations[Y * Side + (X + 1)]);
			}
			else if (bOddY)
			{
				Coarse = 0.5 * (Elevations[(Y - 1) * Side + X] + Elevations[(Y + 1) * Side + X]);
			}

			Job.MorphUVs[Index] = FVector2D(Coarse - Elevations[Index], Job.WorldSize);
		}
	}

	Job.Triangles.Reset((Side - 1) * (Side - 1) * 6);
	for (int32 Y = 0; Y < Side - 1; ++Y)
	{
		for (int32 X = 0; X < Side - 1; ++X)
		{
			const int32 A = Y * Side + X;
			const int32 B = A + 1;
			const int32 C = A + Side;
			const int32 D = C + 1;
			Job.Triangles.Add(A); Job.Triangles.Add(C); Job.Triangles.Add(B);
			Job.Triangles.Add(B); Job.Triangles.Add(C); Job.Triangles.Add(D);
		}
	}

	// ---- holes where caves reach the surface ------------------------------
	//
	// **A height field cannot have a hole, so the hole is a deletion.** The
	// surface is generated as if there were no caves and then the triangles
	// standing where a mouth is are dropped. That is the whole trick: the cave
	// mesh supplies the walls beyond, and the two meet at the edge of the hole
	// because both are level sets of the same field.
	//
	// Off by default while the volumetric layer has no LOD story, so the
	// terrain everything else measures against is untouched. -cavemesh turns it
	// on.
	static const bool bCaveMesh = FParse::Param(FCommandLine::Get(), TEXT("cavemesh"));
	if (bCaveMesh && LedgerCaves::ShouldMeshCaves(Job))
	{
		const double RadiusCm = Job.Params.Radius;
		TArray<int32> Kept;
		Kept.Reserve(Job.Triangles.Num());
		for (int32 Triangle = 0; Triangle + 2 < Job.Triangles.Num(); Triangle += 3)
		{
			const int32 I0 = Job.Triangles[Triangle];
			const int32 I1 = Job.Triangles[Triangle + 1];
			const int32 I2 = Job.Triangles[Triangle + 2];

			const FVector Centroid =
				(Job.Vertices[I0] + Job.Vertices[I1] + Job.Vertices[I2]) / 3.0;
			const FVector3d World = FVector3d(Centroid) + Job.Centre;
			const FVector3d Direction = World.GetSafeNormal();
			const double GroundMetres =
				(Elevations[I0] + Elevations[I1] + Elevations[I2]) / 300.0;
			const double AltitudeMetres = (World.Length() - RadiusCm) / 100.0;

			if (LedgerCaves::Density(Direction, AltitudeMetres, GroundMetres, Job.Params) > 0.0)
			{
				continue;
			}
			Kept.Add(I0); Kept.Add(I1); Kept.Add(I2);
		}
		Job.Triangles = MoveTemp(Kept);

		LedgerCaves::GenerateCaveMesh(Job);
		Job.bHasCaves = Job.CaveTriangles.Num() > 0;
		if (Job.bHasCaves)
		{
			UE_LOG(LogLedger, Verbose, TEXT("cave patch %llu: %d vertices, %d triangles"),
				Job.Key, Job.CaveVertices.Num(), Job.CaveTriangles.Num() / 3);
		}
	}

	// Normals by accumulating face normals. Linear in triangle count, and far
	// cheaper than `CalculateTangentsForMesh`, which was most of the old
	// per-patch cost and produced no better result for an untextured surface.
	for (int32 Triangle = 0; Triangle < Job.Triangles.Num(); Triangle += 3)
	{
		const int32 I0 = Job.Triangles[Triangle];
		const int32 I1 = Job.Triangles[Triangle + 1];
		const int32 I2 = Job.Triangles[Triangle + 2];
		const FVector Edge1 = Job.Vertices[I1] - Job.Vertices[I0];
		const FVector Edge2 = Job.Vertices[I2] - Job.Vertices[I0];
		const FVector FaceNormal = FVector::CrossProduct(Edge2, Edge1);
		Job.Normals[I0] += FaceNormal;
		Job.Normals[I1] += FaceNormal;
		Job.Normals[I2] += FaceNormal;
	}

	// The node's own up vector, for the steepness term. Vertices are relative to
	// the centre, so the centre's direction is the local vertical.
	const FVector LocalUp = FVector(Job.Centre.GetSafeNormal());

	// ---- climate ---------------------------------------------------------
	//
	// Sampled on the coarse grid and interpolated. See ClimateGrid.
	const TArray<FLedgerBiome>* Biomes = Job.Biomes.IsValid() ? Job.Biomes.Get() : nullptr;

	// Skipped entirely when the colours came off disk: every one of those
	// climate samples marches forty steps upwind through the height field, and
	// they are the second half of what a patch costs.
	const bool bBiomes = !bFromDisk && Biomes != nullptr && Biomes->Num() > 0;

	const int32 ClimateGrid = ClimateGridFor(Job.Extent);
	TArray<FLedgerClimate> Grid;
	if (bBiomes)
	{
		Grid.SetNumUninitialized(ClimateGrid * ClimateGrid);
		for (int32 GridY = 0; GridY < ClimateGrid; ++GridY)
		{
			for (int32 GridX = 0; GridX < ClimateGrid; ++GridX)
			{
				const double GridU = Job.U
					+ (static_cast<double>(GridX) / (ClimateGrid - 1)) * Job.Extent;
				const double GridV = Job.V
					+ (static_cast<double>(GridY) / (ClimateGrid - 1)) * Job.Extent;
				Grid[GridY * ClimateGrid + GridX] = LedgerClimate::At(
					LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(Job.Face, GridU, GridV)),
					Job.Params, Job.SeasonPhase);
			}
		}
	}

	auto ClimateAt = [&Grid, ClimateGrid](double LocalU, double LocalV)
	{
		const double GridU = FMath::Clamp(LocalU, 0.0, 1.0) * (ClimateGrid - 1);
		const double GridV = FMath::Clamp(LocalV, 0.0, 1.0) * (ClimateGrid - 1);
		const int32 X0 = FMath::Clamp(FMath::FloorToInt32(GridU), 0, ClimateGrid - 2);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(GridV), 0, ClimateGrid - 2);
		const double FracX = GridU - X0;
		const double FracY = GridV - Y0;

		auto Mix = [&Grid, ClimateGrid, X0, Y0, FracX, FracY](double FLedgerClimate::* Field)
		{
			const double A = Grid[Y0 * ClimateGrid + X0].*Field;
			const double B = Grid[Y0 * ClimateGrid + X0 + 1].*Field;
			const double C = Grid[(Y0 + 1) * ClimateGrid + X0].*Field;
			const double D = Grid[(Y0 + 1) * ClimateGrid + X0 + 1].*Field;
			return FMath::Lerp(FMath::Lerp(A, B, FracX), FMath::Lerp(C, D, FracX), FracY);
		};

		FLedgerClimate Out;
		Out.SeaLevelTemperatureC = Mix(&FLedgerClimate::SeaLevelTemperatureC);
		Out.Moisture = Mix(&FLedgerClimate::Moisture);
		return Out;
	};

	// Weights per vertex, and the patch totals the palette is chosen from.
	TArray<double> PatchTotals;
	TArray<TArray<double>> VertexWeights;
	TArray<float> VertexSnow;
	if (bBiomes)
	{
		PatchTotals.SetNumZeroed(Biomes->Num());
		VertexWeights.SetNum(VertexCount);
		VertexSnow.SetNumZeroed(VertexCount);
	}

	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		FVector Normal = Job.Normals[Index].GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			Normal = LocalUp;
		}
		Job.Normals[Index] = Normal;

		const double Steepness = 1.0 - FMath::Clamp(
			static_cast<double>(FVector::DotProduct(Normal, LocalUp)), 0.0, 1.0);

		if (!bBiomes)
		{
			Job.Colors[Index] = SurfaceColour(
				Elevations[Index], Job.Params.MaxElevation, Steepness);
			// No snow overlay on the fallback ramp. Alpha is snow cover everywhere
			// else in this project (T060), and Blend hands back FColor's default
			// alpha of 255 -- full snow -- on every patch that has no biomes. The
			// ramp carries its own white at the top already.
			Job.Colors[Index].A = 0;
		}
		else
		{
			// Temperature is recomputed from this vertex's own altitude rather
			// than interpolated: the lapse rate is the term relief moves most,
			// and interpolating it would flatten every mountain the biome map
			// is supposed to notice.
			FLedgerClimate Climate = ClimateAt(Job.UVs[Index].X, Job.UVs[Index].Y);
			Climate.AltitudeMetres = Elevations[Index] / 100.0;
			Climate.TemperatureC = Climate.SeaLevelTemperatureC
				- LedgerClimate::LapseRateCPerKm
					* FMath::Max(0.0, Climate.AltitudeMetres) / 1000.0;

			const double SlopeDegrees = FMath::RadiansToDegrees(
				FMath::Acos(FMath::Clamp(1.0 - Steepness, -1.0, 1.0)));
			LedgerBiomes::Weigh(*Biomes, Climate, SlopeDegrees, VertexWeights[Index]);

			// Snow does not lie on a cliff. The same slope the biomes are
			// weighed by sheds it, which is why the shed is here and not in
			// SnowCover -- that function is about climate, and a cliff is not
			// a climate.
			const double Sheds = FMath::GetMappedRangeValueClamped(
				FVector2d(45.0, 62.0), FVector2d(1.0, 0.0), SlopeDegrees);
			VertexSnow[Index] = static_cast<float>(
				LedgerClimate::SnowCover(Climate) * Sheds);
			for (int32 Biome = 0; Biome < PatchTotals.Num(); ++Biome)
			{
				PatchTotals[Biome] += VertexWeights[Index][Biome];
			}
		}

		// A tangent perpendicular to the normal. Nothing samples a normal map
		// yet, but ProcMesh wants the channel and a degenerate basis shows up as
		// black shading the moment something does.
		const FVector Reference = FMath::Abs(Normal.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
		Job.Tangents[Index] = FProcMeshTangent(
			FVector::CrossProduct(Reference, Normal).GetSafeNormal(), false);
	}

	if (bBiomes)
	{
		// The palette is the patch's own three heaviest biomes, so whichever it
		// leaves out is the least present on it. Two neighbouring patches can
		// still choose differently, and where they do the discontinuity is
		// bounded by the weight of the biome one of them dropped -- which is,
		// by construction, below the three it kept.
		// `-onepalette` gives every patch on the planet the same three biomes.
		//
		// The control arm for the seams. A palette chosen per patch means two
		// neighbours can drop different biomes, and where they do the ground
		// changes composition along a straight line -- which is what a patch
		// boundary is. Rectangular seams appeared in the first T053 captures
		// and that is the obvious suspect, but "obvious suspect" is how this
		// project has been wrong four times already. With one palette
		// everywhere the palette cannot be the cause; if the seams survive,
		// they are somebody else's.
		static const bool bOnePalette =
			FParse::Param(FCommandLine::Get(), TEXT("onepalette"));
		if (bOnePalette)
		{
			Job.Palette.Slots[0] = 0;
			Job.Palette.Slots[1] = FMath::Min(1, Biomes->Num() - 1);
			Job.Palette.Slots[2] = FMath::Min(2, Biomes->Num() - 1);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("patchpalette")))
		{
			// The other control arm: each patch picks its own three. This is
			// what T053 shipped first and it seams along patch boundaries.
			// Kept so the comparison in docs/comparisons/biome-surfaces/ is a
			// command line rather than a pair of images somebody has to trust.
			Job.Palette = LedgerBiomes::ChoosePalette(PatchTotals);
		}
		else
		{
			// The palette belongs to a cell, not to a patch.
			//
			// **A patch cannot choose its own three.** Two neighbours whose own
			// totals rank the biomes differently keep different triples, and
			// along their shared edge the ground changes composition in a
			// straight line -- a seam exactly as long as a patch is wide. The
			// captures showed them, a one-palette control made them vanish, and
			// truncating the weight field to three (LedgerBiome.cpp) did not:
			// the problem is not which weights are dropped, it is that the two
			// patches drop different *biomes*.
			//
			// So the choice is made for a fixed cell of the cube face that both
			// patches lie inside, from climate sampled at that cell's corners.
			// Every patch in the cell gets the same three, so there is no seam
			// inside one; the seams that remain are on cell lines, which are
			// six hundred kilometres apart and fall where the palette genuinely
			// changes. It is not free of them, and the write-up says so.
			constexpr double CellExtent = 1.0 / 16.0;
			const double CellU = FMath::Floor(Job.U / CellExtent) * CellExtent;
			const double CellV = FMath::Floor(Job.V / CellExtent) * CellExtent;

			TArray<double> CellTotals;
			CellTotals.SetNumZeroed(Biomes->Num());
			TArray<double> CellWeights;
			for (int32 CellY = 0; CellY <= 4; ++CellY)
			{
				for (int32 CellX = 0; CellX <= 4; ++CellX)
				{
					const FVector3d Point = LedgerTerrain::CubeToSphere(
						LedgerTerrain::FaceToCube(Job.Face,
							CellU + (CellX / 4.0) * CellExtent,
							CellV + (CellY / 4.0) * CellExtent));
					LedgerBiomes::Weigh(
						*Biomes, LedgerClimate::At(Point, Job.Params), 0.0, CellWeights);
					for (int32 Biome = 0; Biome < CellTotals.Num(); ++Biome)
					{
						CellTotals[Biome] += CellWeights[Biome];
					}
				}
			}
			Job.Palette = LedgerBiomes::ChoosePalette(CellTotals);
		}
		for (int32 Index = 0; Index < VertexCount; ++Index)
		{
			const FVector3f Slots = LedgerBiomes::SlotWeights(VertexWeights[Index], Job.Palette);

			// Alpha is snow, which was the one channel nobody was using.
			//
			// Snow is not a biome: it lies on top of whichever ground is there,
			// and giving it a palette slot would have cost a biome and made a
			// snowy forest and a snowy desert the same place. As a fourth
			// channel it is an overlay, which is what it is.
			Job.Colors[Index] = FColor(
				static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Slots.X, 0.0f, 1.0f) * 255.0f)),
				static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Slots.Y, 0.0f, 1.0f) * 255.0f)),
				static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Slots.Z, 0.0f, 1.0f) * 255.0f)),
				static_cast<uint8>(FMath::RoundToInt(
					FMath::Clamp(VertexSnow[Index], 0.0f, 1.0f) * 255.0f)));
		}
	}

	// ---- scatter ---------------------------------------------------------
	//
	// On the worker, with the patch, because placement needs the height field
	// and doing it on the game thread would be a second sampling pass in the
	// frame. Declines immediately for any patch without collision.
	if (!bFromDisk)
	{
		LedgerScatter::ScatterPatch(Job);
	}

	// ---- sea surface ----------------------------------------------------
	//
	// Sea level is exactly the reference sphere: `Elevation` returns negative
	// below the waterline, so the shell sits at `Radius` plus a couple of
	// centimetres to keep it off the terrain at the shoreline, where the two
	// meet at the same height by definition and would otherwise z-fight.
	constexpr double WaterLift = 3.0;

	bool bAnyWater = false;
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		if (Elevations[Index] < 0.0)
		{
			bAnyWater = true;
			break;
		}
	}

	if (bAnyWater)
	{
		Job.bHasWater = true;
		Job.WaterVertices.SetNumUninitialized(VertexCount);
		Job.WaterNormals.SetNumUninitialized(VertexCount);
		Job.WaterUVs.SetNumUninitialized(VertexCount);
		Job.WaterColors.SetNumUninitialized(VertexCount);
		Job.WaterTangents.SetNumUninitialized(VertexCount);

		const FVector LocalUpForWater = FVector(Job.Centre.GetSafeNormal());

		for (int32 Y = 0; Y < Side; ++Y)
		{
			for (int32 X = 0; X < Side; ++X)
			{
				const int32 Index = Y * Side + X;
				const double LocalU = static_cast<double>(X) * Inverse;
				const double LocalV = static_cast<double>(Y) * Inverse;
				const FVector3d UnitSphere = LedgerTerrain::CubeToSphere(
					LedgerTerrain::FaceToCube(
						Job.Face, Job.U + LocalU * Job.Extent, Job.V + LocalV * Job.Extent));

				const FVector3d Surface = UnitSphere * (Job.Params.Radius + WaterLift);
				Job.WaterVertices[Index] = FVector(Surface - Job.Centre);
				Job.WaterUVs[Index] = FVector2D(LocalU, LocalV);
				// The sea is a sphere, so its normal is the radial direction.
				Job.WaterNormals[Index] = FVector(UnitSphere);

				const FVector Reference = FMath::Abs(Job.WaterNormals[Index].Z) < 0.9f
					? FVector::UpVector : FVector::ForwardVector;
				Job.WaterTangents[Index] = FProcMeshTangent(
					FVector::CrossProduct(Reference, Job.WaterNormals[Index]).GetSafeNormal(), false);

				// Two different depth encodings, because the two consumers need
				// wildly different ranges.
				//
				// Alpha is depth over kilometres, for the colour gradient from
				// shallows to open ocean. Red is depth over the first three
				// metres, for waves and foam: on the alpha scale, standing in
				// knee-deep water reads as 0.02 and everything at the shoreline
				// is indistinguishable from everything else.
				const double Depth = FMath::Clamp(
					-Elevations[Index] / (Job.Params.MaxElevation * 0.30), 0.0, 1.0);
				const double ShoreProximity = 1.0 - FMath::Clamp(
					-Elevations[Index] / 300.0, 0.0, 1.0);

				Job.WaterColors[Index] = FColor(
					static_cast<uint8>(ShoreProximity * 255.0),
					255,
					255,
					static_cast<uint8>(FMath::Pow(Depth, 0.5) * 255.0));
			}
		}

		// Only emit a quad where some corner is actually underwater. Without
		// this the sea is a continuous sheet laid over the continents.
		Job.WaterTriangles.Reset();
		for (int32 Y = 0; Y < Side - 1; ++Y)
		{
			for (int32 X = 0; X < Side - 1; ++X)
			{
				const int32 A = Y * Side + X;
				const int32 B = A + 1;
				const int32 C = A + Side;
				const int32 D = C + 1;

				if (Elevations[A] >= 0.0 && Elevations[B] >= 0.0 &&
					Elevations[C] >= 0.0 && Elevations[D] >= 0.0)
				{
					continue;
				}

				Job.WaterTriangles.Add(A); Job.WaterTriangles.Add(C); Job.WaterTriangles.Add(B);
				Job.WaterTriangles.Add(B); Job.WaterTriangles.Add(C); Job.WaterTriangles.Add(D);
			}
		}

		Job.bHasWater = Job.WaterTriangles.Num() > 0;
	}

	// Written after everything, so a half-generated patch is never stored --
	// and only when this run actually did the work, so a cache hit does not
	// rewrite the file it just read.
	if (!bFromDisk)
	{
		LedgerPatchDisk::Store(Job);
	}

	// The diagnostic, painted last and after the store.
	//
	// After the store deliberately: what goes on disk is the real patch, so a
	// run with -terrainvis does not poison the cache for the next run without
	// it. And after everything else, because it overwrites the colours the
	// climate produced and nothing downstream should still be reading them.
	if (LedgerTerrainVis::IsOn())
	{
		LedgerTerrainVis::Paint(Job, bFromDisk);
	}

	Job.GenerationMs = (FPlatformTime::Seconds() - Started) * 1000.0;
	// Release: everything written above is visible to whoever sees this true.
	Job.bComplete.store(true, std::memory_order_release);
}
