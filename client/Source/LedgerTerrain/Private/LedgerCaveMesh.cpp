#include "LedgerCaveMesh.h"

#include "LedgerCaves.h"
#include "LedgerPlanet.h"
#include "Misc/CommandLine.h"

namespace
{
	/// Rock, as a signed field: positive inside the rock, negative in open air.
	///
	/// Two conditions, and the minimum of them. Rock is below the ground *and*
	/// not inside a cave, so the wall a mesher has to find is the boundary of
	/// whichever of those two runs out first. Taking the minimum makes one field
	/// out of both, and its zero set is the cave wall where a cave is the
	/// nearer boundary and the ground surface where it is not -- which is
	/// exactly the mouth.
	double RockDensity(const FVector3d& Direction, double AltitudeMetres,
		double GroundMetres, const FLedgerTerrainParams& Params)
	{
		const double BelowGround = GroundMetres - AltitudeMetres;
		const double OutsideCave = -LedgerCaves::Density(
			Direction, AltitudeMetres, GroundMetres, Params);
		return FMath::Min(BelowGround, OutsideCave);
	}
}

namespace LedgerCaves
{
	bool ShouldMeshCaves(const FLedgerPatchJob& Job)
	{
		if (Job.WorldSize > MaxCaveWorldSize)
		{
			return false;
		}

		const FVector3d Centre = Job.Centre.GetSafeNormal();
		if (Centre.IsNearlyZero())
		{
			return false;
		}

		// A patch's own half-diagonal, generously. MightContainCaves must never
		// say no where a cave exists, so it is told the patch is bigger than it
		// is rather than smaller.
		return MightContainCaves(Centre, Job.WorldSize / 100.0, Job.Params);
	}

	void GenerateCaveMesh(FLedgerPatchJob& Job)
	{
		Job.CaveVertices.Reset();
		Job.CaveTriangles.Reset();
		Job.CaveNormals.Reset();
		Job.CaveUVs.Reset();

		// One sample beyond the brick on +X and +Y, so the cells on its far
		// edges have neighbours and the wall is joined across the boundary to
		// the next brick. Without it the last cell and the next brick's first
		// were never joined, and from inside a passage every brick seam was a
		// slit of sky -- chain114's first photographs inside a cave. The next
		// brick does not join its own first edge, so nothing is drawn twice.
		//
		// **And one before, on -X and -Y, so each brick joins its own first edge
		// too** (T056). Leaving that edge to the neighbour assumed the neighbour
		// reached it at the same cell size; where the neighbour is a finer patch
		// its strip stops half a coarse cell short, and down the walk's shadowed
		// wall that was a line of slits of sky, still there after the bricks
		// were given each other's altitude ranges. Both sides now draw the seam,
		// so a boundary overlaps by a cell rather than depending on its neighbour.
		// Lattice index X is corner X - 1 of the brick.
		const int32 Across = BrickAcross + 3;
		const double RadiusMetres = Job.Params.Radius / 100.0;

		// ---- the ground under the brick, on a coarse grid --------------------
		//
		// Elevation is the expensive call in this project and the brick wants
		// 33 x 33 x 25 samples of the rock field, each of which needs the local
		// ground. The ground over one patch is smooth, so it is sampled on a
		// 5 x 5 grid and interpolated -- the same trade the climate sampling
		// makes, for the same reason.
		constexpr int32 GroundGrid = 5;
		double Ground[GroundGrid][GroundGrid];
		for (int32 GroundY = 0; GroundY < GroundGrid; ++GroundY)
		{
			for (int32 GroundX = 0; GroundX < GroundGrid; ++GroundX)
			{
				const FVector3d Point = LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(
					Job.Face,
					Job.U + (GroundX / static_cast<double>(GroundGrid - 1)) * Job.Extent,
					Job.V + (GroundY / static_cast<double>(GroundGrid - 1)) * Job.Extent));
				Ground[GroundY][GroundX] =
					LedgerTerrain::Elevation(Point, Job.Params) / 100.0;
			}
		}

