# A gas giant: the model, and what is still missing

T079. The acceptance: *descend into a gas giant — bands resolve, pressure rises,
and the ship is destroyed at the depth the atmosphere model predicts.*

**Half of this is delivered and the other half is not.** The physics and the
banding are built and measured; the descent as a thing you fly is not, and the
reason is at the end.

## Somewhere with no floor

`LedgerBodies::Generate` now makes a fourth body — a gas giant beyond the frost
line, at 4.5 to 7 astronomical units, because that is where they can form: past
the distance at which water is ice rather than vapour, so there is enough solid
material to build a core big enough to hold hydrogen. Putting one closer would
be generating a planet that could not have got there.

A gas giant has no surface, so "altitude" needs a datum that is not one. The
convention is the **one-bar level**, and a giant's quoted radius is that level —
which means `RadiusMetres` keeps meaning the same thing whether or not there is
anything solid underneath.

## Pressure

```
gas giant: 80715 km radius, 10.83 m/s^2, 104 K, scale height 34.7 km

predicted crush depth 135.8 km; the descent failed at 135.8 km
```

Two computations. The prediction is a logarithm — `H·ln(P/P₀)` — evaluated
before anything moves. The descent is a loop that steps down a metre at a time
asking the pressure and stopping when the hull gives. They agree **to under a
metre**.

The pressure law itself is checked against the differential equation it solves.
`Ledger.Gas.PressureMatchesTheIntegratedHydrostaticEquation` steps `dP = ρg dh`
downwards in five-metre slices with ρ recomputed from the pressure at each
slice, over 150 km, and compares with the closed form at every step. Worst
disagreement **3.1e-4**, which is the first-order integrator's own error and not
the model's.

### Against Jupiter

```
Jupiter: gravity 24.78 m/s^2 (measured 24.79), scale height 24.1 km (measured about 27)
```

Two things worth recording from that line.

**The equatorial radius, and it is not a detail.** Jupiter's quoted surface
gravity of 24.79 is GM over its *equatorial* radius of 71,492 km. Asked with the
volumetric mean radius of 69,911 km — which is what the generator uses, and
reasonably — it gives 25.92, and this test failed until it was asked with the
radius the number was quoted at. Two right answers to two different questions.

**The scale height is 11% low and the reason is known.** This model carries one
mean molecular mass of 2.3 for every giant; Jupiter's is nearer 2.22, and its
temperature is not constant with depth the way an isothermal model assumes. Both
push the same way. That is close enough for a body a ship descends into and not
close enough to call the model finished, so the bound is stated with the reason
rather than opened until it passes quietly.

The model errs *shallow* — it predicts the hull failing sooner than a real
atmosphere would — which is the safe direction for a thing that kills the ship.

## Bands

`out/t079-giant-bands.png` is the field, drawn. Alternating belts and zones with
boundaries that wobble, and pale storm ovals sitting inside them.

**The bands are a field, not a texture** — a function of latitude, longitude and
time that anything can sample. That is what lets them be checked rather than
admired:

```
storms cover 3.64% of the surface
  latitude +0.37 rad: wind -0.532, storm moved -1.4486 rad
```

- The jets alternate: sign changes in the zonal wind from pole to pole, with the
  poles quiet, because a jet running over the pole would have to run round a
  point.
- The field is banded: **more than twice the variance across the bands as along
  one**. That is the difference between a gas giant and a marble, and it is the
  first thing that would break if the turbulence were sampled without the flow.
- Storms drift the way their own band blows.

### The bug the drift test found

Storms were placed **exactly on the jet boundaries**, on the reasoning that a
shear line is where a storm lives. A jet boundary is where the wind reverses —
which is to say where it is zero. All nine storms sat in perfectly still air for
ever, and the drift test could not find a single one moving.

They now sit a third of a jet's width off the boundary, embedded in one of the
two jets and carried by it. Which is also why they are lenses rather than
circles: the wind shears anything round into one within a few rotations.

## What is not delivered

**You cannot fly into it.** The giant is five astronomical units away, the world
builds one body's terrain at a time, and a body with no surface needs a
volumetric renderer rather than a cube-sphere — the terrain framework that T078
showed is body-agnostic is agnostic about *surfaces*, and this has none.

So the acceptance's "descend into" is unmet. What exists is the model it would
descend through, measured, and the bands it would descend past, drawn. The
remaining work is a volumetric interior, which belongs with T226's volumetrics
and needs the atmosphere calibration that four tasks are now waiting on.

Recorded as blocked rather than done, and the same way T430 was: the half that
is finished is committed and measured, and the half that is not is named.
