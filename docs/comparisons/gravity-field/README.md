# Coasting between two bodies

T083. The acceptance: *a ship coasting between two bodies follows the trajectory
the two-body model predicts, and the dominant body switches cleanly.*

Those two halves pull against each other. The ship must follow the **two**-body
prediction while actually moving under a field summed from **every** body. The
gap between them is the perturbation, and measuring it is the whole point: too
big and the two-body model is a lie, zero and the summed field is not summing
anything.

## The coast

A circular orbit 2000 km above the planet — 6770 m/s, 128.7 minutes — integrated
with fourth-order Runge-Kutta under the pull of every body in the system, and
compared against a rotation. With no perturbation the ship traces a circle about
the planet at a constant rate, so the prediction at time *t* is the starting
offset turned by `2πt/T`, computed from nothing the integrator touches.

```
  0.25 of an orbit:    69.3 m from the prediction
  0.50 of an orbit:   196.3 m
  0.75 of an orbit:   476.2 m
  1.00 of an orbit:  1272.6 m

worst departure: 1273.0 m in 52277 km of arc, which is 2.44e-05 of the orbit
```

## Three numbers, because one would not have meant anything

A non-zero drift proves nothing on its own — an integrator accumulates error
too, and a field that had quietly dropped every body but the planet would still
wander. So the residue was taken apart.

**Halve the step:** `1273.0 m against 1273.0 m, a change of 0.00%`. The
integration has converged; the residue is physics.

**Take the other bodies out of the field:**

```
with only the planet in the field the coast drifts 166 km,
against 185 km predicted from half a-t-squared on the planet's own
6.20e-03 m/s^2 fall around the star
```

**That came out the opposite way round from what was expected, and it is the
better result.** The planet-only field is not *slightly* worse — it is a hundred
and thirty times worse. The frame is not inertial: the planet is falling around
the star, so a ship that does not feel the star does not accelerate with it and
falls out of the frame at ½at². Predicted from that acceleration alone: 185 km.
Measured: 166 km.

The star is not a small correction to a coast near a planet. It is what keeps
the ship *with* the planet at all.

### And a mistake on the way to that

The first attempt to isolate the field zeroed the other bodies' **masses**. That
also stops the planet's own orbit — its mean motion is computed from its
parent's mass — so the planet stood still while the ship kept the velocity it
had been given, and the test reported a drift of **1.4e11 m** and blamed the
integrator.

Only the field may be cut down. Where the bodies *are* is not the variable.

## The switch

```
  body 1 (planet):    sphere of influence   893795 km
  body 2 (moon):      sphere of influence    69022 km
  body 3 (gas-giant): sphere of influence 41998298 km
  body 4 (station):   sphere of influence        0 km

walking the 388515 km from planet to moon in 200000 steps:
1 change, from body 1 to body 2, at 69020 km from the moon
```

**Exactly once, two kilometres from the boundary** — which is one step of the
walk. A boundary that chattered, flicking between two answers over a few
kilometres, would make a trajectory planner switch reference frames dozens of
times in a row, and every switch is a chance to lose a digit.

The station's sphere of influence rounds to zero, which is correct: a hundred
tonnes has no meaningful gravitational reach, and a ship near one is on the
planet's orbit with a station nearby rather than the other way about.

## Inside a body

```
  1.00 of the way out: 9.549345 m/s^2
  0.50 of the way out: 4.776357 m/s^2
  0.05 of the way out: 0.480691 m/s^2
  0.00 of the way out: 0.006189 m/s^2
```

Nothing should ever be down there. But a trajectory that ends up inside a planet
should produce a wrong answer rather than an infinity that poisons every step
after it, so the field falls linearly to zero at the centre — the mass above you
stops pulling — rather than dividing by nothing.

At the surface the summed field is 9.549345 m/s² against the planet's own
9.545979: everything else in the system is three parts in ten thousand.