		auto GroundAt = [&Ground](double LocalU, double LocalV)
		{
			const double GridU = FMath::Clamp(LocalU, 0.0, 1.0) * (GroundGrid - 1);
			const double GridV = FMath::Clamp(LocalV, 0.0, 1.0) * (GroundGrid - 1);
			const int32 X0 = FMath::Clamp(FMath::FloorToInt32(GridU), 0, GroundGrid - 2);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(GridV), 0, GroundGrid - 2);
			const double FracX = GridU - X0;
			const double FracY = GridV - Y0;
			return FMath::Lerp(
				FMath::Lerp(Ground[Y0][X0], Ground[Y0][X0 + 1], FracX),
				FMath::Lerp(Ground[Y0 + 1][X0], Ground[Y0 + 1][X0 + 1], FracX), FracY);
		};

		// The brick hangs from the ground at the patch centre, so it is a slab
		// rather than a shape that follows every hillside. A passage crossing a
		// dip has to stay inside it.
		const double CentreGround = GroundAt(0.5, 0.5);
		const double CellDown = (ShellAboveMetres + BrickDepthMetres) / BrickDown;
		// On a lattice of altitude shared by every brick, so two neighbours sample
		// the same levels and meet along the same line rather than a cell apart.
		//
		// **And over its neighbours' ranges as well as its own.** Each brick hangs
		// from the ground at its own centre, so two side by side cover different
		// altitudes, and the seam between them -- joined only by the brick on the
		// -X/-Y side -- was drawn over that brick's range alone. Where the
		// neighbour reached higher or lower, a vertical slit of sky stood on the
		// boundary as tall as the difference: the slivers down the shadowed wall
		// in T056's walk photographs. So a brick samples the union of its range
		// and the ranges of the eight neighbours whose seams it joins, each found
		// the way that neighbour finds it -- the ground at its centre.
		auto TopAt = [&Job, CellDown](double U, double V)
		{
			const double Ground = LedgerTerrain::Elevation(
				LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(Job.Face, U, V)), Job.Params) / 100.0;
			return FMath::CeilToDouble((Ground + ShellAboveMetres) / CellDown) * CellDown;
		};
		const double OwnTop = FMath::CeilToDouble((CentreGround + ShellAboveMetres) / CellDown) * CellDown;
		double TopMetres = OwnTop;
		double BottomMetres = OwnTop - BrickDown * CellDown;
		for (const FVector2D Step : { FVector2D(1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(1.0, 1.0),
			FVector2D(-1.0, 0.0), FVector2D(0.0, -1.0), FVector2D(-1.0, -1.0), FVector2D(1.0, -1.0), FVector2D(-1.0, 1.0) })
		{
			const double Top = TopAt(Job.U + (0.5 + Step.X) * Job.Extent, Job.V + (0.5 + Step.Y) * Job.Extent);
			TopMetres = FMath::Max(TopMetres, Top);
			BottomMetres = FMath::Min(BottomMetres, Top - BrickDown * CellDown);
		}
		const int32 Layers = FMath::RoundToInt32((TopMetres - BottomMetres) / CellDown);
		const int32 Down = Layers + 1;

		// ---- sample -----------------------------------------------------------
		TArray<float> Samples;
		Samples.SetNumUninitialized(Across * Across * Down);

		auto SampleIndex = [Across](int32 X, int32 Y, int32 Z)
		{
			return (Z * Across + Y) * Across + X;
		};

		auto CornerPoint = [&Job, Across](int32 X, int32 Y)
		{
			const double LocalU = (X - 1) / static_cast<double>(BrickAcross);
			const double LocalV = (Y - 1) / static_cast<double>(BrickAcross);
			return LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(
				Job.Face, Job.U + LocalU * Job.Extent, Job.V + LocalV * Job.Extent));
		};

		bool bAnyInside = false;
		bool bAnyOutside = false;
		for (int32 Z = 0; Z < Down; ++Z)
		{
			const double Altitude = TopMetres - Z * CellDown;
			for (int32 Y = 0; Y < Across; ++Y)
			{
				for (int32 X = 0; X < Across; ++X)
				{
					const FVector3d Direction = CornerPoint(X, Y);
					const double LocalGround = GroundAt(
						(X - 1) / static_cast<double>(BrickAcross),
						(Y - 1) / static_cast<double>(BrickAcross));
					const float Value = static_cast<float>(
						RockDensity(Direction, Altitude, LocalGround, Job.Params));
					Samples[SampleIndex(X, Y, Z)] = Value;
					bAnyInside |= Value > 0.0f;
					bAnyOutside |= Value <= 0.0f;
				}
			}
		}

		if (!bAnyInside || !bAnyOutside)
		{
			// All rock or all air. Nothing crosses, so there is no wall, and
			// this is the answer for most bricks even inside cave country.
			return;
		}

		// ---- one vertex per crossed cell --------------------------------------
		TArray<int32> CellVertex;
		constexpr int32 CellsAcross = BrickAcross + 2;
		CellVertex.Init(INDEX_NONE, CellsAcross * CellsAcross * Layers);

		auto CellIndexOf = [](int32 X, int32 Y, int32 Z)
		{
			return (Z * CellsAcross + Y) * CellsAcross + X;
		};

		/// The world position of a lattice corner, relative to the patch centre.
		auto CornerWorld = [&](int32 X, int32 Y, int32 Z)
		{
			const FVector3d Direction = CornerPoint(X, Y);
			const double Altitude = TopMetres - Z * CellDown;
			return FVector(Direction * ((RadiusMetres + Altitude) * 100.0) - Job.Centre);
		};

		static const int32 EdgeA[12][3] = {
			{0,0,0},{1,0,0},{0,1,0},{0,0,1},
			{0,0,0},{1,0,0},{0,0,1},{1,0,1},
			{0,0,0},{0,1,0},{0,0,1},{0,1,1} };
		static const int32 EdgeB[12][3] = {
			{1,0,0},{1,1,0},{1,1,0},{1,0,1},
			{0,1,0},{1,1,0},{0,1,1},{1,1,1},
			{0,0,1},{0,1,1},{1,0,1},{1,1,1} };

		for (int32 Z = 0; Z < Layers; ++Z)
		{
			for (int32 Y = 0; Y < CellsAcross; ++Y)
			{
				for (int32 X = 0; X < CellsAcross; ++X)
				{
					FVector Sum = FVector::ZeroVector;
					int32 Crossings = 0;

					for (int32 Edge = 0; Edge < 12; ++Edge)
					{
						const int32 AX = X + EdgeA[Edge][0];
						const int32 AY = Y + EdgeA[Edge][1];
						const int32 AZ = Z + EdgeA[Edge][2];
						const int32 BX = X + EdgeB[Edge][0];
						const int32 BY = Y + EdgeB[Edge][1];
						const int32 BZ = Z + EdgeB[Edge][2];

						const float ValueA = Samples[SampleIndex(AX, AY, AZ)];
						const float ValueB = Samples[SampleIndex(BX, BY, BZ)];
						if ((ValueA > 0.0f) == (ValueB > 0.0f))
						{
							continue;
						}

						// Where along the edge the field is zero. Linear, which
						// is exact enough for a field that is a distance.
						const float Along = ValueA / (ValueA - ValueB);
						Sum += FMath::Lerp(
							CornerWorld(AX, AY, AZ), CornerWorld(BX, BY, BZ), Along);
						++Crossings;
					}

					if (Crossings == 0)
					{
						continue;
					}

					CellVertex[CellIndexOf(X, Y, Z)] = Job.CaveVertices.Num();
					Job.CaveVertices.Add(Sum / Crossings);
					Job.CaveNormals.Add(FVector::UpVector);
					Job.CaveUVs.Add(FVector2D::ZeroVector);
				}
			}
		}

		if (Job.CaveVertices.Num() == 0)
		{
			return;
		}

		// ---- join four cells around every crossed edge ------------------------
		//
		// Only the three edges that start at a lattice corner, and only where
		// all four surrounding cells exist -- so the brick's own boundary is
		// left open rather than sealed with a lid. A lid would be a wall across
		// the passage at every patch boundary, which is worse than a seam.
		// **Wound towards the air, decided per quad from where the air is.** The
		// winding used to come from which side of the edge was rock, which is
		// right only while the lattice axes are right-handed -- and they are not
		// on every cube face, and the depth axis points down. So on some faces
		// every wall faced into the rock, and from inside the passage the whole
		// cave was back faces: T056's camera eighteen metres under a mouth saw
		// uniform sky. The front face here is the one whose normal is
		// (P2 - P0) x (P1 - P0), the convention the normals below are
		// accumulated with and the one every mesh in this project renders by.
		//
		// **Per triangle, not per quad.** A surface-nets quad is not planar: its
		// four vertices are averages of different cells' crossings, and where the
		// wall folds over into the ground at the rim a quad folds with it, so its
		// two halves can face opposite ways. Deciding the winding from one half
		// turned the other into a back face, and down the walk's shadowed wall
		// that was a row of downward-pointing wedges of sky along the wall top, one
		// per quad -- evenly spaced because they were the lattice (T056).
		auto Wind = [&Job](int32 A, int32 B, int32 C, const FVector& Air)
		{
			const FVector& PA = Job.CaveVertices[A];
			const FVector Front = FVector::CrossProduct(Job.CaveVertices[C] - PA, Job.CaveVertices[B] - PA);
			const bool bFlip = FVector::DotProduct(Front, Air) < 0.0;
			Job.CaveTriangles.Add(A);
			Job.CaveTriangles.Add(bFlip ? C : B);
			Job.CaveTriangles.Add(bFlip ? B : C);
		};
		auto Quad = [&Wind](int32 A, int32 B, int32 C, int32 D, const FVector& Air)
		{
			if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE || D == INDEX_NONE)
			{
				return;
			}
			Wind(A, B, C, Air);
			Wind(A, C, D, Air);
		};

		for (int32 Z = 1; Z < Layers; ++Z)
		{
			for (int32 Y = 1; Y <= BrickAcross + 1; ++Y)
			{
				for (int32 X = 1; X <= BrickAcross + 1; ++X)
				{
					const float Here = Samples[SampleIndex(X, Y, Z)];

					// Along X: the four cells sharing this edge differ in Y and Z.
					if ((Here > 0.0f) != (Samples[SampleIndex(X + 1, Y, Z)] > 0.0f))
					{
						Quad(CellVertex[CellIndexOf(X, Y - 1, Z - 1)],
							CellVertex[CellIndexOf(X, Y, Z - 1)],
							CellVertex[CellIndexOf(X, Y, Z)],
							CellVertex[CellIndexOf(X, Y - 1, Z)],
							(Here > 0.0f ? 1.0 : -1.0) * (CornerWorld(X + 1, Y, Z) - CornerWorld(X, Y, Z)));
					}
					// Along Y: differ in X and Z.
					if ((Here > 0.0f) != (Samples[SampleIndex(X, Y + 1, Z)] > 0.0f))
					{
						Quad(CellVertex[CellIndexOf(X - 1, Y, Z - 1)],
							CellVertex[CellIndexOf(X, Y, Z - 1)],
							CellVertex[CellIndexOf(X, Y, Z)],
							CellVertex[CellIndexOf(X - 1, Y, Z)],
							(Here > 0.0f ? 1.0 : -1.0) * (CornerWorld(X, Y + 1, Z) - CornerWorld(X, Y, Z)));
					}
					// Along Z: differ in X and Y.
					if ((Here > 0.0f) != (Samples[SampleIndex(X, Y, Z + 1)] > 0.0f))
					{
						Quad(CellVertex[CellIndexOf(X - 1, Y - 1, Z)],
							CellVertex[CellIndexOf(X, Y - 1, Z)],
							CellVertex[CellIndexOf(X, Y, Z)],
							CellVertex[CellIndexOf(X - 1, Y, Z)],
							(Here > 0.0f ? 1.0 : -1.0) * (CornerWorld(X, Y, Z + 1) - CornerWorld(X, Y, Z)));
					}
				}
			}
		}

		// ---- normals from the triangles ---------------------------------------
		//
		// Accumulated face normals rather than the field's gradient. The
		// gradient is available and would be smoother, but it is the gradient of
		// a minimum of two fields and is discontinuous exactly at the mouth,
		// where the ground surface takes over from the cave wall -- which is the
		// one place the shading has to look right.
		for (FVector& Normal : Job.CaveNormals)
		{
			Normal = FVector::ZeroVector;
		}
		for (int32 Triangle = 0; Triangle + 2 < Job.CaveTriangles.Num(); Triangle += 3)
		{
			const int32 I0 = Job.CaveTriangles[Triangle];
			const int32 I1 = Job.CaveTriangles[Triangle + 1];
			const int32 I2 = Job.CaveTriangles[Triangle + 2];
			const FVector Face = FVector::CrossProduct(
				Job.CaveVertices[I2] - Job.CaveVertices[I0],
				Job.CaveVertices[I1] - Job.CaveVertices[I0]);
			Job.CaveNormals[I0] += Face;
			Job.CaveNormals[I1] += Face;
			Job.CaveNormals[I2] += Face;
		}

		const FVector LocalUp = FVector(Job.Centre.GetSafeNormal());
		for (FVector& Normal : Job.CaveNormals)
		{
			Normal = Normal.GetSafeNormal();
			if (Normal.IsNearlyZero())
			{
				Normal = LocalUp;
			}
		}

		// ---- and the back of every face ----------------------------------------
		//
		// **Two-sided, because the walk's wall had windows in it that were back
		// faces** (T056). Winding towards the air per quad, then per triangle,
		// left the same row of wedge-shaped slits of sky along the shadowed wall
		// top; drawing every triangle from both sides removed them, so some faces
		// the camera sees are wound away from it wherever the fold at the rim
		// defeats the air test. The back is a second copy of the vertices with
		// the normals flipped, so it is lit as the side it is.
		// ponytail: doubles the cave section; orient by the field gradient instead
		// if cave patches ever show up in the frame budget.
		const int32 FrontVertices = Job.CaveVertices.Num();
		const int32 FrontIndices = Job.CaveTriangles.Num();
		// Appended from copies: adding an array's own element to it trips the
		// self-reference assert, which is how the first walk of this died.
		Job.CaveVertices.Append(TArray<FVector>(Job.CaveVertices));
		Job.CaveUVs.Append(TArray<FVector2D>(Job.CaveUVs));
		for (int32 Index = 0; Index < FrontVertices; ++Index)
		{
			Job.CaveNormals.Add(-FVector(Job.CaveNormals[Index]));
		}
		for (int32 Index = 0; Index + 2 < FrontIndices; Index += 3)
		{
			Job.CaveTriangles.Add(Job.CaveTriangles[Index] + FrontVertices);
			Job.CaveTriangles.Add(Job.CaveTriangles[Index + 2] + FrontVertices);
			Job.CaveTriangles.Add(Job.CaveTriangles[Index + 1] + FrontVertices);
		}
	}
}
