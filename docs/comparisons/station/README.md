# A station on rails, and a ship that stays docked

T082. The acceptance: *a station's position matches its orbit over a simulated
month, and a docked ship stays docked.*

## On rails

A station is a body. It has a parent, an orbit, and the ephemeris puts it where
it belongs like everything else — nothing new was needed for the first half of
the acceptance except a fourth row in the system description.

```
station at 556 km altitude, 96.7 minute orbit, inclination 28.4 degrees

over 30 days in 20000 steps, the two routes to the station's position
differ by at most 0.000000 m
one orbit returns it to within 0.000000 m
```

Two routes: the whole-system walk that sums a parent chain, and the direct
"parent's place plus the station's own orbit". A station is two orbits deep, so
the sum has to come out the same either way — and it does, exactly.

## The frame is the orbit's, not the stars'

A station needs an attitude, which a planet never does: "which way is up on it"
is a question a sphere does not have to answer.

Up is away from the parent, forward is along the track, and the third axis
follows. That is the local-vertical local-horizontal frame every real spacecraft
uses, and it costs nothing extra because both vectors are already in the state
the ephemeris returns.

```
around one orbit: up is 1.479e-06 degrees off outward,
                  the basis is right-handed to 1.453e-10
the flight path angle reaches 0.0939 degrees
```

**Forward is not exactly the velocity, and should not be.** On an eccentric
orbit the velocity has a radial part — the station climbing and falling — and
that belongs to up. The angle between them is the flight path angle, zero only
on a circle, and 0.0939° here is this orbit's eccentricity of 0.00164 showing
itself.

## Staying docked

```
over 30 days in 20000 steps: the dock stays 41.665 m out to within 3.333e-05 m,
and its local coordinates return to within 3.487e-05 m
across a quarter orbit the docked point travels 44143 km
```

Thirty-three microns over a month, while being carried forty-four thousand
kilometres in a quarter of an orbit.

**Both bounds started tighter than the arithmetic can reach.** The station is
1.5e11 m from the primary and the dock is 42 m from the station, so recovering
the second from the first throws away eleven digits: a double's last bit at
1.5e11 is about 20 microns, and both figures above are a couple of those. The
first bound written here was a micron — finer than any correct implementation
could ever have met. The bound is now stated against the floor it sits on.

The same mistake in a different place: the frame axes were first probed with a
one-metre lever arm, which put 20 microns of subtraction noise on a 1 m vector
and reported a thousandth of a degree of false lean. A hundred-kilometre arm
reads the same direction with the noise divided by a hundred thousand.

## What the station broke, and why that is the point

Adding a fourth body to the system **immediately failed a test that had been
green for two tasks.** `Ledger.SkyBody.PhaseMatchesTheSunTargetObserverGeometry`
walks every pair of bodies, and the station is 556 km above a 6320 km planet:

```
body 1 seen from body 4: formula 0.38102 against sampled 0.05268
```

The formula was never wrong. It is the far-field one — `(1 + cos a) / 2` assumes
the observer sees a hemisphere — and nothing in the system had previously been
close enough to anything to leave that domain.

So the domain got measured instead of assumed:

```
  at  1.05 radii: out by 0.3143      at  3.00 radii: out by 0.0661
  at  1.50 radii: out by 0.1822      at  5.00 radii: out by 0.0360
  at  2.00 radii: out by 0.1147      at 10.00 radii: out by 0.0169
                                     at 40.00 radii: out by 0.0040
```

**A per cent wants about forty radii.** Five was the figure guessed before
measuring — and written into the exclusion before the table existed — and it is
out by nearly an order of magnitude. The documented domain on
`IlluminatedFraction` is now the measured one, and there is a test that walks an
observer in and records the breakdown, so nobody removes the restriction on the
grounds that it seemed to work.

Every real pair clears it easily: the closest is a planet seen from its own moon
at sixty-seven radii. The only thing that trips it is something in low orbit,
which is exactly what a station is.
