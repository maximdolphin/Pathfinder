# The ground at walking distance

T429. Two floors moved together: the quadtree's, and the height function's.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -nearfield`. It parks two
kilometres east of the town, waits twenty seconds for the finest patches to
arrive, photographs the ground at eye height, and writes `out/near-field.txt`.

## What was wrong

The terrain had a hard geometric floor at **4.77 m** — `MaxDepth 15`, 64 quads a
patch, a 6,371 km planet. And the height function had one at about **33 m**: its
finest band was sampled at 150,000 on the unit sphere over four octaves, worth
9–36 m of amplitude against a 9 km ceiling. Within thirty metres of the camera
the ground was a plane, and the plane was drawn out of quarter-screen facets.

Neither could be fixed alone. More triangles interpolating a plane is a more
expensive plane; more detail than the grid can resolve is a surface that changes
every time the LOD does.

## What changed

`MaxDepth` 15 → 18, taking the finest quad to **0.60 m**. Three levels rather
than more, because the new band bottoms out around 1.9 m and triangles finer
than the field they sample are not buying anything.

A near-field band in `GeneratedElevation`: 30 m down to about 1.9 m, five
octaves, amplitude 0.00017 of `MaxElevation` — about 1.5 m. Deliberately small.
This band is the difference between ground and a plane at walking distance and
it is invisible from a kilometre up; making it larger would put bumps on
mountains that are supposed to read as mountains.

### The invariant that changed

`Elevation` now takes a `SampleSpacingMetres`, and the band fades out over a
grid too coarse to resolve it — full strength to 1 m, gone by 4 m, which lines
up with the quadtree: depth 18 and 17 carry all of it, 16 about half, 15 and
coarser none.

**The height field is now a function of the grid as well as the position.** Two
patches at different depths disagree about the ground between them by up to a
metre and a half. That is a real cost and it was taken deliberately:

- Without a near-field band the ground is a plane for thirty metres in every
  direction.
- With an unlimited one, a 4.8 m patch sampling a 1.9 m wavelength produces a
  different random surface every time it is rebuilt at a different depth. That
  is a shimmer, not a texture.
- The disagreement is bounded and already handled: LOD transitions morph (T048),
  and `SampleTerrain` reads the drawn mesh rather than the field (T061), so
  collision and queries agree with what is on screen by construction rather than
  by luck.

`LedgerPatchDisk::FormatVersion` went to 2 in the same change. The band alters
every elevation in every fine patch without altering the payload's layout at
all — exactly the case a header check would have sailed past.

## Measured

```
finest patch drawn here       38.2 m across
its quads                      0.60 m
on a 1920 px viewport at 90 degrees:
  one quad at 20.0 m              29 px  (acceptance: <= 40)
  one quad at  1.7 m             337 px  (at one's feet, not the criterion)

field, near-field band off      0.137 m
field, band on at this LOD      0.183 m
the drawn mesh                  0.186 m  (acceptance: >= 0.15)

the band on its own:
  RMS                           0.087 m
  range over the profile        0.500 m
```

The band contributes 8.7 cm RMS and half a metre of range over fifty metres —
knee-height undulation, which is what was missing. The drawn mesh tracks the
field it is built from to within 3 mm of RMS, which is the geomorph and the
stitching agreeing.

### The acceptance was wrong and is corrected in the open

T429's acceptance said *no triangle edge over 40 px standing on flat ground*,
and the first run of the fixture measured that at the camera's own feet, 1.7 m
away, where a 0.60 m quad is 337 px. It reported FAIL.

No terrain can meet that reading. 40 px at 1.7 m is a 7 cm triangle, which over
this planet is depth 24 and a few hundred million patches in view. The criterion
was written meaning the ground a standing person is *looking at*, twenty-odd
metres out, and that is what the fixture now measures — **with the figure at
one's feet printed beside it rather than dropped**, because the criterion was
mine and quietly moving it would be worse than having got it wrong.

## What the photograph also shows

`out/near-field-standing.png` is the honest result and it is worth reading for
what it does *not* fix. The ground undulates and no facets are visible on it.
Everything else in the frame is placeholder: the scatter props are black
untextured cones several times the height of a person, and they dominate the
image completely. At mid-distance the ground texture averages into mush, which
is the single-tiling-scale problem.

Neither is T429. They are T434 and T432, and the photograph is filed here as
evidence for this task and as the argument for those.

## Files

| | |
|---|---|
| `client/Source/LedgerTerrain/Private/LedgerTerrainMath.cpp` | the band, and `NearFieldStrength` |
| `client/Source/LedgerTerrain/Public/LedgerTerrainMath.h` | the argument for the changed invariant |
| `client/Source/LedgerTerrain/Public/LedgerPlanet.h` | `MaxDepth` |
| `client/Source/LedgerTerrain/Private/LedgerPatchGenerator.cpp` | where the spacing comes from |
| `client/Source/LedgerHarness/Private/LedgerNearField.cpp` | the fixture |
