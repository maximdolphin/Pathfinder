# What the surface fidelity milestone cost

T436. Four flights, same machine, same cache state, one control arm each.

## The runs

| | overall mean | overall p99 | cache |
|---|---|---|---|
| everything on | 21.7 ms | 72.7 ms | 99% from disk |
| `-noscatter` | 21.9 ms | 70.8 ms | 99% |
| `-noparallax` | 21.9 ms | 65.9 ms | 99% |
| `-flatterrain` | 25.0 ms | 82.3 ms | **20%** |

Per phase, mean / p99 / GPU / terrain tick:

| phase | run | mean | p99 | gpu | terrain |
|---|---|---|---|---|---|
| surface | full | 16.7 | 17.8 | 8.8 | 1.22 |
| | `-noscatter` | 16.7 | 18.1 | 8.7 | 1.28 |
| | `-noparallax` | 16.7 | 17.8 | 8.8 | 1.22 |
| ridge sweep | full | 38.1 | 89.0 | 10.8 | 20.52 |
| | `-noscatter` | 42.6 | 85.8 | 10.8 | 23.10 |
| | `-noparallax` | 39.5 | 79.2 | 11.0 | 20.86 |

## What the milestone's material work costs: nothing measurable

**Scatter is free.** Turning off every stone on the planet moves the overall mean
by 0.2 ms in the *wrong* direction and the ridge sweep's terrain tick by 2.6 ms,
also the wrong way. Both are inside run-to-run variance. Instanced 320-triangle
meshes with no collision, culled by a hierarchical cluster tree, cost what the
theory says they cost.

**Parallax is free.** Same story: 0.2 ms of mean, in the wrong direction, and a
p99 difference well inside the noise. One extra triplanar sample faded out at
15 m does not show up.

**The world-space normal blend is free**, by construction — it is the same three
texture samples with a swizzle and a normalise on top.

So the entire M2S material programme — parallax, the corrected fades, the
world-space normals, three surface layers of stones — is inside the noise floor
of this measurement.

## `-flatterrain` is not a valid control arm for this, and the data says so

It looks like the material costs 57 ms at the surface phase: 73.5 ms mean
against 16.7, with the terrain tick at 41.48 against 1.22. It does not.

The report's own cache line gives it away. `-flatterrain` runs at **20% served
from disk** where every other run is at 99%. It swaps the surface for an
untextured constant, which drops the biome set, which changes the patch cache's
content key — so the entire flight regenerates instead of loading. Its 41 ms
terrain tick is generation cost, not material savings, and it is a CPU number
standing in for a GPU question.

Recorded rather than quietly dropped, because the number is large and plausible
and would have been believed. A proper material control arm has to hold the
generation inputs fixed and change only what is shaded; `-flatterrain` was built
to answer a different question (T047) and answers this one wrongly.

GPU time is the honest proxy in the meantime, and it sits between 8.3 and
11.0 ms in every run including the flat one.

## The bill is the geometry, and it is T429

The whole overrun is the CPU terrain tick, and the terrain tick is the quadtree
walk and the streaming decisions:

```
ridge sweep terrain tick    before T429   9.4 ms
                            now          20.5 ms
```

Three extra depth levels are three more annuli of nodes, and a sweep along a
ridge at speed is where the visible set is widest. Near the camera the same
change made things *faster* — the surface phase's tick is 1.22 ms against 6.5
before — because the disk cache now serves the fine patches. It is the far half
of the visible set that got expensive.

## On the honesty of p99 here

Across this session the same flight has reported an overall p99 of 34.4, 57.6,
61.5, 65.9 and 72.7 ms at various configurations, and the differences between
adjacent configurations are not always larger than the differences between
repeats of the same one. **Mean is the number to reason about** — it moved 19.7
to 21.7 across the whole milestone — and p99 on this fixture is currently too
noisy to attribute a few milliseconds to a cause.

That is itself a finding: the flight is not yet a precise enough instrument for
the questions M2S wants to ask of it, and a quieter one is worth more than
another optimisation measured against this one.

## The standing position

M02's gate is *no frame over 16 ms on the transect*. It was failing at 45 ms p99
before this milestone started and it is failing at 72.7 now. The material work
is free. The geometry work is not, is unpaid, and the next place to look is the
quadtree walk itself rather than anything M2S added on top of it — with the
caveat that the per-depth threshold that would have cut it broke `SampleTerrain`
(see `LedgerQuadTree.cpp`), so there is a real defect in the leaf lookup waiting
underneath.

## Files

Nothing changed. `out/perf-{full,noscatter,flatterrain,noparallax}.txt`.
