# The 200 km transect, and why it fails

T068 is M02's gate: *200 km at 300 m and 900 m/s: no holes, no seams, no frame
over 16 ms, collision present throughout.* The collision half fails badly and
this is what is known about it.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -transect`.

## The measurement

```
distance      200 km
speed         900 m/s
altitude      50 m
nodes visible 2695, of which with collision 1476
worst cook    5.02 ms
traces missed 20454 of 20632  (99.1%)
```

A downward trace from the ship finds nothing beneath it on almost every frame.

**It is not a regression from anything done on 2026-09-09/10.** Run with
`-breakthreshold`, which restores the pre-T429 error threshold, it fails
identically. The ground is drawn correctly throughout; it is collision that is
absent.

## What is ruled out

| | |
|---|---|
| collision never being requested | 1,476 of 2,695 visible nodes want it |
| collision never cooking | worst cook 5.02 ms, so cooking happens |
| the trace being too short | 400 m of trace against 50 m of clearance |
| the trace channel | an explicit `BlockAll` profile and `ECC_WorldStatic` object type were added and it did not move |

## What is actually wrong

Read out of the code rather than measured, which matters given the next section.

`ALedgerPlanet` skips any leaf whose section is already active:

```cpp
if (ActiveSections.Contains(Key) || InFlight.Contains(Key)) { continue; }
```

So **a patch built without collision can never gain it.** Collision is decided
at build time from whether the leaf is within `CollisionRadius` (3 km), and at
900 m/s the geometry lead builds ground several seconds ahead — well outside
3 km. Nearly every patch the ship passes over was built while it was still far
away, with collision correctly declined, and is then never reconsidered.

That explains the shape of the failure exactly: collision is cooked in
quantity, on the wrong patches.

## The obvious fix makes it worse

Re-requesting active patches that want collision and lack it took the miss rate
from 89% to 99.8%. Every frame re-asks for more than the streaming budget can
serve, so sections churn instead of settling and the ground under the ship is
rebuilt rather than kept. Reverted.

The two candidates that remain:

- **Cook collision for what the lead builds**, paying for it on ground that may
  never be flown over. Simple, and it moves cost from a place that needs it to a
  place that might not.
- **Upgrade a section in place**, without a rebuild. Correct, and the procedural
  mesh path cannot do it today: collision geometry is created or not at
  `CreateMeshSection`, and there is no way to add it afterwards without
  re-uploading the section.

## A warning about the number

The same build, unchanged, reported **93.7%, 88.9%, 99.8% and 99.1%** across
four consecutive runs. Nothing about the fixture is deterministic: streaming
order, cook timing and frame pacing all vary, and the run length varies with
them (17,000 to 37,000 frames for the same 200 km).

So none of the four changes tried tonight can be credited or blamed on the
strength of a single run each — the explicit collision profile is kept because
it is correct on its own terms, not because the number moved. **The structural
finding above is the only solid thing here**, and it was read out of the code.

Before this is worked on, the fixture needs to be made repeatable — a fixed time
step and a fixed frame budget — or every future change to it will be argued from
noise. That is the same conclusion `docs/comparisons/m2s-cost/` reached about
p99 on the scripted flight, from the same cause.

## Files

| | |
|---|---|
| `client/Source/LedgerHarness/Private/LedgerTransect.cpp` | the run, the trace, the report |
| `client/Source/LedgerTerrain/Private/LedgerPlanet.cpp` | the skip, and the note explaining it |
| `client/Source/LedgerTerrain/Private/LedgerPatchComponents.cpp` | the collision profile |
