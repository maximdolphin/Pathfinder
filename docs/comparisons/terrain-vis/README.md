# The terrain, coloured by what it is doing

T066. `-terrainvis=lod|patch|collision|biome|climate|cache`.

The acceptance is that a terrain bug is diagnosable *without adding code*. This
project's history is the argument for it: `DirectionToFace` used as a sphere
inverse in three separate systems, three vacuous "nothing floats" tests in a
row, four night-side captures, an `UnfilledNodes` counter that three consecutive
fixes each moved by under two per cent. Every one of those was a round of
writing new instrumentation to answer a question a picture would have answered.

## How it works

The diagnostic is written into the patch's vertex colours **at generation time**,
because that is the only place that knows most of these — whether the patch came
off disk, whether it was asked for with collision, how deep it is — and none of
them survive into the material. The material then draws vertex colour as
emissive and unlit, because a LOD ramp multiplied by a sun angle is not a LOD
ramp.

Painted **after** the disk store, deliberately: what goes in the cache is the
real patch, so a run with `-terrainvis` does not poison the next run without it.

| mode | what it shows |
|---|---|
| `lod` | depth on a blue-green-yellow-red ramp against `MaxDepth` |
| `patch` | a hash colour per patch, so every boundary is a hard edge |
| `collision` | green where a body could stand, red where it would fall through |
| `biome` | the three strongest palette weights as red, green and blue |
| `climate` | temperature in red over −40…40 °C, moisture in blue |
| `cache` | blue served from disk, orange generated this run |

The counters that are one number rather than a field — the streaming queue, the
section pool, the LOD brake's current threshold — stay in
`ALedgerPlanet::LogStats`. A single number is a worse picture than a sentence.

## What the first six captures show

**`lod`** — red underfoot out to about thirty metres, orange beyond. The rings
are exactly where the arithmetic in T429 says they should be, and a LOD bug
would be a ring in the wrong place or a ring with a bite out of it.

**`collision`** — green to well past the mid-distance, red on the far mountain.
Reading a hole in the collision ring off this takes no code at all, which is the
whole point.

And a thing found for free, which is the acceptance demonstrating itself: the
collision view makes the **scatter banding** legible. The stones form chains
across ground that is uniformly green, so they are not stopping at the collision
boundary as had been assumed in `docs/comparisons/rocks/` — they are following
something else, on ground that has collision everywhere. That assumption was
written down twice tonight and this picture disproves it in one glance.

## Files

| | |
|---|---|
| `client/Source/LedgerTerrain/Public/LedgerTerrainVis.h` | the modes |
| `client/Source/LedgerTerrain/Private/LedgerTerrainVis.cpp` | the painter |
| `client/Source/LedgerTerrain/Private/LedgerPatchGenerator.cpp` | one call, last, after the store |
| `client/Source/LedgerMaterial/Private/LedgerTerrainMaterial.cpp` | unlit vertex colour |
