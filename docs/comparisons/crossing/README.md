# Crossing to another body in one session

T088's second clause. Watch a moon from the ground, fly to it, land on it,
**without restarting the process**.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -crossing -RenderOffScreen`
(`-crossto=<n>` picks a different destination.)

## What the world had to learn

A world was built once, during `OnWorldBeginPlay`, around one body — its
terrain, its sky and its town are all functions of which body that is. The only
way to stand on a different one was to start a new process with `-body=`, which
is not what "in one session" means.

So the tail of `OnWorldBeginPlay` became `BuildWorldFor(UWorld&)` — 457 lines
moved, nothing changed — and `SwitchToBody(int32)` destroys the planet, the
atmosphere and the settlement, then lets the next tick build the replacement.
**This is the first piece of M07 arriving early**, and it arrives because T088
asks for it.

```
crossing: Home to Companion, 361622 km, quoted 5.48 hours at a gravity;
the far end pulls 1.28 m/s2 and holds no air
site: body 1, sun.site 0.865, solar altitude 59.8 deg
crossing 1/6: watch        2/6: departure        3/6: cruise
switching from body 1 (Home) to 3 (Companion)
site: body 3, sun.site 0.946, solar altitude 71.1 deg
crossing 4/6: arrival      5/6: descent          6/6: landed
exit: 0
```

| | |
|---|---|
| from | Home |
| to | Companion |
| distance | 361,622 km |
| quoted | 5.48 hours at a gravity |
| world switched | yes |
| standing on | body 3, radius 1976 km |
| gravity there | 1.28 m/s² (0.130 g) |
| atmosphere | none |

The trip is quoted by T087's map before anything moves, the clock is run forward
by T085's accelerator, and the world changes bodies underneath a running game.

## The crash, and how it was finally seen

For a dozen runs the process took an access violation on the render thread about
fourteen seconds after the switch, with a breadcrumb saying nothing more
specific than `SceneRender / ViewFamilies`. Guessing at it produced four real
fixes and no answer (below). What produced the answer was `-forcelogflush`: with
it, the last line written before the fault was

```
LogUObjectBase: Warning: Object is not registered
```

immediately after a biome palette was created. That named the culprit.
`LedgerBiomeSurfaces.cpp` kept a function-static `TMap<FString, FSurfaceSet>` of
**raw `UTexture2D*`**, and nothing rooted them. The first world's materials died
with it, the collector took the textures the cache was still pointing at, and
the second world built its palettes on freed memory.

The cache now holds `TStrongObjectPtr` roots, and checks `IsValid` on the way
out so a stale entry is reloaded and logged rather than returned.

**A static cache of raw UObject pointers is a dangling pointer waiting for a
collection.** It survived every run that only ever built one world.

## The town built in the dark

With the crash gone, three of the six frames came back black or blown out, and
each was argued about as a streaming problem, an aim problem and an exposure
problem before anyone asked whether the ground being photographed was in
daylight. One log line settled it:

```
site: body 3, sun.site 0.946, solar altitude -80.0 deg
```

Those two numbers describe the same place and cannot both be right. `SunFacing`
is the star's direction **in the active body's rotating frame**, so it means
something different the moment the active body changes — and the switch was not
recomputing it. The moon's landing site was being chosen against the *planet's*
sun vector: the picker found somewhere with the sun nearly overhead by that
measure, and the sky, asked properly, put it eighty degrees below the horizon.
The town was built in the dark, and the light was correct all along.

One line after `SetActiveBody`, and the two agree: `0.946` and `71.1 deg`.

Three smaller things fell out of the same investigation:

- **The stand-off was measured in the wrong body's radii.** The arrival is taken
  after the switch, above a moon a third the size, so forty home-radii put the
  camera four times too far out — a photograph of the right thing showing
  nothing. Eight radii of whichever body is underneath.
- **Fractions past 1.0 are altitude, not more travelling.** The clock was
  multiplying straight through, so the landing happened five and a half hours
  after the arrival — most of a night on a small fast moon.
- **On an airless body the meter has to follow the highlights.** With no sky
  light a shadow receives nothing, the frame is half sunlit ground and half pure
  black, and metering the middle of that histogram exposes for the black. The
  post-process now moves the metering window to the 70th–96th percentile when
  the body has no air, which is the same thing sunny-sixteen does and for the
  same reason.

## Four defects found on the way, none of them the cause

Each of these is real, each is fixed, and each was found because it was in the
way of something else.

**A failed asset load, retried every frame.** T093's wind subsystem looked for a
material parameter collection that a clone without the content pack does not
have. `LoadObject` does not cache a failure, so it was a synchronous load and an
async-loading flush *every tick* — 147 ms stalls and a log line per frame, in
every run since T093 landed. It looks for it once now.

**A stale planet pointer.** The ship cached the planet and re-fetched it only
when the pointer was null. A `UPROPERTY` to a destroyed actor is not cleared
until the collector runs, so between a switch and the next collection the ship
was steering by a planet that had already emptied its job table. `IsValid`, not
`!= nullptr`.

**A stale sky.** `ULedgerSkyBodies` built its spheres once and kept them. A
moon's sky is not a planet's — different bodies, different sizes — so a crossing
has to rebuild the set rather than point old spheres at new indices.

**A planet that woke before it was told anything.** `SpawnActor` dispatches
`BeginPlay` *immediately* once the world is running, and defers it only during
the world's own begin play. The first build therefore assigned the surface
material before the planet woke, and a rebuild did not: the planet found no
material, fell back to `VertexColorViewMode_ColorOnly` — an engine debug
material for visualising vertex colours — and painted a procedural planet with
it. It is spawned deferred now, with `FinishSpawning` after the last thing that
has to be true first.

## What a full pool costs

The terrain stats now end with a line saying what the process is holding, next
to the counters that explain it:

```
sections           3600 active, 0 in flight, 0 free of 3600
memory             10806 MB used, 10806 MB peak, 15135 MB virtual, 5285 MB available
```

Ten point eight gigabytes at a saturated pool, against 2.5 at startup: about
2.3 MB per patch, which is a 65×65 mesh plus cooked collision plus a
ray-tracing acceleration structure — Lumen's hardware path is on, so every
section builds one. It is a cost, not a leak: it tracks `sections active`
exactly and comes back down when the view pulls away.

It is worth knowing because running out of **commit** does not arrive as a
diagnosis. It arrives as an access violation in whichever allocation happened to
be next, which on one occasion was `SetProcMeshSection` inside `UploadFromCache`
and looked like a terrain bug. Two of these processes at once on a machine with
a small page file is enough. A warning now fires ten seconds before the bang,
when headroom drops under two gigabytes.

## Still open

The moon photographed from eight radii shows its quadtree: patch-sized tonal
blocks where neighbouring palettes meet, and a scattering of black holes and
white triangles at the coarsest level. That is a level-of-detail defect at
planet scale, visible only from orbit, and it belongs to the terrain rather than
to the crossing.

## The frames

| | |
|---|---|
| `crossing-0-watch.png` | the moon from the home ground, before anything moves |
| `crossing-1-departure.png` | the home world from low altitude |
| `crossing-2-cruise.png` | half-lit home planet against stars, mid-crossing |
| `crossing-3-arrival.png` | the destination, lit, from eight of its own radii |
| `crossing-4-descent.png` | its surface from two hundred kilometres |
| `crossing-5-landed.png` | standing on it: black sky, stars, regolith, ice peaks |
