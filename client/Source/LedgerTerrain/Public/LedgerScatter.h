// Rocks, trees and debris, placed from the biome rules. T057, §6.8.
//
// **Deterministic from the patch, not from the stream.** A scatter that
// depends on what order patches arrived in is a scatter that moves when you
// turn round. Every instance's position comes from a hash of the patch key and
// a cell index and nothing else, so the same ground gives the same forest on
// every run, on every machine, in any order.
//
// **Generated on the worker with the patch.** Placement needs the height field,
// which is the expensive thing in this project; doing it on the game thread
// would put a second full sampling pass in the frame. It rides the patch job,
// which already has the terrain parameters and the biome set.

#pragma once

#include "CoreMinimal.h"

struct FLedgerPatchJob;

/// One scattered thing.
///
/// Floats, and a position relative to the patch centre, because there may be
/// tens of thousands of these and they are copied into an instanced component
/// as transforms. Doubles would double the traffic to buy precision that a
/// patch-relative offset does not need.
struct FLedgerScatterInstance
{
	FVector3f Position = FVector3f::ZeroVector;
	FQuat4f Rotation = FQuat4f::Identity;
	float Scale = 1.0f;

	/// Which mesh. The planet is handed an array of them and this indexes it.
	uint8 Variant = 0;

	/// What it is: 0 a stone, otherwise one more than an index into
	/// LedgerScatter::PlantKinds. The variant picks a mesh within the kind.
	uint8 Kind = 0;
};

namespace LedgerScatter
{
	/// Candidate cells across a patch. Every cell offers one instance and most
	/// decline, so this is the ceiling on density rather than the density.
	constexpr int32 CellsAcross = 20;

	/// Ground steeper than this holds nothing. Not a biome property: a tree on
	/// a forty-degree slope is standing on its own root ball whatever the
	/// biome says, and this is the cheap guard before the biome is even asked.
	constexpr double MaxSlopeDegrees = 34.0;

	/// The plants a biome can name in its "plants" list, and how many stems
	/// each puts down together: grass grows in tufts and ferns in a stand; a
	/// shrub or a sapling stands alone.
	inline constexpr const TCHAR* PlantKinds[] = {
		TEXT("grass"), TEXT("shrub"), TEXT("fern"), TEXT("conifer"), TEXT("succulent"), TEXT("deadwood") };
	inline constexpr int32 PlantClump[] = { 5, 1, 3, 1, 1, 1 };
	constexpr int32 PlantKindCount = UE_ARRAY_COUNT(PlantKinds);

	/// Plant sites across one scatter cell, each way: two is a site every ten
	/// metres on the finest patch. Undergrowth, not a lawn -- a sward is a
	/// near-field layer of its own, not more of this one.
	constexpr int32 PlantSites = 2;

	/// Plants are drawn to this distance and no further: past it a shrub is
	/// under a pixel from any height anybody looks at one from, and nearly
	/// every instance in the scatter is a plant.
	constexpr int32 PlantCullMetres = 300;

	/// The finest patch size, in centimetres of world size. Everything is
	/// measured relative to this.
	constexpr double FinestWorldSize = 40000.0;

	/// How many times that a patch may be and still be scattered.
	///
	/// **One, and the attempt to make it four is worth recording.** Scatter
	/// that stops at the finest LOD leaves an edge, so the obvious move is to
	/// carry on for a couple of size steps with fewer candidates per patch as
	/// the patch grows. That was tried: 97,777 instances over 905 patches, well
	/// inside the frame budget at 1.5 ms and still sixty -- and it looked far
	/// worse, because a fixed candidate count over a patch means the density
	/// per unit area falls by the square of the patch's size, so patches one
	/// LOD step apart differ eightfold and the forest comes out in rectangular
	/// blocks with bare ground between them. The patch grid, drawn in trees.
	///
	/// Density has to be a function of *position*, not of which patch a point
	/// happens to fall in, and that means a candidate lattice fixed to the
	/// world rather than to the patch. That is real work and it is T059's, so
	/// this stays at one and the edge stays until then.
	constexpr double MaxSizeRatio = 1.0;

	/// Fills the job's scatter list. Empty for any patch more than MaxSizeRatio
	/// times the finest, which is the great majority of them.
	LEDGERTERRAIN_API void ScatterPatch(FLedgerPatchJob& Job);
}
