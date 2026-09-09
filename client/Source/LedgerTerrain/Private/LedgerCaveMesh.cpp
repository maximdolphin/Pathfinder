#include "LedgerCaveMesh.h"

#include "LedgerCaves.h"
#include "LedgerPlanet.h"

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

		const int32 Across = BrickAcross + 1;
		const int32 Down = BrickDown + 1;
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
		const double TopMetres = CentreGround + ShellAboveMetres;
		const double CellDown = (ShellAboveMetres + BrickDepthMetres) / BrickDown;

		// ---- sample -----------------------------------------------------------
		TArray<float> Samples;
		Samples.SetNumUninitialized(Across * Across * Down);

		auto SampleIndex = [Across](int32 X, int32 Y, int32 Z)
		{
			return (Z * Across + Y) * Across + X;
		};

		auto CornerPoint = [&Job, Across](int32 X, int32 Y)
		{
			const double LocalU = X / static_cast<double>(Across - 1);
			const double LocalV = Y / static_cast<double>(Across - 1);
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
						X / static_cast<double>(Across - 1),
						Y / static_cast<double>(Across - 1));
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
		CellVertex.Init(INDEX_NONE, BrickAcross * BrickAcross * BrickDown);

		auto CellIndexOf = [](int32 X, int32 Y, int32 Z)
		{
			return (Z * BrickAcross + Y) * BrickAcross + X;
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

		for (int32 Z = 0; Z < BrickDown; ++Z)
		{
			for (int32 Y = 0; Y < BrickAcross; ++Y)
			{
				for (int32 X = 0; X < BrickAcross; ++X)
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
		auto Quad = [&Job](int32 A, int32 B, int32 C, int32 D, bool bFlip)
		{
			if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE || D == INDEX_NONE)
			{
				return;
			}
			if (bFlip)
			{
				Job.CaveTriangles.Add(A); Job.CaveTriangles.Add(C); Job.CaveTriangles.Add(B);
				Job.CaveTriangles.Add(A); Job.CaveTriangles.Add(D); Job.CaveTriangles.Add(C);
			}
			else
			{
				Job.CaveTriangles.Add(A); Job.CaveTriangles.Add(B); Job.CaveTriangles.Add(C);
				Job.CaveTriangles.Add(A); Job.CaveTriangles.Add(C); Job.CaveTriangles.Add(D);
			}
		};

		for (int32 Z = 1; Z < BrickDown; ++Z)
		{
			for (int32 Y = 1; Y < BrickAcross; ++Y)
			{
				for (int32 X = 1; X < BrickAcross; ++X)
				{
					const float Here = Samples[SampleIndex(X, Y, Z)];

					// Along X: the four cells sharing this edge differ in Y and Z.
					if ((Here > 0.0f) != (Samples[SampleIndex(X + 1, Y, Z)] > 0.0f))
					{
						Quad(CellVertex[CellIndexOf(X, Y - 1, Z - 1)],
							CellVertex[CellIndexOf(X, Y, Z - 1)],
							CellVertex[CellIndexOf(X, Y, Z)],
							CellVertex[CellIndexOf(X, Y - 1, Z)],
							Here > 0.0f);
					}
					// Along Y: differ in X and Z.
					if ((Here > 0.0f) != (Samples[SampleIndex(X, Y + 1, Z)] > 0.0f))
					{
						Quad(CellVertex[CellIndexOf(X - 1, Y, Z - 1)],
							CellVertex[CellIndexOf(X, Y, Z - 1)],
							CellVertex[CellIndexOf(X, Y, Z)],
							CellVertex[CellIndexOf(X - 1, Y, Z)],
							Here <= 0.0f);
					}
					// Along Z: differ in X and Y.
					if ((Here > 0.0f) != (Samples[SampleIndex(X, Y, Z + 1)] > 0.0f))
					{
						Quad(CellVertex[CellIndexOf(X - 1, Y - 1, Z)],
							CellVertex[CellIndexOf(X, Y - 1, Z)],
							CellVertex[CellIndexOf(X, Y, Z)],
							CellVertex[CellIndexOf(X - 1, Y, Z)],
							Here > 0.0f);
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
	}
}
