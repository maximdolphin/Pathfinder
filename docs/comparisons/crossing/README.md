# Crossing to another body in one session

T088's second clause. **The machinery exists and the last frame is missing.**

Run it: `UnrealEditor.exe client/Ledger.uproject -game -crossing`
(`-crossto=<n>` picks a different destination.)

## What the world had to learn

A world was built once, during `OnWorldBeginPlay`, around one body — its terrain,
its sky and its town are all functions of which body that is. The only way to
stand on a different one was to start a new process with `-body=`, which is not
what "in one session" means.

So the tail of `OnWorldBeginPlay` became `BuildWorldFor(UWorld&)` — 457 lines
moved, nothing changed — and `SwitchToBody(int32)` destroys the planet, the
atmosphere and the settlement, then lets the next tick build the replacement.
**This is the first piece of M07 arriving early**, and it arrives because T088
asks for it.

```
crossing: Home to Companion, 361622 km, quoted 5.48 hours at a gravity;
the far end pulls 1.28 m/s2 and holds no air
crossing 1/6: watch
crossing 2/6: departure
crossing 3/6: cruise
switching from body 1 (Home) to 3 (Companion)
crossing 4/6: arrival
```

The trip is quoted by T087's map before anything moves, the clock is run forward
by T085's accelerator, and the world changes bodies underneath a running game.
Then, about fourteen seconds later, the process takes an access violation on the
render thread.

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

That last one looked exactly like the cause. It was not.

## What has been ruled out

| suspicion | test | result |
|---|---|---|
| the destination is airless | `-crossto=4`, a body with air | same crash |
| teardown and rebuild share a frame | split across two ticks | same crash |
| the view target is destroyed | camera kept alive across the switch | same crash |
| a level-of-detail jump from orbit to ground | graded descent step added | same crash |
| the body itself is broken | `-body=3` at startup | **completely stable** |

The last row is the useful one: the destination world is fine when it is built at
startup and not fine when it is built as a replacement, so what is wrong is
something the first world leaves behind rather than anything about the second.

The crash arrives about fourteen seconds after the switch, on the render thread,
with a breadcrumb no more specific than `SceneRender / ViewFamilies`, and a null
dereference. Whoever picks this up should start there: a render resource
belonging to the first world that outlives it.
