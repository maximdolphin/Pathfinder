# The quoted trip against the flown one

T087. A travel time nobody flies is a number in a menu. The acceptance is that
the quote matches what actually happens, so the test flies it: a ship under the
full gravity field of every body in the system, steering itself, arriving when it
arrives — and the quote never gets to see how that went.

## What the map answers

Nothing here is stored. A site's position is a function of the ephemeris and the
time, and a travel time is a function of both endpoints and the ship. That falls
straight out of T070's decision to solve rather than integrate, and it is what
lets a route be priced for next Tuesday as cheaply as for now.

The crossing is a brachistochrone, not a Hohmann transfer: for a ship that can
thrust the whole way the fast route is the straight one, and from rest that is
`T = 2 sqrt(d / a)`. Two things make it more than a formula.

**The ship does not start from rest.** It leaves a body that is already moving,
so it has a closing speed `v` along the route and the flip is no longer halfway:

```
T = (2 sqrt(v²/2 + a d) - v) / a
```

**And the destination moves while you are on the way.** The distance to solve for
is the distance to where the site *will* be, which depends on how long the trip
takes, which depends on the distance. So the quote iterates that fixed point
rather than measuring the gap at departure and calling it the trip.

## The result

Three departures spread over the home year, home world to gas giant, at one
gravity of thrust:

```
day   0: 5.697 au, quoted 6.80 days, flown 7.34 days -- +7.90%
day 178: 7.780 au, quoted 7.93 days, flown 8.76 days -- +10.39%
day 385: 7.080 au, quoted 7.54 days, flown 8.10 days -- +7.51%
worst speed at the 1000 m gate: 133.6 m/s, against the 132.9 m/s that stops in
that distance
```

Arriving is not the same as passing nearby. The speed at the gate is not
compared to zero — a ship a kilometre out is still moving, and should be, at
exactly the speed that lets it stop in a kilometre. It is within **0.5%** of that
number in all three flights.

The eight-to-ten per cent is accounted for rather than tolerated:

- The deceleration curve is drawn at 90% of the engine so there is something
  left for steering. That is 1/0.9 on the second leg, **2.7% of the trip**, by
  arithmetic and not by measurement.
- The rest is crossways. The destination moves across the route at tens of
  kilometres a second and the ship has to arrive matching it; the delta-v ledger
  shows **1,154 km/s of the 6,140 burned going sideways**. The quote does not
  model that, and a quote that did would be pricing one particular autopilot
  rather than the trip.

## Four pilots, and the one that worked

Every pilot except the last came out at **+21%**, and the fact that three
completely different laws produced the same number was the clue: it was never the
steering.

**Pure pursuit** — point at where the destination is right now — arrived
reliably and ran a fifth long. Following a line of sight that rotates needs a
lateral acceleration of `v · v_across / d`, and as `d` collapses that runs away.

**Lead pursuit** — aim where the destination will be in the pilot's own estimate
of the time left. The estimate moves, so the aim point moves with it by the
target's speed times that estimate: a jittering aim point millions of kilometres
wide. Missed by 118,000 km at 57 km/s.

**Fly the plan** — aim at the quoted intercept, a fixed point that cannot rotate
away. The ship gets there and the destination is not, because the flight ran
long. Missed by 577,000 km, which is the quote's own error rendered as a
distance. Sharp, and useless as a gate: it only opens on a perfect quote.

**Split throttle** — command the along-track brake directly and give the
crossways correction the remainder. This is where the trace finally showed what
was happening:

```
t=3.40d d=3.04au  closing=2682573  stoppable=2911729   (below the curve)
t=4.08d d=2.01au  closing=2390816  stoppable=2365456   1% above
t=5.44d d=0.55au  closing=1340997  stoppable=1234245   9% above
t=6.12d d=0.14au  closing= 805483  stoppable= 622858  29% above
t=6.70d d=0.04au  closing=-330202                      flew past at 330 km/s
```

**Drawing the curve and the brake at the same 95% means a ship that drifts above
the curve can never come back.** Braking at the curve's own rate holds the gap
while the gap grows, because being too fast means covering ground faster than the
curve was drawn for. It flew past and spent a day and a half yo-yoing. Giving the
brake full authority fixed the overshoot and doubled the delta-v instead, because
then it chatters across the curve.

A guidance bug announces itself slowly: 1% at four days, 9% at five, 29% at six.

The pilot that works is the oldest one there is — **velocity-to-be-gained**, at
full throttle towards the difference between the speed it wants and the speed it
has, with the curve at 90% and the burn capped at what is actually wanted.

## What was ruled out

- **Integration error.** Twenty times finer steps changed the answer by 0.05
  percentage points (21.66% against 21.71%). It was never the integrator.
- **The acceleration leg.** It matches theory to 4%, and the peak speed matches
  `sqrt(0.9 a d)` to better than 1% — 2,790 km/s measured against 2,819
  predicted. Everything wrong was in the second half.

## Two things the quote does not price

**The climb out of a well.** These are transit quotes, and a site is defined for
the test as the radius where the body's pull is a tenth of the ship's thrust —
14,067 km up at the home world, 291,708 km at the giant. Inside that, arriving is
a landing rather than a crossing.

The first version used one body radius, which sounds generous and is not: a gas
giant still pulls about 7 m/s² two radii out, so a one-gravity ship trying to
stop there had three of its ten metres per second squared left and arrived like a
meteor. That is a real fact about gas giants and it belongs in the definition of
the site, not in a widened tolerance.

**A landing site is not a planet.** A site turns with its body — 180.00° half a
rotation later, still 20,496 km from the centre — and the quote follows it.

## The shape of the answer

```
over one home year Home to Giant 2 runs from 5.162 au (day 568, 6.46 days of
travel) to 8.032 au (day 247, 8.13 days)
1.56 times as far is 1.26 times as long, against a square root of 1.25
```

Travel time goes as the **square root** of distance, not linearly with it. That
is the whole character of a torch ship: twice as far is not twice as long, so the
far side of the system is closer than it looks.
