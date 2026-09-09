// Rivers, derived from the height field rather than drawn on it. T055, §6.8.
//
// **A lattice, not a mesh.** The terrain quadtree exists to put triangles where
// the camera is; drainage is a global question and has to be answered once for
// the whole planet at one resolution, or two neighbouring patches at different
// LODs would disagree about which way a river runs. So this is a fixed grid on
// the cube-sphere, built once, and the terrain reads from it.
//
// **Cross-face neighbours cost nothing, but only in cube space.** A cell's
// neighbour is found by offsetting u and v past the edge of the face and asking
// `DirectionToFace` where the resulting *cube* point belongs. That is exact:
// `DirectionToFace` inverts `FaceToCube`.
//
// It is NOT exact through the sphere. `CubeToSphere` applies an area-evening
// warp on top of the projection, and `DirectionToFace` does not undo it, so
// `DirectionToFace(CubeToSphere(FaceToCube(f, u, v)))` is a different cell --
// by several, near a face edge. The first version of this indexed neighbours
// that way and every one of its tests passed: the flow graph was still
// perfectly consistent, every link still went downhill and nothing cycled, and
// none of the links joined cells that touch. `CellRoundTripsThroughItsCentre`
// exists because of that.

#pragma once

#include "CoreMinimal.h"
#include "LedgerTerrainMath.h"

/// Why a cell has nowhere to send its water.
enum class ELedgerFlowTerminal : uint8
{
	/// It sends water to a neighbour.
	Flows = 0,

	/// It is at or below sea level.
	Sea = 1,

	/// Land with no lower neighbour. A basin, and on a noise height field there
	/// are a great many of them -- every local dimple is one. That is a fact
	/// about the terrain rather than a defect here, and the report counts them.
	Basin = 2,
};

/// Where the water on this planet goes.
struct FLedgerFlowField
{
	/// Cells across one cube face. The lattice is Resolution² per face, six
	/// faces.
	int32 Resolution = 0;

	/// Metres above sea level at each cell centre.
	TArray<float> Height;

	/// The cell this one drains into, or INDEX_NONE at a terminal.
	TArray<int32> Downstream;

	/// How many cells drain through this one, itself included. One for a
	/// ridge-top cell, thousands at a river mouth.
	TArray<float> Flow;

	TArray<ELedgerFlowTerminal> Terminal;

	int32 Num() const { return Resolution * Resolution * 6; }

	/// The unit-sphere point at a cell's centre.
	LEDGERTERRAIN_API FVector3d Centre(int32 Index) const;
};

namespace LedgerHydrology
{
	/// Cells across a cube face by default.
	///
	/// 256 puts a cell at about 39 km, which is a continental-scale drainage
	/// network rather than a stream you can stand in. That is the right first
	/// answer: the acceptance is about whether the network is *correct* --
	/// nothing uphill, everything reaching the sea or a basin, the same every
	/// time -- and correctness is a property the resolution does not change.
	/// 6 x 256² is 393,216 cells and one height sample each.
	constexpr int32 DefaultResolution = 256;

	/// The cell at a face position, with out-of-range coordinates resolved onto
	/// whichever face they actually belong to. Exact.
	LEDGERTERRAIN_API int32 CellIndex(
		ELedgerCubeFace Face, int32 CellU, int32 CellV, int32 Resolution);

	/// The cell containing a direction on the sphere.
	///
	/// Inverts `CubeToSphere` numerically -- it has no closed-form inverse, and
	/// the alternative is the bug described at the top of this file.
	LEDGERTERRAIN_API int32 CellAt(const FVector3d& UnitSphere, int32 Resolution);

	/// Samples the height field, finds each cell's steepest-descent neighbour,
	/// and accumulates flow. Pure: the same params and resolution give the same
	/// field, which is one third of the acceptance.
	LEDGERTERRAIN_API FLedgerFlowField BuildFlowField(
		const FLedgerTerrainParams& Params, int32 Resolution);

	/// Follows the flow downstream. Returns the terminal cell and the number of
	/// steps, or INDEX_NONE if it did not terminate within MaxSteps -- which,
	/// because every step is strictly downhill, can only mean the field is
	/// broken.
	LEDGERTERRAIN_API int32 TraceToTerminal(
		const FLedgerFlowField& Field, int32 From, int32 MaxSteps, int32& OutSteps);

	/// A hash of the whole network. Two builds of the same planet agree; two
	/// different seeds do not.
	LEDGERTERRAIN_API uint64 NetworkHash(const FLedgerFlowField& Field);
}
