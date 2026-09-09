# The terrain sampling API (T061)

One place gameplay asks the ground a question: `ALedgerPlanet::SampleTerrain`
takes a world point and returns the surface radius, the altitude, the normal,
the slope, the climate, the biome mix and the snow lying on it.

## The acceptance

> A thousand random queries agree with a physics trace to the millimetre.

```
1000 queries over a 2 km disc: 996 traced and sampled, 4 hit nothing,
                               0 hit ground the API could not answer about
agreement: 996 of 996 within a millimetre (100.0%)
error: mean 0.0060 mm, worst 0.0225 mm (at 891 m from the ship)
```

Twenty-two microns at worst. Four of the thousand found no terrain to hit at
all, which is the collision ring's edge rather than a disagreement — the API and
the trace both say nothing is there.

Run it: `-terrainquery`. The fixture parks the ship on the town site, waits
twelve seconds for the collision ring to fill, and traces straight down the
radial a thousand times.

## Why it reads the mesh

**Because the mesh and the height function are not the same surface.** The mesh
is a bilinear interpolation of the field sampled every few metres, and on rough
ground it sits over a metre from the field it interpolates — measured in
`docs/comparisons/scatter/`, where placing scatter at the field's exact value
left trees hovering.

A physics trace hits the mesh. A character stands on the mesh. An API that
answered from the field would disagree with both by that much while looking
authoritative, and everything built on it would inherit the error.

So this walks the quadtree to the node whose geometry is *drawn* — not the
tree's leaf, because a split node keeps its geometry until all four children
have theirs — finds its live section, and interpolates over the same two
triangles the generator emitted and the collision was cooked from. Not over the
quad: a bilinear patch and two triangles are different surfaces, and the
difference is the cell's own curvature.

Where no patch is loaded it returns false. A caller that ignores that will put
something at the centre of the planet, which is why `bValid` is the first field.

## Two things the fixture got wrong first

**It traced into empty sky a thousand times and reported a clean sweep of
nothing.** The scripted flight runs in the same process and teleports the ship
through its phases; twelve seconds in, the ship is mid-descent at altitude and
there is no collision ring anywhere near the ground. The fixture now re-places
the ship on the site every frame, because disabling its flight model is not
enough to stop the harness moving it.

**Then four queries reported the API 29.5 metres wrong.** The ship is parked at
the settlement and a single-hit trace takes whatever is nearest — which for
those four was a rooftop. The buildings are real collision and the trace was
right; the question was wrong. It now takes every hit down the radial and uses
the first one that belongs to the planet.

Both were the fixture measuring something other than what it claimed, and both
would have been easy to write up as a defect in the API.
