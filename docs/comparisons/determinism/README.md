# The sky and the ephemeris, and a seed across two processes

T086. Both halves of this task check the code against **itself** rather than
against a number somebody worked out — which is a weaker claim per test and a
much harder one to satisfy by accident, because there is nothing to tune
towards.

## The test that could not fail

The first draft asked the navigator's identity:

```
sin(altitude) = sin(latitude) sin(declination)
              + cos(latitude) cos(declination) cos(hour angle)
```

against `LedgerSky::SolarAltitude`, at a hundred random times and places. It
agreed to **zero arcseconds**, and the zero was the tell. `SolarAltitude`,
`SolarDeclination` and `HourAngle` are all derived from the same
`SunDirectionInBody` vector, so the identity was checking spherical trigonometry
against itself. It could not have failed. A test that cannot fail is not
evidence, and this project has already been caught once by rules that passed for
somebody else's reason.

## The test that could

The replacement goes the other way round the frames.

- **The sky** rotates the star's direction from the system frame *into* the
  body's (`UnrotateVector`) and projects it onto an east-north-up basis.
- **The test** rotates the observer *out* of the body frame into the system's
  (`LedgerFrames::ToSystem`) and does the whole calculation there: up is the
  observer's position minus the body's centre, and the sun is the star minus the
  observer.

Nothing is shared but the orbit and the orientation, which is exactly what is
being checked.

```
100 samples over a year of 49604144 s and the whole globe; the worst
disagreement is 6.4705 arcseconds, at latitude 18.0 with the sun 0.6 degrees up
the body's radius over its distance to the star is 6.4802 arcseconds, which is
what standing on the ground rather than at the centre is worth
```

Inside the arcminute the acceptance asks for, and the residual **has a name**.
The sky answers with the direction from the body's centre — the geocentric
convention, and the right one for lighting a whole terrain patch with one sun.
The test answers from the ground. The difference between those is parallax, it
is the body's radius over the distance to the star, and the worst case lands
with the sun 0.6° above the horizon, which is precisely where parallax is
largest.

So the residual is not slop to be widened away. It is a prediction — 6.4705
measured against 6.4802 predicted — and the test asserts it as one, with a floor
as well as a ceiling. **A residual much smaller than parallax would be the
interesting failure**: it would mean the observer never left the centre and the
two roads had quietly merged, which is how the first draft died.

## One seed, another process

The cheap half is the same seed generated twice in one process and compared bit
for bit, which catches a static, an uninitialised field, or a container whose
order depends on an address.

The expensive half is a committed table — 44 rows, every body at four times,
positions to the millimetre — in `EphemerisGolden.txt`. **A committed table is a
second process.** It was written by a build that is no longer running, on a day
that is over, and every later run is the comparison.

```
44 rows against the recorded run; the worst coordinate differs by 0.0000 m
```

One of the four times is **negative**. An epoch is a label, not a beginning, and
a Kepler solve that only worked forwards would pass every other test in this
module.

This is the only test here that can fail because of the compiler rather than the
code, and that is deliberate: ARCH Rule 5 says the same seed means the same
world, and a world that depends on who built it does not obey that. If the table
ever needs updating, it is updated by hand and the reason goes in the commit —
the same discipline as the disk cache's format version.
