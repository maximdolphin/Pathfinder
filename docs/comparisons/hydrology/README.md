# River networks by flow accumulation (T055)

Rivers derived from the height field rather than drawn on it: a fixed lattice on
the cube-sphere, one height sample per cell, steepest descent to a neighbour,
and flow accumulated in descending height order.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -hydrology`. It builds the
whole planet's drainage in about a tenth of a second and writes
`out/hydrology.txt`.

## Why a lattice and not the quadtree

The quadtree exists to put triangles where the camera is. Drainage is a global
question: which way a river runs at a point depends on ground a thousand
kilometres away, and two neighbouring patches at different LODs would answer it
differently. So the flow field is built once for the whole planet at one
resolution and the terrain reads from it.

256 cells per cube face is 39 km apart and 393,216 cells. That is a continental
drainage network rather than a stream you can stand in, which is the right first
answer: the acceptance is about whether the network is *correct*, and
correctness does not depend on how fine the lattice is.

## The acceptance

> Every river reaches the sea or a basin, none flows uphill, and the same seed
> produces the same network.

All three are properties of the field rather than of a picture, so all three are
tests rather than a screenshot. Five, all green:

```
Ledger.Hydrology.NoCellFlowsUphill
Ledger.Hydrology.EveryCellReachesTheSeaOrABasin
Ledger.Hydrology.SameSeedSameNetwork
Ledger.Hydrology.FlowIsConserved
Ledger.Hydrology.CellRoundTripsThroughItsCentre
```

`FlowIsConserved` is not in the acceptance and is the one that would catch a
wrong accumulation: every cell contributes itself and passes on what it
received, so the flow arriving at the terminals is exactly the cell count. An
accumulation pass that double-counted would still produce a plausible-looking
network with every river the wrong size, and nothing else would notice.

`SameSeedSameNetwork` builds twice and compares a hash of the downstream links.
The build runs on the task graph, so it is also the test for a race in the
neighbour pass — there is nowhere else one would show up.

## The bug that every other test passed

The first version found a cell's neighbour by offsetting u and v past the face
edge, projecting to the sphere, and asking `DirectionToFace` where it landed.

`DirectionToFace` inverts `FaceToCube`. It does **not** invert `CubeToSphere`,
which applies an area-evening warp on top of the projection:

```cpp
OnCube.X * FMath::Sqrt(1.0 - (Y2 + Z2) * 0.5 + (Y2 * Z2) / 3.0)
```

So `DirectionToFace(CubeToSphere(FaceToCube(f, u, v)))` is a different cell — by
several, near a face edge. Every "neighbour" in that field was some other cell
somewhere else on the face.

**Every test passed.** The flow graph was still perfectly consistent: each link
still went strictly downhill, nothing cycled, flow was conserved, and two builds
agreed. What it was not was a *map*. The tell was in the report, not the tests —
32,605 closed basins, 32,227 river mouths, and a longest river of 0 km, because
no cell's feeders were among its neighbours.

Neighbours are now resolved in cube space, where `DirectionToFace` is an exact
inverse and the face seam still needs no special case. `CellAt`, which has to go
the other way, inverts the warp with a damped fixed point — there is no closed
form, and twelve iterations converge to well under a cell.

`CellRoundTripsThroughItsCentre` exists because of this: a cell must round-trip
through its own centre, and each of its eight neighbours must be within two cell
widths of it on the sphere.

## What the planet drains like

```
lattice 256 per cube face, 393216 cells, 39.1 km apart, built in 0.1 s
land 113920 cells (29.0%), sea 279296
of the land, 46.6% drains to the sea and 53.4% into 1985 closed basins

11603 river mouths. The twelve largest:
      lat     lon    basin km2   cells   length km
   -37.65  129.95      394271     258         821
   -27.31   52.09      362179     237        1173
    32.23 -113.43      334672     219         977
   -10.97  139.87      320918     210        1016
```

The largest catchment is 394,000 km² and the longest river 1,173 km — the Rhine
and the Danube rather than the Amazon, which is about right for a lattice at 39
km on a planet whose highest land is 1,372 m.

**Fifty-three per cent of the land is endorheic.** Earth is about eighteen. That
is not a defect in the accumulation; it is what a height field with no erosion
looks like — every local dimple is a closed basin, and there are 1,985 of them.
Pit-filling (a priority flood, so rivers cross depressions and go on to the sea)
would bring it down, and is not done here because the acceptance explicitly
allows a basin as a terminus and because the same missing relief that blocks
T051 and T054 is what makes the dimples the dominant landform.

Raw report: `hydrology.txt`.
