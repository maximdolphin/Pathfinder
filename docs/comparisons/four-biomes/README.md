# Stand still in four places and look at the ground

T437, the proof M2S is held to. **It fails**, and it found two things every
numeric fixture in this milestone passed over.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -fourbiomes`.

## What it does

Searches the lit, dry-land surface of the whole planet for the point where each
biome's weight is highest — the best example of each rather than the first, so
four sites do not all land on the same boundary — and photographs the four
strongest at eye height with identical framing.

It found:

```
Ice cap              weight 1.00
Desert               weight 1.00
Tropical rainforest  weight 1.00
Savanna              weight 0.99
```

## The half that can be measured

Four photographs of the same desert would satisfy a careless reading of the
acceptance, so the fixture checks the four are different places. Mean ground
colour over the bottom half of each frame, and the distance between each pair:

```
ice-cap       (148.3, 145.0, 134.1)
desert        (134.6, 118.4,  86.4)
rainforest    ( 92.9, 117.2,  86.7)
savanna       (121.4, 106.8,  65.8)

closest pair: desert vs savanna, 27.0 of 255
```

Four measurably distinct grounds. That half passes.

## The half that matters, and does not pass

The acceptance is *a person shown it without context does not identify it as a
video game heightfield*. Nobody would pass `four-biomes-3-tropical-rainforest.png`.

Two things, both of which the numbers missed:

**The stones are on a visible lattice.** The scatter places one instance per
cell of a 20×20 grid per patch, and in the mid-distance of the rainforest shot
that grid is plain: rows of stones marching in step across the hillside. At the
grassland site where T434 was signed off, the density was low enough that it
never showed. One capture in one biome is not a test of a placement rule.

**The stones are white.** The vertex colour is a light warm grey and the flat
material tints it with white, so under a bright sun in a bright biome they blow
out to featureless white blobs — where the same stones at the grassland site
read as pale rock. They also take no colour from the ground they sit on, which
is why a rainforest has arctic-looking boulders in it.

Neither is a rendering bug. Both are T434 signed off on the strength of one
photograph of one biome, which is exactly the failure this fixture exists to
catch, and T434 is reopened.

## Why this is the right kind of failure

Every other fixture in M2S measures something: pixels per quad, RMS from a
fitted line, autocorrelation at the tiling period, mean absolute difference
against a control arm. The surface passes all of them. It passes them and still
looks like this, which is the whole argument for keeping one task in the
milestone whose verdict is a person's.

## Files

| | |
|---|---|
| `client/Source/LedgerHarness/Private/LedgerFourBiomes.cpp` | the search, the framing, the difference check |
| `out/four-biomes-{1..4}-*.png` | the four |
