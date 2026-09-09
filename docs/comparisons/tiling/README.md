# The tiling grid that is not there

T431, closed by measurement rather than by code.

## The premise

The task was written on this reasoning, from `roadmap_data_surface.py`:

> A 2 m tile over a 305 m patch is 152 repeats each way, unrotated and
> unoffset. The eye finds that grid immediately, and it is the most
> recognisable tell of procedural ground.

The proposed fix was stochastic sampling — three hex-grid taps per texture, each
with a noise-driven offset and rotation, blended by barycentric weight. Three
times the samples on every ground layer, which is why the acceptance asked for a
measurement rather than an opinion.

## The measurement

An overhead capture from eight metres. At 90° horizontal that frames 16.0 m
across 1920 px — **120 px per metre, so a 2 m tile repeats every 240 px**. That
is the right scale to look for this: the acceptance said a 200 m capture, but at
200 m across a 2 m tile is 19 px and a repeat that small cannot be told from
noise.

Scatter off, so the stones are not in the signal. Luminance of the middle fifth
of the frame, where the ground is closest to face-on.

Raw autocorrelation decays smoothly and shows nothing at 2 m, but that is
dominated by the macro bands and the lighting gradient, which would hide a tile
peak under them. High-passed at 4 m to leave only structure finer than the
macros:

```
  lag       correlation
  1.00 m      +0.0142
  1.90 m      -0.0084
  2.00 m      -0.0151   <-- the tiling period
  2.10 m      +0.0022
  4.00 m      -0.0073   <-- and its first harmonic

noise floor past 1 m: mean -0.0063, sd 0.0099
peak at the 2 m tile:  -0.0151  =  0.9 sd BELOW the floor
```

There is no peak. The correlation at the tiling period is slightly *below* the
average of everything around it. Whatever else is wrong with this ground, it is
not repeating at 2 m in a way any measurement can find.

## Why not

Four things are already breaking it up, none of them put there for this reason:

- **Triplanar.** Three projections of the same world position, at different
  phases, blended by the surface normal. On ground of any relief none of them
  dominates completely, so the repeat of any one is diluted by two others that
  repeat out of step with it.
- **Three ground slots at once.** The biome palette blends three surface sets by
  height, and their tiling distances come from each scan's own metadata — 2 m,
  2 m and 1 m, or 0.5 m. Periods that are not equal do not reinforce.
- **The 90 m and 1.2 km macro bands**, each mean-normalised and multiplied in.
  Their least common multiple with a 2 m tile is far past the horizon.
- **Vertex colour**, which carries the biome and varies continuously across
  every patch.

## What was not done, and when to do it

No stochastic sampling. It would have tripled the texture samples on every
ground layer to fix something the measurement says is not happening, and a
three-times cost on the most-shaded surface in the game is not something to pay
on a hunch.

The measurement is the deliverable, and it is repeatable: if the material ever
loses its triplanar projection, or drops to one ground slot, or the macro bands
are removed, run the same autocorrelation and look for a peak at 240 px before
assuming the ground is fine.

## Files

Nothing changed. The capture is `out/near-field-down-noscatter.png`, taken with
`-nearfield -noscatter`.
