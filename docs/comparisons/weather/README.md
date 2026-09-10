# Weather as a function of time

T092. **The same decision T070 made about the solar system, made again about the
air.** An integrated weather model has to be stepped from the epoch to the moment
you care about, so asking what next Tuesday looks like costs a simulated week and
the answer depends on every frame in between. A weather model that is a function
of time costs the same to ask about next Tuesday as about now, gives the same
answer twice, and cannot drift.

A storm here is not a thing that is updated. It is a birth time, a place, a size
and a lifetime, and where it is at any moment is arithmetic on those.

## A week of one storm

```
Home turns once in 33.6 hours, so its Hadley cell reaches 35.5 degrees
and it has 3 circulation cells a hemisphere

cell 4 over a week: 3338 km travelled, longest hourly step 25.5 km,
2 storms in the slot
the worst equatorward step in a life is 0.00e+00 radians
```

- **Coherent** means no teleporting: a storm riding a westerly covers about 50 km
  in an hour, and the longest step in a week is 25.5.
- **Poleward**, because that is what mid-latitude cyclones do — they ride the
  westerlies and curl towards the pole as they occlude. Within one storm's life
  the latitude never moves back towards the equator, not by a rounding error.
- **The same seed gives the same week to the bit** — compared as `==` on doubles,
  not within a tolerance.
- **And asking out of order changes nothing.** Day six is day six whether or not
  day five was asked for first, which is the property an integrated model cannot
  have and the reason this one is built the way it is.

## How fast a body turns decides how many wind bands it has

Held and Hou: the Hadley cell's edge scales as the inverse square root of the
rotation rate. Checked against the three bodies whose banding everybody knows:

```
Venus    turns in  5832.00 h: Hadley edge  90.0 deg, 1 cells (observed: one, pole to pole)
Earth    turns in    23.93 h: Hadley edge  30.0 deg, 3 cells (observed: three)
Jupiter  turns in     9.93 h: Hadley edge  19.3 deg, 5 cells (observed: about six jets)
```

Venus has no trade winds and no jet stream because one enormous cell reaches from
equator to pole; Jupiter is striped because its cells are twenty degrees wide.
Nobody chose either — the rotation period did.

```
latitude   zonal wind
      0      -0.0 m/s
     10     -13.0 m/s     easterly: the trades
     20     -13.0 m/s
     30      +0.0 m/s     the subtropical ridge, calm
     40     +13.0 m/s     westerly
     50     +13.0 m/s
     60      -0.0 m/s
     70     -13.0 m/s     polar easterlies
```

The boundaries are calm and that is not decoration: a boundary is where air rises
or sinks, and vertical air has no east-west preference.

## The wind runs along the isobars

Geostrophic balance: the pressure gradient force and Coriolis cancel, and what is
left is a flow at right angles to both. It is why weather maps are read as flow
maps, and why a low turns rather than filling.

```
a -3213 Pa low at -35.5 N, 1349 km across
circulation around the low is -1.423e+08 m2/s, with winds to 27.6 m/s on the ring
the Coriolis parameter there is -6.03e-05 /s, so a low should turn clockwise
that is 61% of what a wind of that speed all the way round would give
a +1409 Pa high at 40.7 N circulates -1.135e+08 m2/s
```

**The measurement is circulation, not angle**, and the first version got that
wrong. Measuring the angle between the wind and the line to the centre found it
57° off square — because the prevailing westerly is added on top of the storm's
own flow and a thirteen-metre band swamps it locally. That is physically right,
and it makes the angle the wrong instrument.

The line integral of the wind around a closed loop is the right one: **a uniform
flow through the loop contributes exactly zero**, however strong, so what is left
is the storm's own rotation. A low turns with the sign of the Coriolis parameter
and a high against it, and both come out that way from the same formula.

The sign convention bit once on the way: the anticlockwise tangent at bearing `b`
is `(-cos b, sin b)` in east-north, and having it the other way round reported
every storm on the planet spinning backwards. It failed loudly, which is the best
thing a sign error can do.

## Two things this model does not claim

**It has no fronts.** Real cyclones carry a warm front and a cold front with a
temperature discontinuity across each, and this has a smooth Gaussian anomaly.
Fronts are where rain lives, so they arrive with T095.

**Cells are born on a schedule, not by baroclinic instability.** A slot is
occupied for its lifetime and then reused with a new seed; a real storm track
spawns storms when the temperature gradient is steep enough. The schedule
produces the right population and the right places — cells are born on the
boundary between circulation cells, which is the polar front — but the *rate* is
a constant rather than a consequence.

Both are stated rather than hidden, because a model that fades a Gaussian in and
out is not a model that resolves a front, and the difference matters to whoever
reads a wind field expecting one.
