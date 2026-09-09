# Biomes

One file per biome. Adding a biome is adding a file here; nothing recompiles.

```json
{
  "name": "Salt flat",
  "temperatureC": 28.0,
  "temperatureToleranceC": 7.0,
  "moisture": 0.04,
  "moistureTolerance": 0.06,
  "maxSlopeDegrees": 8.0,
  "surfaceSet": "rocky_sand_vd4pbdt",
  "tint": [0.90, 0.88, 0.82],
  "scatterDensity": 0.0
}
```

`name` and the four climate fields are required, and a file missing any of them
is rejected with a message naming the field — a biome that quietly defaulted to
the middle of climate space would win everywhere and look like a bug in the
climate model. The rest default: no slope limit, no tint, no scatter.

Tolerance is one standard deviation of a Gaussian falloff, not a hard edge.
Biomes overlap on purpose; the overlap is what makes a boundary a gradient
rather than a line drawn on the ground.

`surfaceSet` is a directory name under `client/Content/Surfaces`.

Config rather than Content because the cook deletes loose non-asset files from
`Content/` — see `docs/comparisons/packaged/pso/README.md`, which is the bug
that taught us that.
