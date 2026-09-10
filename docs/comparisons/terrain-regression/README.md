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
| `viewport_width` / `_height` | the resolution every number above is a function of |

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

`budget_p99_ms` is set at 90 against a clean run of 70 at 1920x1080, and
**M02's gate is 16.7**. That is not the gate being quietly relaxed — the gate is
in the roadmap, it is failing, and `docs/comparisons/m2s-cost/` says so. This threshold exists so that a
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
| holes | `-breakthreshold` | `ErrorThresholdPixels` back to 150, which is the regression that actually happened |
| cracks | `-breakstitching` | every patch edge declared un-stitched, so a patch beside a coarser neighbour keeps vertices its neighbour does not have |
| popping | `-breakmorph` | `MorphScale` zero, so a collapsing node snaps the whole transition |
| budget | `-breakthreshold -nopatchdisk` | the same demand, with every one of those patches generated rather than loaded |

`--demonstrate` runs the flight five times — clean, then once per fault — and
reports which were caught.

```
fault      verdict        watched
clean      pass           -
holes      caught         holes_worst
cracks     caught         cracks_stitch_rejects
popping    caught         pop_morph_enabled
budget     caught         budget_p99_ms

VERDICT: PASS
```

### It took three attempts, and the failures taught more than the pass

**`-nolodbrake` does not produce holes.** It used to. Raising
`ErrorThresholdPixels` to 250 cut demand enough that the brake is no longer what
stands between the tree and the pool, so removing it changes very little. A
fault that has stopped faulting is worse than no fault at all, and the
demonstration reported it MISSED.

**`-smallpool` does not produce holes either**, and this one is genuinely
informative: four hundred sections instead of 3,600, with the brake off, ran
**faster than clean** — p99 20.6 ms against 42 — with `holes_worst` at zero. A
starved pool makes the streamer stop *asking* rather than ask and be refused, so
nothing is recorded as unfilled and the terrain simply goes coarse. That is also
the corrected brake doing its job.

**The first two runs measured at 1280×720.** Clean p99 read 31.8 there against
70 at 1080p, which is a margin so wide that turning the disk cache off could not
cross a threshold of 110 — the budget fault was reported MISSED for that reason
alone. This is the same mistake that hid the holes bug in T429, made again, by
the tool written to stop it happening. `viewport_width` and `viewport_height`
are now in the block, permanently.

What works is `-breakthreshold`, which reproduces the regression that actually
happened rather than an invented one: high demand, seven thousand sections
wanted against a pool of 3,600. `holes_worst 1801`, `budget_p99_ms 93.3`.

## Files

| | |
|---|---|
| `tools/terrain_regression.py` | the thresholds, the checks, the demonstration |
| `client/Source/LedgerHarness/Private/LedgerPerf.cpp` | the machine-readable block |
| `client/Source/LedgerTerrain/Private/LedgerPatchStreaming.cpp` | the crack and pop counters, and `-breakstitching` |
| `client/Source/LedgerMaterial/Private/LedgerTerrainMaterial.cpp` | `-breakmorph` |
