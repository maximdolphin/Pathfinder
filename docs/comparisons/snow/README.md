# Snow: the overlay is right, the field is not

T060. The blend that puts snow on screen exists, is correct by construction, and
is **off by default** because what feeds it is wrong.

Run it: `-snow` turns the overlay on. `-channel=snow` photographs the weight it
is driven by.

## The first attempt, and why this one is shaped differently

The blend was written once before and reverted the same evening. It
height-blended a snow set into the finished ground, and a height blend competes
whether or not it is wanted: it changed every terrain capture on the planet,
including at season zero where the climate is identical and nothing should have
moved. Shipping it would have meant every measurement in this repository being
taken against a look nobody chose.

So this one is a lerp whose alpha is multiplied by cover:

```
Settles    = saturate((1 + (1 - GroundHeight) * 1.5) * (1 - RockWeight * 0.7))
SnowWeight = saturate(Cover * Settles)
Albedo     = lerp(Albedo, SnowAlbedo, SnowWeight)
```

At cover zero the alpha is exactly zero, and a lerp at zero is the identity.
Not approximately — the arithmetic cannot do anything else. Everything clever
about where snow settles lives inside that multiplication, where it is harmless:
hollows fill first because the ground's own height map drives it, and a face
steep enough to be rock sheds most of what lands on it.

## And it still changed everything, which is the finding

Four biomes, with and without:

```
ice cap             61.4 / 255 mean difference
tropical rainforest 52.1
desert              40.7
savanna             36.4
```

A desert changing by forty levels is not a blend competing. **The cover really
is that high.** `-channel=snow` over grassland at season zero reads a mean of
131 of 255, and `four-biomes-3-desert` comes back grey and half-snowed.

That reframes the original revert, too. Its reasoning was "at season zero the
climate is identical and nothing should have moved" — which assumed cover is
zero at season zero. Season zero is a season, not an absence of winter, and
cover at these sites is not zero. The first blend may have been less wrong than
it was recorded as being; it was never separated from the field it was reading.

## What has to happen next, in order

1. ~~**Check the channel end to end.**~~ **Done, and it was not the cause.** The
   fallback path returned alpha 255 through `Blend` — full snow — which is
   plainly wrong and is fixed to zero: with no biomes there is no climate to
   ask, and unknown cover is better rendered as none than as a metre of it.
   Nothing read the channel until the overlay existed, which is how a wrong
   constant sat there for two milestones.

   It changed the measurement by one tenth of one level: 131.0 to 131.1. The
   biome path is running at this site and the alpha it writes really does say
   half cover.
2. **Check whether T051 raised it.** Now the only candidate left. The drying rate over land went 0.97 to 0.99
   per step in the same session, which moved desert from 50.6% to 32.9% of land.
   More moisture where it is cold is more snow, and nobody has looked at what
   that did to cover.
3. **The shaping was made patchy and it did not help**, which narrows the
   question rather than answering it. Cover now raises a level and the ground's
   height map decides what is under it —
   `saturate((Cover - Height) * 4 + Cover)`, still exactly zero at zero cover —
   so half cover should dapple rather than mix. The mean difference against no
   snow is unchanged (40.63 against 40.68 on the desert, which is what a
   variance change looks like through a mean) and the desert still reads
   uniformly grey.

   The likely reason is that the overlay also fades the biome tint and the
   scan's own mean with `SnowWeight`, so partial cover desaturates the whole
   surface instead of dappling it. That is the next thing to try, and it is a
   different fix from the blend.

The overlay stays behind `-snow` until those are answered. The work, the control
arm and the measurement are all kept; what is not kept is a snowed desert in
front of anybody.

## Files

| | |
|---|---|
| `client/Source/LedgerMaterial/Private/LedgerTerrainMaterial.cpp` | the overlay, `-snow`, and `-channel=snow` |
| `client/Source/LedgerTerrain/Private/LedgerPatchGenerator.cpp` | where alpha is written, on both paths |
