// The patch cache that survives a run. T065, §6.8.
//
// Generation is deterministic, so a patch that has been built once on this
// machine need never be built again. The in-memory cache already gets 88%
// reuse inside a run; this is the other 12%, and all of the first minute.
//
// **It stores what was expensive, not what was produced.** A finished patch is
// about 250 KB of interleaved vertex data and 35,000 of them is nearly nine
// gigabytes, which is not a cache, it is a second copy of the planet. What cost
// the time is the noise: 4,225 elevation samples per patch, plus a climate grid
// whose every point marches forty steps upwind sampling the height field again.
// Positions, normals, tangents, triangles and morph targets are arithmetic on
// top of that, and arithmetic is not what anybody is waiting for.
//
// So the payload is the elevation grid, the vertex colours the climate produced,
// the palette and the scatter -- about 34 KB a patch -- and the rest is
// recomputed. "Generates nothing" in the acceptance means evaluates no noise,
// which is the thing that costs.

#pragma once

#include "CoreMinimal.h"

struct FLedgerPatchJob;

namespace LedgerPatchDisk
{
	/// Bumped whenever the payload's layout or meaning changes, so an old cache
	/// is ignored rather than misread. It is in the key, not a header check:
	/// a stale entry then simply never matches and is evicted in its turn.
	constexpr uint32 FormatVersion = 1;

	/// Where the cache lives. Under Saved, because it is derived from the
	/// project rather than part of it, and because it is per machine.
	LEDGERTERRAIN_API FString Directory();

	/// Everything about a job that changes what generation would produce.
	///
	/// The whole point of a content-addressed cache is that this is complete.
	/// A field left out is a patch served from a cache that no longer describes
	/// it -- and the failure would look like terrain, not like a cache bug.
	LEDGERTERRAIN_API uint64 ContentKey(const FLedgerPatchJob& Job);

	/// Where an entry with this key lives. Public so a test can clean up after
	/// itself without a second copy of the naming rule to drift out of step.
	LEDGERTERRAIN_API FString PathFor(uint64 Key);

	/// Fills the job's expensive parts from disk. False when there is no entry.
	LEDGERTERRAIN_API bool Load(FLedgerPatchJob& Job);

	/// Writes them. Quietly does nothing if the directory cannot be written to
	/// -- a machine with a read-only Saved should be slow, not broken.
	LEDGERTERRAIN_API void Store(const FLedgerPatchJob& Job);

	/// How many entries have been read and written this run, for the report.
	LEDGERTERRAIN_API void Stats(int32& OutHits, int32& OutMisses, int32& OutWrites);
}
