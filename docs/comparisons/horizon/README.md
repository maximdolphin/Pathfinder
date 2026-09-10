# A mountain range eighty kilometres away

T059's acceptance, and the arithmetic that kept it blocked.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -horizon`. It finds the
highest lit land on the planet, stands 80 km away at 600 m, photographs it, and
writes `out/horizon.txt`.

## Why it was blocked, in one line of arithmetic

At 80 km the horizon drop is `R(1 - cos(80/6371))` = **502 m**. On the planet
this task was written against, whose highest land was 1,372 m, the tallest thing
anywhere would have stood 870 m proud of the curve and subtended 10 px. That is
a bump, and no amount of impostor work makes a bump read as a range.

## Now

```
highest lit land              2891 m
horizon drop at 80 km          502 m
standing proud of it          2389 m
subtends                        29 px at 1920 wide, 90 degrees

VERDICT: PASS -- there is a silhouette
```

`horizon-80km.png` is the photograph, and it settles the half a number cannot:
snow-capped peaks sit on the horizon line with a ragged profile, distinct
summits and shadowed flanks. They read as terrain. They are small — 29 px is
small — but the acceptance asks for a silhouette rather than for a spectacle,
and there is one.

## What is not built

The task's **title** says "far-field impostors and horizon detail" and its
detail asks for impostors for scatter past the last real LOD. That is not built,
and the acceptance as written does not ask for it: *"A mountain range 80 km away
has a silhouette and reads as terrain rather than as a gradient."*

The design constraint for that work is already measured and belongs with it: a
candidate count fixed per patch cannot thin with distance without drawing the
patch grid in trees. That was tried — 97,777 instances at 1.5 ms and visibly
worse — and reverted. It needs a candidate lattice fixed to the world, so
density is a function of position rather than of which patch a point fell in.
See `docs/comparisons/scatter/`.

Marked against the acceptance, with the gap named rather than implied.

## Files

| | |
|---|---|
| `client/Source/LedgerHarness/Private/LedgerHorizon.cpp` | the search, the framing, the arithmetic |
| `out/horizon-80km.png` | the photograph |
