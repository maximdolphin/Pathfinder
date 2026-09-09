# Scatter: rocks, vegetation and debris (T057)

Deterministic placement from the biome rules, instanced, generated on the worker
with the patch that carries it.

## The acceptance

> Ten thousand visible instances at 60 fps, identical placement across runs, and
> nothing floating or half-buried.

**42,408 instances at a median 16.6 ms — sixty frames a second**, measured at
the three-biome site the T053 fixture finds, over 2,296 sampled frames with the
screenshot frames excluded:

```
scatter: 42408 instances over 318 patches, 2 buckets rebuilt in 0.51 ms
biome site frames: 2296 sampled, median 16.6 ms (60 fps), p99 28.7 ms
```

Wall clock, not `DeltaSeconds`: the fixture runs under `-useFixedTimeStep`, so
the engine's delta is a constant by construction and would have reported a
perfect sixty however slowly the frame actually ran.

**Identical placement across runs** is `Ledger.Scatter.SamePatchSamePlacement`,
and it compares bit-for-bit rather than nearly. Every instance's position comes
from a hash of the patch key and a cell index and nothing else — no stream, no
clock, no iteration order — so anything that makes two runs differ at all makes
them differ visibly, and a tolerance would hide exactly that.

**Nothing floating or half-buried** is `Ledger.Scatter.NothingFloatsOrIsHalfBuried`,
and getting that test to mean anything took three attempts. It is worth the
space.

## Three vacuous passes in a row

The first version placed instances at the exact height function and then checked
them against the exact height function. It passed, and it would have passed
whatever the mesh looked like.

The second version compared against the mesh — take an instance, find its grid
cell, interpolate. That needs to invert the projection, and `DirectionToFace`
inverts `FaceToCube` but **not** `CubeToSphere`'s area-evening warp. Every
instance fell outside `[0,1]` and was skipped by the bounds check, and the test
reported a worst gap of exactly 0.000 m over four hundred instances it never
looked at. The hydrology lattice was wrong the same way on the same day, which
is the argument for writing this down rather than just fixing it.

The third version picked the first patch of land it found. That patch was flat,
and a bilinear interpolation of flat ground is exact, so the answer was 0.000 m
again — this time correctly, about a plain.

The version that works searches for the *roughest* patch on a cube face, and
measures two things that need no inverse: how far the mesh wanders from the
height function at cell centres, and how far the placement moves when the mesh
is available versus when it is not.

```
400 instances; mesh wanders 1.222 m from the height function,
and the scatter moved by up to 0.518 m to follow it
```

**And that found a real bug.** The mesh is a bilinear interpolation of the
height field sampled every 4.8 m, and on the roughest patch this planet has it
sits up to **1.22 m** away from the function it interpolates. A tree placed at
the exact value is therefore up to 1.22 m above or below the ground a player can
see. Placement now reads the patch's own vertices, so an instance is on the
surface that is drawn rather than the one it was derived from.

## How it stays in the frame

One instanced component per variant is two draw calls and a rebuild that costs
**13.4 ms** at a forest site, because every arriving patch invalidates all
78,000 instances and there is no stable handle for an instance in a
`UHierarchicalInstancedStaticMeshComponent` — removing one renumbers the rest.
One component per patch is a free rebuild and four hundred draw calls.

Sixteen buckets, hashed by patch key so neighbours land in different ones, is 32
draw calls and a rebuild of a sixteenth; at most two buckets are rebuilt per
frame. Measured: **0.3 to 1.2 ms**, against 13.4.

The cost of that is a queue. While streaming hard, a patch can wait up to eight
frames — an eighth of a second — with ground and no trees on it. That happens at
the edge of the collision radius, where nothing is close enough to notice.

## What it looks like, and what is wrong with it

`forest.png`. Density comes from the biome, so the same climate field that
paints the ground plants the trees, and the desert the scripted flight lands in
gets three per cent density while this site gets one.

**There is a hard edge where the forest stops.** Scatter is placed only on the
finest patches — the ones that carry collision — and beyond that ring there is
nothing at all rather than something smaller or an impostor. It is visible in
the middle distance of `forest.png` as a curved line with trees on one side and
bare ground on the other. That is T059's business (far-field impostors and
horizon detail) and it is named here so that nobody reads this picture as
finished.

The meshes are the two trees the settlement plants, borrowed. The biome files
already have somewhere to name their own rocks and shrubs; until those meshes
exist, borrowing these is honest about what is being placed and lets the
placement itself be measured.
