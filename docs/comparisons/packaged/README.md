# The packaged build, and the two reasons it did not match (T427)

ARCH Rule 6: the milestone ends with something that runs. A build with no
editor, flying orbit to ground to orbit, producing captures that match the
committed references and a frame-time report no worse than the editor's.

It failed on four of eight captures for a long time, with two written
explanations, neither of which had been tested. Both were wrong. There were two
real causes and they were found by testing rather than by reasoning.

## One: the fixture could not reproduce itself

Run the editor build twice and compare it against itself: seven of eight shots
disagree, the coast by a mean of 7.67 — against the 8.26 that had been blamed
on the packaged build. See `../repeatability/`. `-useFixedTimeStep -fps=60`
fixes it, and the phase frame counts then come out at exactly duration × 60 in
both builds.

## Two: the packaged build had no clouds

With the fixture pinned, four shots still failed, and the same four — the ones
with sky in frame. The picture said it immediately:
`no-clouds/terrain-town.png` is a clear blue sky where the reference has a full
cloud deck. The packaged log said it too:

```
LogStreaming: Warning: LoadPackage: SkipPackage:
  /Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst
  - The package to load does not exist on disk or in the loader
LogUObjectGlobals: Warning: Failed to find object
  'Object /Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst...'
```

`UVolumetricCloudComponent` takes its material from its own class default.
Nothing in this project names that asset, so the cooker never saw it. Adding
`/Engine/EngineSky` to `DirectoriesToAlwaysCook` is the whole fix — the same
mechanism, and the same class of mistake, as the project's own generated assets
in T425.

| shot | no clouds | clouds cooked |
|---|---|---|
| coast | mean 17.27, worst 84 | mean 3.90, worst 20 |
| surface | mean 5.99, worst 135 | mean 0.70, worst 97 |
| sweep | mean 14.89, worst 122 | mean 0.30, worst 28 |
| town | mean 4.20, worst 98 | mean 0.86, worst 101 |

## The gate

`compare_captures.py` against the committed references, which is what T427
gates on:

```
terrain-coast.png       residual 0.78    terrain-space.png       residual 0.00
terrain-entry.png       residual 0.00    terrain-surface.png     residual 0.71
terrain-orbit.png       residual 0.02    terrain-sweep.png       residual 0.34
terrain-town.png        residual 0.89    terrain-underwater.png  residual 0.00
captures match the reference
```

Eight of eight, worst 0.89 against a limit of 2.50.

Frame times, packaged against the editor over the same flight:

| | packaged | editor |
|---|---|---|
| mean | 16.6 ms | 18.0 ms |
| p99 | 27.6 ms | 29.0 ms |
| frames | 8,760 | 8,760 |

The packaged build is not worse; it is slightly better. Both still fail the
16.7 ms budget on about half of frames — that is the frame budget's own
problem, tracked under the terrain work, and not what this task asks.

## Files

- `with-clouds/` — the passing run: report, town frame, comparison output.
- `no-clouds/` — the same run before the cook fix, kept because the town frame
  is the clearest single piece of evidence in this whole investigation.
- `performance-run1.txt`, `performance-run2.txt`, `performance.txt` — earlier
  free-running packaged reports, from before any of this was understood.
