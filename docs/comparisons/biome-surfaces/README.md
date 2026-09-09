# Biome-driven surface composition (T053)

The climate field picks the ground. Three surface scans per patch, weighted by
biome, height-blended, with macro variation at ninety metres and at 1.2 km.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -biomesite`. It searches
the planet for a place worth photographing, writes `out/biome-site.txt`, takes
six pictures and exits.

## What it does

The patch generator samples climate on a 3×3 grid across each patch — one
`LedgerClimate::At` costs forty height samples, so per vertex it would be a
fortyfold increase in the cost this project has spent the most effort measuring
— interpolates, recomputes temperature from each vertex's own altitude, and
weighs the biomes with that vertex's own slope. The three weights go into the
vertex colour. A dynamic instance per palette binds the three scans, their
tiling distances, their measured mean albedos and the biome tints.

The three grounds are combined by height, all three at once rather than as
nested lerps: every slot bids its own height plus its own weight and the
proudest texel within `BlendDepth` wins, which is symmetric in the three.

## The acceptance, part by part

> Three biomes meet on one slope with no visible blend band, no repetition at
> any distance, and the boundary explained by the climate field.

**Three biomes meet — yes, and the report says which.** The fixture searches for
the point where the *third* weight is largest (maximising the top two finds a
two-way boundary, of which there are thousands):

```
site: 27.50 lat, -82.00 lon
the three heaviest here: 0.381, 0.310, 0.309
```

**The boundary explained by the climate field — yes.** The report prints the
temperature, the moisture and every biome's weight at the site and across it,
plus the dominant biome every 500 m along the line the camera is looking down.

**No repetition at any distance — fixed, and it was real.** The first captures
showed a hard grid over the whole middle distance. The ninety-metre macro
sample was not distance-faded, so past its mip range it was a few pixels per
tile and contributed a moiré rather than variation. It now fades with the
detail it modulates; the 1.2 km one does not, because at ten kilometres its
features are still hundreds of pixels across, which is what it is for.

**No visible blend band — no. This is the part that does not pass.**

## The seams, and three control arms

The first captures had rectangular seams across the ground. Rectangular is a
strong hint, but this project has been wrong four times by acting on a strong
hint, so it was measured instead. All three arms are command lines:

| arm | flag | seams |
|---|---|---|
| one palette for the whole planet | `-onepalette` | none |
| a palette per patch | `-patchpalette` | yes, one per patch edge |
| a palette per cell of a cube face | *(default)* | none inside a cell |

`edge-one-palette-control.png` is the control that made this a finding rather
than a guess: with one palette everywhere the seams are gone, so the palette is
the cause. `edge-patch-palette.png` is the failure, kept.

**Why a per-patch palette seams.** The mesh carries three weights, so a patch
keeps its own three heaviest biomes and drops the rest. Two neighbours whose
totals rank differently drop *different* biomes, and along their shared edge the
ground changes composition in a straight line.

Truncating the weight field to three continuously — subtracting the
fourth-largest weight and clamping, so at most three are ever non-zero and a
biome entering or leaving the top three fades rather than appears — is right
and is kept, but it did **not** move the seams. That is worth recording: the
problem is not which weights get dropped, it is that the two patches drop
different biomes.

The palette is now chosen for a fixed cell of the cube face, 1/16 of a face and
about 600 km, from climate sampled at that cell's corners. Every patch inside a
cell agrees, so there are no seams inside one. The whole planet uses twelve
palettes.

**It does not remove the problem, it relocates it.** `edge-cell-palette.png` has
no patch seams and one hard line across the foreground: a cell boundary. There
are far fewer of them and they are 600 km apart, but standing on one it is a
wall.

## How many biomes a cell actually needs

The number the decision turns on, counted over every cell with land in it:

```
biomes present in one palette cell (~600 km), at 5% of the cell or more:
  1 biome     213 cells   30.4%
  2 biomes    241 cells   34.4%
  3 biomes    169 cells   24.1%
  4 biomes     54 cells    7.7%
  5 biomes     22 cells    3.1%
  6 biomes      1 cells    0.1%
  cells needing more than the three slots the mesh can carry: 11.0%
```

Three slots are enough for **89%** of the land. A fourth — vertex colour has an
unused alpha channel — would take it to **96.9%**. Neither reaches 100%, and
the only thing that does is dropping palettes entirely: a texture array indexed
per vertex, so a vertex names any three of the whole biome set rather than three
of its cell's. That is a change to how the terrain material is built and to what
the bake produces, so it is written down here rather than done.

## What it costs

The material went from two scans to four — three parameterised ground slots plus
rock — and from one macro sample to two: 21 texture samples to 42. Measured the
same way T047 measured the old one, `-flatterrain` against the real material
over the same fixed-step flight, GPU milliseconds:

| phase | real | flat | the material |
|---|---|---|---|
| orbit | 2.6 | 2.5 | 0.1 |
| descent | 4.2 | 3.8 | 0.4 |
| atmospheric entry | 5.9 | 4.6 | 1.3 |
| **surface** | 7.0 | 7.2 | **−0.2** |
| **town** | 7.3 | 7.4 | **−0.1** |
| ridge sweep | 6.0 | 6.5 | −0.5 |
| coast | 7.9 | 7.1 | 0.8 |
| underwater | 8.6 | 8.6 | 0.0 |
| ascent | 3.0 | 3.0 | 0.0 |
| space | 6.0 | 4.9 | 1.1 |

**Doubling the samples cost at most 1.3 ms, and nothing measurable in the two
phases where terrain fills the screen** — the differences there are negative,
which is run-to-run noise rather than a saving. T047's conclusion survives its
own premise changing: there is still no material cost for a virtual texture to
recover.

The game-thread terrain cost moved from 6.08 ms to 6.69 ms in the ridge sweep,
about ten per cent, which is the nine climate samples per patch.

## Two bugs worth keeping

**The tints were colours, not albedos.** Desert at 0.78 where dry sand is about
0.35, and only fresh snow gets near 0.8. Multiplied by mean-normalised detail
and two macro terms it clipped to white, and the first edge captures are a
photograph of an overexposed desert that is not overexposed. The tints are now
published reflectances and the material saturates the product as a backstop —
clamping rather than rescaling, so if it ever does real work the data is wrong
and should be fixed there.

**The "across the boundary" direction was measured 500 km out.**
`500000 cm / (Radius_cm / 100)` is an arc only if the numerator is metres, and
it was centimetres. The fixture reported a rainforest "5 km across" that was
five hundred kilometres away, and the photographs — correctly — showed eight
kilometres of unchanging ground. The line-of-sight table in the report exists
because of this: a picture of one uniform ground cannot distinguish a broken
blend from a boundary that was never in frame.

Raw reports: `biome-site.txt`, `t053-real.txt`, `t053-flat.txt`.
