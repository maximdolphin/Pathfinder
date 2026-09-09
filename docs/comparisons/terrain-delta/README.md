# Terrain modification (T062)

Levelling, excavation and fill, as a sparse delta over the generated field.

Run it: `-flattenpad`. It parks the ship on the town site, levels a forty-metre
pad, and answers all three clauses of the acceptance in one go.

## The acceptance

> Flatten a pad, reload, and it is still flat; the delta costs nothing where
> nothing was modified.

```
pad: 40 m flat with 20 m of blend, brought to 661.79 m
the ground it replaced varied by 4.22 m across the pad

after levelling: 317 samples over the pad, worst 0.0000 m from the target
reloaded from disk: 1 edits, 21 samples along the pad, worst 0.0000 m
200000 height samples away from the pad: 90.6 ms without the delta,
                                         89.5 ms with it (-1.2%)
```

The flatness is measured through `SampleTerrain` — T061's API, which reads the
drawn mesh and is itself verified against physics traces to twenty-two microns.
So "it is flat" means the geometry a player would stand on is flat, not that a
function returns a constant.

`-1.2%` is measurement noise. The delta is present, loaded, and applied; away
from the pad it costs nothing measurable.

## Why an edit is not a heightmap

Storing modified ground as samples means storing it at a resolution, and the
terrain has no single resolution — it has fifteen. A delta stored per texel is
wrong at every LOD but one and enormous at the finest.

So an edit is stored as what it is: a place, a radius, a falloff and a height to
bring the ground to. Evaluating it is a distance and a lerp, it is correct at
every LOD because it is not a sampling of anything, and one edit is six numbers
on disk.

The falloff is not decoration. Without it a levelled pad is a cylinder punched
into a hillside with a vertical wall round it.

Everything downstream follows for free — collision, the sampling API, scatter,
the biome weights — because they all read the same height function. Levelling
drops every patch, live and cached, and the next stream rebuilds them.

## The bug this found, twice in one evening

The first version bucketed edits with `SphereToFace`, the honest inverse of the
cube-sphere projection. It is a forty-step fixed point, and it ran on **every
height sample on the planet**:

```
200000 height samples away from the pad: 89.5 ms without the delta,
                                         192.4 ms with it (+115.0%)
```

That is the exact opposite of the acceptance's second clause. The same mistake
had been made an hour earlier in the quadtree's stitch probes, where it took the
terrain's game-thread cost from 6.6 ms to 16 (`docs/comparisons/projection/`).

A bucket does not have to be a *place*. It has to be *consistent* between the
insert and the lookup, and `DirectionToFace` is consistent, cheap, and distorts
the bucket's shape smoothly — which does not matter, because the insert covers
an edit's extent by probing nine points around it rather than by trusting the
lattice's geometry.

The other half was an arccosine per edit per sample. It is now a dot product
against a precomputed cosine, and the arccosine only runs for the handful of
samples actually on the pad.
