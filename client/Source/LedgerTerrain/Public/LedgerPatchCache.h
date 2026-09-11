// Geometry for patches that have been on screen and are not on screen now.
//
// Flying out over a ridge and back regenerates every patch on the way home, and
// the height function does not change between the two passes — the second
// generation is guaranteed to reproduce what the first one made. Holding the
// geometry costs memory and returns worker-thread seconds.
//
// **This is a type rather than four fields on the planet actor**, because it
// has an invariant and invariants want an owner: the byte count it evicts
// against must equal the bytes it is actually holding. When that bookkeeping
// lived among the actor's other twenty members it went wrong twice — once by
// caching live patches and starving the pool, once by counting an entry it had
// already removed — and both times the symptom appeared somewhere else
// entirely, as patches that could not be filled.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBiome.h"
#include "LedgerScatter.h"
#include "ProceduralMeshComponent.h"

/// A patch that has been drawn and is not drawn now.
///
/// It holds the component's own section structs rather than the parallel arrays
/// the generator produced. Taking a copy on release is one memcpy of an
/// interleaved buffer and putting it back is `SetProcMeshSection`, so nothing
/// is converted in either direction.
///
/// The stitch flags live in the entry, not in the key. A patch whose coarser
/// neighbour has since subdivided needs different edge geometry, and that is
/// rare enough that checking on lookup and missing beats a wider key.
struct FLedgerCachedPatch
{
	FVector3d Centre = FVector3d::ZeroVector;
	uint8 StitchLeft = 0;
	uint8 StitchRight = 0;
	uint8 StitchBottom = 0;
	uint8 StitchTop = 0;
	uint8 CornerLevels[4] = { 0, 0, 0, 0 };

	FProcMeshSection Land;
	FProcMeshSection Water;
	bool bHasWater = false;

	/// Which biomes the vertex colours in `Land` are weights of. Cached with
	/// the geometry because the two only mean anything together: replaying a
	/// section under a different palette paints the ground with the wrong
	/// three grounds.
	FLedgerBiomePalette Palette;

	/// Cached with the geometry, because regenerating it means sampling the
	/// height field again -- which is the cost the cache exists to avoid.
	TArray<FLedgerScatterInstance> Scatter;

	/// Monotonic counter, not a timestamp. Wall clock would make eviction
	/// depend on frame rate; a serial makes it depend on use, which is what
	/// least-recently-used is supposed to mean.
	uint64 LastUsed = 0;

	int64 Bytes() const;
};

class LEDGERTERRAIN_API FLedgerPatchCache
{
public:
	/// Memory the cache may hold, in bytes. Bounded by bytes rather than by
	/// entries because entries are not the same size: a patch over water carries
	/// a second mesh section and a patch inland does not, so a count would be a
	/// budget for the average patch and a surprise for every other one.
	void SetBudget(int64 Bytes) { BudgetBytes = Bytes; }

	/// The entry for a key, or null. Touching it counts as a use.
	FLedgerCachedPatch* Take(uint64 Key);

	/// Adds an entry, evicting least-recently-used ones until the budget holds.
	void Insert(uint64 Key, TSharedPtr<FLedgerCachedPatch> Entry);

	void Remove(uint64 Key);
	void Empty();

	int32 Num() const { return Entries.Num(); }
	int64 Bytes() const { return HeldBytes; }
	int64 Evictions() const { return EvictionCount; }

private:
	TMap<uint64, TSharedPtr<FLedgerCachedPatch>> Entries;
	int64 HeldBytes = 0;
	int64 BudgetBytes = 0;
	int64 EvictionCount = 0;
	uint64 Clock = 0;
};
