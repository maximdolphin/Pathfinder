# The inverse the cube-sphere never had

`FaceToCube` maps face coordinates to a point on a cube. `CubeToSphere` warps
that cube onto a sphere, evening out the areas. `DirectionToFace` inverts
**`FaceToCube`** — and nothing inverted `CubeToSphere`.

So `DirectionToFace(CubeToSphere(FaceToCube(f, u, v)))` does not return
`(f, u, v)`. It is out by up to **0.0657 of a face**, worst at the quarter
points and zero at the centre and the edges. On a cube edge of 10,008 km that is
**657 km**.

`Ledger.Projection.DirectionToFaceIsNotTheSphereInverse` pins that number so
that nobody quietly swaps the two back, and
`Ledger.Projection.SphereToFaceRoundTrips` holds the new one to 6.5e-13 of a
face — six microns.

## Where it mattered

`ALedgerPlanet::LeafDepthAt` answers "how fine is the terrain at this
direction", and edge stitching is decided by asking it about a point just
outside each edge of a patch. Those points come from `UnitSphereAt`, which
applies `CubeToSphere`. `LeafDepthAt` then inverted with `DirectionToFace`.

**So every stitch decision was made by looking at ground up to 657 km away.**
The same class of bug had already been found twice the same day, in the
hydrology lattice's neighbour lookup and in a scatter test that silently skipped
every instance it was supposed to check. Three times is a missing function, not
three mistakes, so `SphereToFace` now exists.

## And what it costs

`SphereToFace` has no closed form. It is a fixed point — forty undamped
iterations of forward-project-and-correct — and putting that in the stitch path
took the terrain's game-thread cost from 6.6 ms to **16.0 ms** in the ridge
sweep. That is most of a frame, to answer a question the caller already knew the
answer to: the probes have face coordinates in hand and only went out to a
sphere direction so that `LeafDepthAt` could turn them back.

`LeafDepthAtFace` takes them directly. An out-of-range coordinate names a point
on the face's extended plane, and which face it truly belongs to is whichever
cube coordinate is now largest — exact, and a divide.

| | ridge sweep | town | surface |
|---|---|---|---|
| before, with the wrong probe | 6.62 | 1.30 | 1.28 |
| the fixed point in the hot path | 16.00 | 9.84 | 9.69 |
| face coordinates, no round trip | 8.40 | 3.90 | 3.76 |

**It still costs about 1.8 ms in the ridge sweep, and that is not explained.**
The captures are pixel-identical before and after, so the change is not
geometric. The likely cause is the stitch flags now being *right*: a drifted
probe usually landed on distant, coarse ground and returned "the neighbour is
coarser", so nearly every patch stitched. Correct flags vary, a cached patch
whose flags no longer match is regenerated, and the cache is running at 57%
reuse. That is a hypothesis with a plausible mechanism and it has not been
measured — the honest place to record it is here rather than in a commit message
that asserts it.

Raw reports: `before.txt`, `after.txt`.
