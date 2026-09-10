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
	/// is ignored rather than misread. 2: T429's near-field band, which changes
	/// every elevation in every fine patch without changing the format at all
	/// -- exactly the case a layout check would have missed. 3: T434's stone
	/// sizes, which are stored in the payload and would otherwise have been
	/// served from disk at the old tree-sized distribution forever. 4: the
	/// scatter fill cap, for the same reason. 5: per-octave near-field
	/// fading, which changes every elevation in every fine patch. 6: the
	/// climate model's drying rate.
	///
	/// **The key covers inputs, not code, and 6 is what that costs.**
	///
	/// ContentKey mixes the terrain parameters, the biome definitions, the
	/// season, the delta and the patch's own geometry -- everything that is
	/// *data*. It cannot mix the behaviour of LedgerClimate or LedgerScatter,
	/// so a change to either produces different patches at the same address.
	/// That is what this counter is for and it has to be turned by hand.
	///
	/// It was not turned for the climate change, and the failure was not
	/// subtle: Ledger.Scatter.NothingFloatsOrIsHalfBuried compares a patch
	/// scattered with its mesh against the same patch scattered without one,
	/// and got 141 against 60 -- the first served from a cache written before
	/// the drying rate moved, the second computed live. A test comparing two
	/// things it believed were the same computation.
	///
	/// Anything that changes what generation produces changes this number. It is in the key, not a header check:
	/// a stale entry then simply never matches and is evicted in its turn.
	constexpr uint32 FormatVersion = 6;

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
