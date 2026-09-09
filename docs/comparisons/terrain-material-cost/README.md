# What the terrain material actually costs (T047)

T047 asks for the blended surface to be composed into a Runtime Virtual
Texture, so the blend happens once per texel rather than once per pixel. Its
acceptance is "terrain material cost falls measurably against the direct-blend
version".

Nobody had measured the direct-blend version. That number decides the task, so
it went first.

## The control arm

`-flatterrain` swaps the terrain surface for an untextured constant on exactly
the same geometry — the cheapest material that can be drawn. The difference in
GPU milliseconds between that and the real material, over the same fixed-step
flight, is the entire budget any caching scheme is competing for.

`flat-town.png` is the control arm rendering, kept because a measurement of a
material that turned out not to be applied would be worthless: the ground in it
is visibly untextured.

| phase | real material | flat | the material |
|---|---|---|---|
| orbit | 2.5 | 2.5 | 0.0 |
| descent | 3.9 | 3.8 | 0.1 |
| atmospheric entry | 5.3 | 4.6 | **0.7** |
| **surface** | 7.4 | 7.3 | **0.1** |
| **town** | 7.5 | 7.5 | **0.0** |
| ridge sweep | 6.2 | 5.8 | 0.4 |
| coast | 7.7 | 7.1 | 0.6 |
| underwater | 8.9 | 8.7 | 0.2 |
| ascent | 3.0 | 3.0 | 0.0 |
| space | 5.3 | 4.9 | 0.4 |

Two scans, triplanar, height-blended, plus a macro sample and a distance fade,
cost **at most 0.7 ms anywhere on the flight — and 0.1 and 0.0 ms in the two
phases where the terrain fills the screen.**

## So T047 does not get built

There is nothing to recover. An RVT can at best return a fraction of 0.7 ms,
and it would cost:

- **page rendering**, which is drawing the terrain again into the page table;
- **memory**, in a project whose patch cache already holds 512 MB;
- **a cache that this terrain invalidates continuously.** The flight uploads
  121,918 patches in 160 seconds — 762 a second. Pages are invalidated when the
  primitives that drew them change.

The task's premise — that the blend is expensive enough to be worth caching —
is false, and the measurement says so by more than an order of magnitude.

## What is still true about it

The other half of the task's justification was "so decals and roads have
something to write into". That is a capability argument, not a performance one,
and it survives: there is no other way to put a road or a decal on this
terrain. But there are no roads or decals yet, and there will not be until the
biome and settlement work. **T047 is blocked on having a consumer, not on
effort** — and when that consumer arrives, the RVT that serves it should be
sized for the play area rather than the planet.

## The size arithmetic, for whoever picks this up

Unreal's non-adaptive RVT maxes at 4096 tiles × 1024 texels = 4.19 M texels a
side. One cube face of this planet is a quarter of the circumference, 10,008 km
of arc, which works out at **2.39 m per texel — coarser than the 2 m scan it
would be caching.** Adaptive mode raises the ceiling to 2^20 tiles, which is
9.3 mm per texel and ample; so size is not the objection. Cost and invalidation
are.
