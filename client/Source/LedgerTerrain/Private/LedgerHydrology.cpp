#include "LedgerHydrology.h"

#include "Async/ParallelFor.h"

namespace
{
	/// The eight neighbours of a lattice cell, as (du, dv) in cell units.
	constexpr int32 NeighbourCount = 8;
	constexpr int32 NeighbourU[NeighbourCount] = { -1,  0,  1, -1, 1, -1, 0, 1 };
	constexpr int32 NeighbourV[NeighbourCount] = { -1, -1, -1,  0, 0,  1, 1, 1 };

	FVector3d CellDirection(int32 Face, int32 CellU, int32 CellV, int32 Resolution)
	{
		const double Inverse = 1.0 / static_cast<double>(Resolution);
		return LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(
			static_cast<ELedgerCubeFace>(Face),
			(CellU + 0.5) * Inverse,
			(CellV + 0.5) * Inverse));
	}
}

FVector3d FLedgerFlowField::Centre(int32 Index) const
{
	const int32 PerFace = Resolution * Resolution;
	const int32 Face = Index / PerFace;
	const int32 Within = Index - Face * PerFace;
	return CellDirection(Face, Within % Resolution, Within / Resolution, Resolution);
}

namespace LedgerHydrology
{
	int32 CellIndex(ELedgerCubeFace Face, int32 CellU, int32 CellV, int32 Resolution)
	{
		if (CellU >= 0 && CellU < Resolution && CellV >= 0 && CellV < Resolution)
		{
			return static_cast<int32>(Face) * Resolution * Resolution
				+ CellV * Resolution + CellU;
		}

		// Off the edge. The cube point for an out-of-range u or v lies on the
		// extended plane of this face, and which face it really belongs to is
		// whichever coordinate is now largest -- which is exactly what
		// DirectionToFace decides. No sphere in the loop, so this is exact.
		const double Inverse = 1.0 / static_cast<double>(Resolution);
		const FVector3d OnCube = LedgerTerrain::FaceToCube(
			Face, (CellU + 0.5) * Inverse, (CellV + 0.5) * Inverse);

		ELedgerCubeFace OtherFace = Face;
		double U = 0.0;
		double V = 0.0;
		LedgerTerrain::DirectionToFace(OnCube, OtherFace, U, V);

		const int32 OtherU = FMath::Clamp(
			FMath::FloorToInt32(U * Resolution), 0, Resolution - 1);
		const int32 OtherV = FMath::Clamp(
			FMath::FloorToInt32(V * Resolution), 0, Resolution - 1);
		return static_cast<int32>(OtherFace) * Resolution * Resolution
			+ OtherV * Resolution + OtherU;
	}

	int32 CellAt(const FVector3d& UnitSphere, int32 Resolution)
	{
		ELedgerCubeFace Face = ELedgerCubeFace::PositiveX;
		double TargetU = 0.0;
		double TargetV = 0.0;
		LedgerTerrain::DirectionToFace(UnitSphere, Face, TargetU, TargetV);

		// Undo the warp. CubeToSphere scales each axis by a factor depending on
		// the other two, so a face position on the sphere is not the face
		// position on the cube: near a face edge they differ by several cells.
		// There is no closed form, but the map is smooth and close to the
		// identity in these coordinates, so a damped fixed point converges in a
		// handful of steps. Damped rather than multiplicative because the
		// correction passes through zero at a face centre.
		double U = TargetU;
		double V = TargetV;
		for (int32 Iteration = 0; Iteration < 12; ++Iteration)
		{
			ELedgerCubeFace Landed = Face;
			double GotU = 0.0;
			double GotV = 0.0;
			LedgerTerrain::DirectionToFace(
				LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(Face, U, V)),
				Landed, GotU, GotV);
			if (Landed != Face)
			{
				break;
			}
			const double ErrorU = TargetU - GotU;
			const double ErrorV = TargetV - GotV;
			U += 0.8 * ErrorU;
			V += 0.8 * ErrorV;
			if (FMath::Abs(ErrorU) < 1e-9 && FMath::Abs(ErrorV) < 1e-9)
			{
				break;
			}
		}

