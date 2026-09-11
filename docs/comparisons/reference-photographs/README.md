# The render against photographs of real ground (T435)

Twelve pairs: four grounds -- desert gravel, scree, forest floor, snow -- at
2 m, 20 m and 200 m. On the left is the render at the site the four-biome and
cliff fixtures found for that ground (`-surfacestudy -reference -studyat=`),
and on the right a freely licensed photograph of the real thing. Every
photograph is CC0 or public domain, downloaded from Wikimedia Commons, and
listed with its author and source in `surfaces/reference/manifest.json`
(and in `pairs.json` beside these images).

**What the pairs do not match.** Distance and ground type are matched; focal
length, sun angle and exposure are not. The photographs were taken where and
when their photographers were, and the renders at the fixture's sites and
clock. So these compare what the ground *is* -- its materials, its clasts, its
cover -- and not its lighting. That is the comparison that fails most clearly
anyway.

## Verdicts

| Pair | Verdict |
|---|---|
| gravel, 2 m | **Loses.** The photograph is desert pavement: angular stones from one to ten centimetres packed shoulder to shoulder over a pale matrix. The render is smooth tan sand with a faint mottle -- not one clast at the distance a person looks at their feet. The arid gravel scan is either not the surface drawn here or is washed out by the macro blend, and there is no small-stone scatter at all. |
| gravel, 20 m | **Loses.** The photograph is still stones to the horizon of the frame; the render is rolling sand under a near-black sky. Two deficiencies: no stone at any scale, and a sky that is exposed as if it were night. |
| gravel, 200 m | **Loses.** Boa Vista's hamada is boulders and cobbles strewn to the horizon over red soil; the render is flat sand. The boulder scatter that exists elsewhere is absent at this site, and the sky is again dark. |
| scree, 2 m | **Loses.** A talus is blocks -- angular, ten to forty centimetres, resting on each other with shadowed gaps between. The render is sand with pale marks. Scree cannot be a texture: it needs a field of block geometry, which the terrain does not have. |
| scree, 20 m | **Loses.** The photograph is a scree slope of grey clasts with dwarf pine on its margins; the render is a sand slope. Same deficiency as at 2 m, and no vegetation. |
| scree, 200 m | **Loses.** A talus apron under grey cliffs, the cliffs shedding it; the render is a sand slope under a dark sky. The site does not read as scree at all: there is no rock outcrop above it to have made it. |
| forest, 2 m | **Loses.** Leaf litter, twigs, a fallen branch and low plants; the render is a pale green-and-white mottle that reads as marble. The forest-floor material is washed out and there is no litter or understorey. |
| forest, 20 m | **Loses.** Trunks and a floor of fallen leaves; the render is green marbled hills with no tree anywhere. The rainforest site has no vegetation at this range. |
| forest, 200 m | **Loses.** A continuous canopy; the render is bare green hills with dark boulders of a scale no forest shows. No canopy. |
| snow, 2 m | **Loses.** Granular snow with visible grain and soft micro-relief; the render is smooth white-grey with streaks, the streaks being the scan's own texture stretched across the slope. |
| snow, 20 m | **Closest.** The render's wind-shaped drifts read like the sastrugi in the photograph -- the one pair where the forms agree. The sky is still wrong, dark where the photograph's is bright overcast. |
| snow, 200 m | **Loses, and the pairing is loose.** The photograph is a snow patch in an alpine meadow; the render is an unbroken snowfield of rolling drifts. Nothing breaks it -- no rock outcrop, no transition to bare ground -- and the sky is dark. |

## What this says to fix, in order of how much it costs the picture

1. **No clasts where the ground is stone.** Gravel and scree render as sand at
   every distance. Needs a dense small-stone scatter at close range, block
   geometry on talus, and the right scan actually winning at those sites.
2. **Dark skies at the reference sites.** Every 20 m and 200 m frame has a sky
   near black or brown against bright photographs. An exposure or sky-light
   fault at these sites and clocks, not a material one.
3. **No vegetation in the forest.** Trees, understorey and litter are all
   absent at the rainforest site; the ground material there is washed out.
4. **Snow without grain.** The closest material, but smooth at 2 m.

Eleven of twelve lose, and the one that holds holds on shape alone. That is
the honest state of close-range fidelity on the day these were made.

Photographs: see `pairs.json` for each file's title, licence and source page.
