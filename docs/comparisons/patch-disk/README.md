# The disk cache for generated patches

T065. Acceptance: *a second visit to the same ground generates nothing and is
bit-identical.*

Run it: nothing to run. It is on by default; `-nopatchdisk` turns it off for a
measurement that wants to see generation happen. The cache lives in
`client/Saved/PatchCache`, which is gitignored — it is derived from the project,
not part of it.

## What is stored, and why it is not the patch

A finished patch is about 250 KB of interleaved vertex data. There are roughly
35,000 leaf patches over this planet, so caching finished geometry would be
nearly nine gigabytes: not a cache, a second copy of the planet.

What actually costs time is the noise. 4,225 elevation samples per patch, plus a
climate grid whose every point marches forty steps upwind sampling the height
field again. Positions, normals, tangents, triangles and morph targets are
arithmetic on top of that, and arithmetic is not what anybody is waiting for.

So the payload is the elevation grid, the vertex colours the climate produced,
the palette and the scatter. Measured on the flight below: **804 MB for 15,791
patches, 51 KB each.** The rest is recomputed on load. "Generates nothing" in the
acceptance means *evaluates no noise*, which is the thing that costs.

## Bit-identical: the unit test

`Ledger.PatchDisk.SecondVisitGeneratesNothingAndIsIdentical` builds one patch
twice and FNV-1a hashes everything that came out — vertices, triangles, normals,
UVs, morph UVs, colours, water vertices, water colours — then compares the two
hashes for equality. Not near-equality. A tolerance here would be a licence for
a cached patch not to be the patch.

It also asserts the counters: the first visit is one miss and one write, the
second is one hit and no write. And that the patch was not trivially empty,
which is how the last three "nothing floats" tests managed to pass while
measuring nothing.

The planet it uses is seeded from the clock, so its ground has provably never
been generated on this machine before. The first version of this test deleted
the cache directory instead and failed on its own leftovers: `IFileManager::Delete`
removes files, not trees, so the delete silently did nothing, a patch from an
earlier session was still sitting there, and the run that was supposed to be cold
opened with a cache hit.

`Ledger.PatchDisk.ChangingAnythingChangesTheKey` covers the other half. A field
missing from the content key is a patch served from a cache that no longer
describes it, and the failure would look like terrain rather than like a cache
bug. Eleven inputs, each changed alone, each required to change the address:
face, position, extent, grid, a stitch flag, seed, sea level, peak elevation,
season, collision, an edit to the ground, and a biome.

## Generates nothing: the flight

The scripted flight, twice, back to back, on an idle machine.

| | cold | warm |
|---|---|---|
| patches served from disk | 601 / 16,391 — **4%** | 13,759 / 18,855 — **73%** |
| terrain tick, surface | 12.21 ms | **6.52 ms** |
| terrain tick, town | 12.07 ms | **5.79 ms** |
| terrain tick, ridge sweep | 18.96 ms | **9.39 ms** |
| overall p99 | 45.0 ms | **34.4 ms** |
| overall mean | 22.4 ms (45 fps) | **19.7 ms (51 fps)** |

The terrain tick roughly halves everywhere it was expensive. That is the whole
claim, and it is the second half of the acceptance: on ground it has seen, the
generator evaluates no noise.

### Why 73% and not 100%

Worth saying rather than rounding up. The flight is scripted but not
frame-identical: the warm run reached the surface sooner because it was not
waiting on generation, so it flew a slightly different path and looked at ground
the cold run never resolved. It wrote 5,095 new entries doing so.

There is also a structural reason. The content key includes the four stitch
flags, because a stitched edge collapses vertices and changes the elevations
themselves rather than only the indices. The same ground with a
differently-subdivided neighbour is therefore a different entry, correctly. A
run that approaches a hill from a different distance will re-generate patches it
has "already seen".

Neither is a defect. 73% on a path that was never meant to be repeated exactly
is the honest number for what this buys in practice, and the unit test is what
proves the exact claim.

## What this does not do

**The cache is unbounded.** 804 MB after one flight over a small part of one
planet. Nothing evicts, nothing caps it, and a few hours of flying in different
places will fill a disk. The format version is in the key rather than in a
header, so a layout change orphans old entries instead of misreading them — but
orphaned entries are not deleted either, they simply stop matching. An LRU cap
is the obvious fix and it is not in this task.

**It is per machine.** Nothing here ships or is shared. That is deliberate:
it is derived data, and derived data that travels is derived data that can
disagree with the thing that derived it.

## Files

| | |
|---|---|
| `client/Source/LedgerTerrain/Public/LedgerPatchDisk.h` | the interface, and the argument for the payload |
| `client/Source/LedgerTerrain/Private/LedgerPatchDisk.cpp` | content key, load, store, counters |
| `client/Source/LedgerTerrain/Private/LedgerPatchGenerator.cpp` | four hooks: load, use, skip, store |
| `client/Source/LedgerTerrain/Private/Tests/LedgerPatchDiskTests.cpp` | both tests above |
| `client/Source/LedgerHarness/Private/LedgerPerf.cpp` | the counters, in the flight's report |
