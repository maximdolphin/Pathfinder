# Four failure modes, four build failures

T067. M02's gate names four things that must not happen on the transect —
holes, cracks, popping and budget overruns — and until this task none of them
could fail a build.

```bash
python tools/terrain_regression.py --check          # the last run
python tools/terrain_regression.py --demonstrate    # every fault, run and caught
```

## The metrics

The flight writes a `key value` block at the end of `out/performance.txt`, with
no prose in it, so a script does not have to parse a table meant for reading.

| key | what it is |
|---|---|
| `holes_worst` | sections the tree wanted and the pool could not give it, at the worst frame |
| `cracks_stitch_rejects` | cached patches thrown away because their edge stitching no longer matched their neighbours' depths |
| `pop_morph_enabled` | whether the LOD transition is eased through rather than snapped |
| `pop_hidden_quads` | how far the surface moves at the largest transition, in that patch's own quads |
| `budget_p99_ms` | the 99th percentile frame |

Two of them are new instrumentation. `cracks_stitch_rejects` counts a comparison
the streaming code was already making to decide what to rebuild — every one of
those rejections was a seam on screen until the rebuild landed, and nobody was
counting them. `pop_hidden_quads` reads `MorphUVs.x`, which is the distance from
a vertex to where its parent would put it, and is therefore exactly the size of
the pop that morphing exists to hide.

### The pop metric was wrong in metres

It first read **2,307 metres** on a clean run, which looks like a catastrophe
and is not one. It was the root node: a patch ten thousand kilometres across has
a colossal transition between it and its parent, and it is never a pop, because
nothing is ever close enough to one for it to subtend anything.

Divided by the patch's own quad size, the number is scale-free and comparable
between depths, which is what a threshold needs. In quads a large number means
the LOD is taking bigger steps than the morph window can smooth, which is the
thing worth failing a build over.

## The thresholds, and why they are not the gate

They live in `tools/terrain_regression.py` next to the paragraph arguing for
each, rather than in a data file. A threshold is a claim about what is
acceptable; a number without its argument is a number somebody raises the next
time it fails.

`budget_p99_ms` is set at 110, and **M02's gate is 16.7**. That is not the gate
being quietly relaxed — the gate is in the roadmap, it is failing at 72–76 ms,
and `docs/comparisons/m2s-cost/` says so. This threshold exists so that a
*regression* fails the build today, while the standing overrun is tracked where
standing overruns belong. A CI check pinned to a number the build has never met
is a CI check that is red from the day it lands and is therefore ignored.

`holes_worst` is 40 rather than 0 for a related reason: that counter is a
horizon test rather than a frustum test and overcounts by a margin nobody has
pinned down (`docs/comparisons/overload/`). Set at the number its name implies
it would fail on a clean build.

## Proven by breaking it

A regression suite that has never been shown to fail is a regression suite
nobody should trust, so each failure mode has a switch that introduces it:

| fault | switch | what it does |
|---|---|---|
| holes | `-nolodbrake` | the LOD brake off, so the tree out-subdivides the section pool |
| cracks | `-breakstitching` | every patch edge declared un-stitched, so a patch beside a coarser neighbour keeps vertices its neighbour does not have |
| popping | `-breakmorph` | `MorphScale` zero, so a collapsing node snaps the whole transition |
| budget | `-nopatchdisk` | the disk cache off, so every patch is generated |

`--demonstrate` runs the flight five times — clean, then once per fault — and
reports which were caught. Its verdict is in `out/terrain-regression.txt`.

## Files

| | |
|---|---|
| `tools/terrain_regression.py` | the thresholds, the checks, the demonstration |
| `client/Source/LedgerHarness/Private/LedgerPerf.cpp` | the machine-readable block |
| `client/Source/LedgerTerrain/Private/LedgerPatchStreaming.cpp` | the crack and pop counters, and `-breakstitching` |
| `client/Source/LedgerMaterial/Private/LedgerTerrainMaterial.cpp` | `-breakmorph` |
