// Meshing the cave field for one patch. T056.
//
// **Surface nets, not marching cubes.** Marching cubes needs a 256-entry case
// table that nobody can review and everybody copies from the same web page.
// Naive surface nets puts one vertex in every cell the surface crosses, at the
// average of that cell's edge crossings, and joins four of them around each
// crossed edge. It is eighty lines, it has no table, and the mesh it makes is
// quad-based and smoother -- which for a cave wall is what you want anyway.
//
// **Only where the patch might have caves, and only when it is small enough.**
// A root patch spans a continent; a brick over it would have cells kilometres
// wide and would mesh nothing but noise. `ShouldMeshCaves` is the gate, and for
// the 74% of the planet with no cave country under it the answer is no after a
// single noise evaluation.

#pragma once

#include "CoreMinimal.h"
#include "LedgerTerrainMath.h"

struct FLedgerPatchJob;

namespace LedgerCaves
{
	/// Cells across a patch's brick, and down through it.
	///
	/// 32 across a 300 m patch is about 10 m a cell, against a passage radius of
	/// 14 m. Coarser than the passage and the tunnel breaks into a chain of
	/// unconnected bubbles; this is the coarsest that does not.
	constexpr int32 BrickAcross = 32;
	constexpr int32 BrickDown = 24;

	/// How far below the local ground the brick reaches, in metres. Not the
	/// whole shell: the deep half has no mouth in it and cannot be seen from
	/// outside, so meshing it costs a lot to render nothing.
	constexpr double BrickDepthMetres = 240.0;

	/// The largest patch that gets a cave brick, in centimetres of world size.
	///
	/// A brick is always BrickAcross cells wide, so this is really a statement
	/// about cell size: 120000 cm is 1.2 km, giving cells of 37 m, which is
	/// already coarser than a passage. Anything bigger would mesh bubbles.
	constexpr double MaxCaveWorldSize = 120000.0;

	/// Whether this patch is worth meshing caves for at all.
	LEDGERTERRAIN_API bool ShouldMeshCaves(const FLedgerPatchJob& Job);

	/// Fills the job's cave section. Leaves it empty when there is no wall
	/// inside the brick, which is the common case even inside cave country.
	LEDGERTERRAIN_API void GenerateCaveMesh(FLedgerPatchJob& Job);
}
