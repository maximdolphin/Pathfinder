# Biomes as data (T052)

A biome is a soft region of climate space with a surface set, a tint, a scatter
density and a slope limit. The mechanism is `LedgerBiome.h`; the biomes are
`client/Config/Biomes/*.json`, one file each.

## The acceptance, run rather than asserted

> Adding a biome is one data file and requires no recompile.

`Ledger.Biome.AddingABiomeIsOneFileAndNoRecompile` builds a directory with two
biomes, asks which one owns hot wet ground, writes a third file into the same
directory, and asks again. The answer changes. Nothing between the two questions
but a file appearing.

Six tests, all green:

```
Ledger.Biome.ShippedSetLoadsWithoutError
Ledger.Biome.WeightsSumToOneEverywhere
Ledger.Biome.ClimateCornersPickTheObviousBiome
Ledger.Biome.SteepGroundLeavesTheBiomesThatRefuseIt
Ledger.Biome.AddingABiomeIsOneFileAndNoRecompile
Ledger.Biome.MalformedFilesAreReportedAndSkipped
```

## Two decisions worth the words

**Weights, not a classification.** `Weigh` returns a weight per biome, Gaussian
in climate distance, normalised. A function returning *the* biome at a point
draws a line on the ground wherever two of them meet, and T053's acceptance is
specifically that three biomes can meet on one slope with no visible blend band.
A Gaussian has no support boundary, so there is no edge to see. The census below
says 33.9% of the land is inside a transition — that is the fraction T053 has to
blend, measured rather than guessed.

**Config, not Content.** Loose non-asset files under `Content/` are deleted by
the cook. That is not a guess either; it is the bug that kept the pipeline cache
from ever shipping (`docs/comparisons/packaged/pso/README.md`). The config
directory stages intact.

Rejected files are named with the field that is wrong, and one bad file does not
take the set down with it. A biome missing `temperatureC` would otherwise sit at
the origin of climate space, which is the middle, which is where it would win —
the failure would look like a bug in the climate model.

## What the framework says about this planet

The `-climate` fixture now runs the census over the land surface, weighted by
`cos(latitude)`:

```
biomes over 19426 land points, 8 loaded:
  Ice cap                 22.9% of land by area
  Tundra                  14.4% of land by area
  Boreal forest            1.2% of land by area
  Temperate forest         1.5% of land by area
  Temperate grassland      5.7% of land by area
  Savanna                  3.7% of land by area
  Desert                  48.1% of land by area
  Tropical rainforest      2.4% of land by area
  in a transition (no biome above 75% weight): 33.9%
```

**Half the planet is desert and four per cent is forest.** That is not a biome
bug and it has not been tuned away: it is what the moisture field currently
produces, and the moisture field is limited by the same missing relief that
blocks T051's rain shadow (`docs/comparisons/climate/README.md`). Widening the
forest biomes until the numbers look like Earth would hide that behind a
distribution that no longer follows from the climate, which is the one thing
reading biomes from climate was supposed to prevent.

The first version of this census was unweighted and reported 45% ice cap. A
one-degree cell at 80° covers a sixth of the ground a one-degree cell at the
equator does, so that was a statement about the sampling grid. Kept here because
the corrected number, 22.9%, is not obviously wrong on its own.

Raw report: `climate-with-biomes.txt`.
