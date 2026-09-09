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

	/// The largest patch that gets scattered, in centimetres of world size.
	///
	/// **The finest LOD only, and this is a frame-budget decision measured
	/// rather than guessed.** Scattering every patch that carries collision
	/// gave 71,286 instances over 391 patches at a forest site, and rebuilding
	/// the instanced components took 10.5 ms -- most of a frame, every frame,
	/// while streaming. Restricting it to the finest patches keeps the
	/// instances where they are more than a pixel across and takes the count
	/// down with the cost.
	constexpr double MaxScatterWorldSize = 40000.0;

	/// Fills the job's scatter list. Empty unless the patch carries collision
	/// and is fine enough -- the planet's own definition of "near enough to
	/// matter", and the framework never touches the 99% of patches nobody can
	/// reach.
	LEDGERTERRAIN_API void ScatterPatch(FLedgerPatchJob& Job);
}