		return CellIndex(Face,
			FMath::FloorToInt32(U * Resolution),
			FMath::FloorToInt32(V * Resolution), Resolution);
	}

	FLedgerFlowField BuildFlowField(const FLedgerTerrainParams& Params, int32 Resolution)
	{
		FLedgerFlowField Field;
		Field.Resolution = FMath::Max(2, Resolution);

		const int32 Count = Field.Num();
		const int32 PerFace = Field.Resolution * Field.Resolution;
		Field.Height.SetNumUninitialized(Count);
		Field.Downstream.SetNumUninitialized(Count);
		Field.Flow.SetNumUninitialized(Count);
		Field.Terminal.SetNumUninitialized(Count);

		// ---- heights ------------------------------------------------------
		//
		// The only height samples this whole thing takes: one per cell. Every
		// neighbour comparison below reads them back out of the array, because
		// sampling is the expensive part of this project and a naive version
		// would take nine.
		ParallelFor(Count, [&Field, &Params, PerFace](int32 Index)
		{
			const int32 Face = Index / PerFace;
			const int32 Within = Index - Face * PerFace;
			const FVector3d Point = CellDirection(
				Face, Within % Field.Resolution, Within / Field.Resolution, Field.Resolution);
			Field.Height[Index] = static_cast<float>(
				LedgerTerrain::Elevation(Point, Params) / 100.0);
		});

		// ---- steepest descent ---------------------------------------------
		ParallelFor(Count, [&Field, PerFace](int32 Index)
		{
			Field.Flow[Index] = 1.0f;

			if (Field.Height[Index] <= 0.0f)
			{
				Field.Terminal[Index] = ELedgerFlowTerminal::Sea;
				Field.Downstream[Index] = INDEX_NONE;
				return;
			}

			const int32 Face = Index / PerFace;
			const int32 Within = Index - Face * PerFace;
			const int32 CellU = Within % Field.Resolution;
			const int32 CellV = Within / Field.Resolution;

			int32 Lowest = INDEX_NONE;
			float LowestHeight = Field.Height[Index];

			for (int32 Neighbour = 0; Neighbour < NeighbourCount; ++Neighbour)
			{
				// In cube space. Going through the sphere here is what broke
				// the first version: see the header.
				const int32 Other = CellIndex(
					static_cast<ELedgerCubeFace>(Face),
					CellU + NeighbourU[Neighbour], CellV + NeighbourV[Neighbour],
					Field.Resolution);
				if (Other == Index)
				{
					// A seam cell whose neighbour rounded back onto itself.
					continue;
				}

				if (Field.Height[Other] < LowestHeight)
				{
					LowestHeight = Field.Height[Other];
					Lowest = Other;
				}
			}

			Field.Downstream[Index] = Lowest;
			Field.Terminal[Index] = Lowest == INDEX_NONE
				? ELedgerFlowTerminal::Basin
				: ELedgerFlowTerminal::Flows;
		});

		// ---- accumulation --------------------------------------------------
		//
		// Highest first. Every cell's downstream is strictly lower than it, so
		// descending height is a topological order of the flow graph and one
		// pass is enough -- no queue, no visited set, and no possibility of a
		// cycle to guard against.
		TArray<int32> Order;
		Order.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Order[Index] = Index;
		}
		Order.Sort([&Field](int32 A, int32 B) { return Field.Height[A] > Field.Height[B]; });

		for (const int32 Index : Order)
		{
			const int32 Below = Field.Downstream[Index];
			if (Below != INDEX_NONE)
			{
				Field.Flow[Below] += Field.Flow[Index];
			}
		}

		return Field;
	}

	int32 TraceToTerminal(const FLedgerFlowField& Field, int32 From, int32 MaxSteps, int32& OutSteps)
	{
		OutSteps = 0;
		int32 Current = From;
		while (OutSteps < MaxSteps)
		{
			const int32 Below = Field.Downstream[Current];
			if (Below == INDEX_NONE)
			{
				return Current;
			}
			Current = Below;
			++OutSteps;
		}
		return INDEX_NONE;
	}

	uint64 NetworkHash(const FLedgerFlowField& Field)
	{
		// FNV-1a over the downstream links. The links are the network: two
		// builds that agree on them agree on every river, every confluence and
		// every mouth, whatever the heights rounded to.
		uint64 Hash = 1469598103934665603ull;
		for (const int32 Below : Field.Downstream)
		{
			const uint32 Value = static_cast<uint32>(Below);
			for (int32 Byte = 0; Byte < 4; ++Byte)
			{
				Hash ^= (Value >> (Byte * 8)) & 0xFFu;
				Hash *= 1099511628211ull;
			}
		}
		return Hash;
	}
}
