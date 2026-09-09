# Snow, ice and seasonal cover (T060) — the field, not yet the look

## The acceptance, as properties of the climate function

> The same location has snow in winter and not in summer, and the snow line
> moves with altitude.

Both halves are things `LedgerClimate` either does or does not do, so both are
tests rather than two screenshots taken six months apart. Four, all green:

```
Ledger.Snow.SameLocationHasSnowInWinterAndNotInSummer
Ledger.Snow.TheSnowLineMovesWithAltitudeAndLatitude
Ledger.Snow.SeasonsAreOppositeInTheTwoHemispheres
Ledger.Snow.ColdAndDryIsBareRock
```

```
55 N at 800 m: 0.00 in summer, 1.00 in winter
45 N: 0.00 at sea level, 1.00 at 4,000 m
```

Fifty-five north at eight hundred metres is chosen because the answer is not
obvious from the latitude: sea level there is about 6 °C on the annual mean, the
seasonal swing is ±16, and the lapse rate takes another five. It sits either
side of freezing across a year, which is the case the acceptance is about.

**The snow line is not a parameter anywhere.** It is where the surface
temperature crosses zero, and it moves with altitude because the lapse rate was
already in the climate model — snow is the first thing to read it. The test
walks the line from the equator to eighty degrees and fails if it ever rises
going poleward.

The seasonal term is odd in the sine of latitude, so the hemispheres are
opposite by construction rather than by a rule somebody has to remember, and the
equator has no seasons at all. A term that was even in latitude would give both
poles winter at once, which is wrong everywhere and obvious from orbit.

Snow needs moisture as well as cold. The coldest deserts on Earth are bare rock,
and a model that puts a snowfield on everything cold paints all of this planet's
high ground white.

## What is wired up

The season is a constant for a run, set by `-season=` (0 to 1, 0.25 the northern
summer). Not a clock: the snow a patch carries is baked into its vertices, so a
season that moved would invalidate every cached patch continuously. A year that
turns belongs with the weather, in M04.

Snow cover rides the **vertex colour's alpha**, which was the one channel the
biome weights left free. Snow is not a biome — it lies on top of whichever
ground is there — so giving it a palette slot would have cost a biome and made a
snowy forest and a snowy desert the same place. As a fourth channel it is an
overlay, which is what it is. It does not lie on cliffs: the same slope the
biomes are weighed by sheds it.

Scatter reads it, and deep cover buries four fifths of the undergrowth.
`coast-summer.png` and `coast-winter.png` are the same coast at the two
solstices; the difference between them is the trees.

## What is not

**The material does not draw the snow.** A snow surface set height-blended over
the finished ground was written, and it changed every terrain capture on the
planet — including at season zero, where the climate is identical to before it
and nothing should have moved at all. That is a bug in the blend rather than in
the snow, and shipping it would have meant every measurement in this repository
being taken against a look nobody chose. Reverted; the channel and the field
stay, and `LedgerTerrainMaterial.cpp` says where the blend goes.

So the seasonal pictures show the scatter changing and not the ground, and the
acceptance's "has snow" is a number rather than a photograph. That is the honest
state.

## And one thing the control caught

Season zero not reproducing the reference captures was how the material bug was
found — but it also caught something else that had nothing to do with snow. The
scatter's `bWithCollision` gate had been dropped during T059's thinning
experiment and not restored by the revert, which quietly doubled the forest and
put trees across a desert the references had bare. Restored, and the two
conditions are now commented as the different things they are: patch size is
about how big an instance is on screen, and collision is the planet's own
statement about how close the patch is.
