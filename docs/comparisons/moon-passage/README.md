# A moon rises, crosses and sets — on schedule, and behind cloud

T088, the M03 gate. **The schedule is right and the photographs do not show it**,
and those are two separate statements that have to be made separately.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -passage`

## The schedule

Nothing here looks for a moon. The ephemeris is scanned for the horizon
crossing, the rise and set are bisected out of that bracket, the transit is a
ternary search between them, and every camera position afterwards is one of
those three numbers plus an offset.

```
body            3, a moon of body 1
up for          10.04 hours, rise to set

moment         when (s)     predicted   rendered   out by
before rise        22548     -13.51     -13.51     0.00'
rise               25440      +0.00      +0.00     0.00'
climbing           34475     +43.49     +43.49     0.00'
transit            43509     +79.19     +79.19     0.00'
descending         52553     +43.46     +43.46     0.00'
set                61596      +0.00      -0.00     0.00'
after set          64489     -13.47     -13.47     0.00'
```

**"Out by" is not an independent opinion and should not be read as one.** It is
the altitude of the sphere the renderer placed, read back off the actor through
`ULedgerSkyBodies::WorldPositionOf`, against the altitude the ephemeris
predicted. That is a round trip through the surface-to-world conversion, so zero
is what it ought to say — and it would not say zero if that conversion dropped
the tilt or swapped an axis, which is the whole reason it is measured. The claim
it supports is "the renderer puts the moon where the ephemeris says", not "two
models agree". T086 is where the second claim lives.

And the same moon, priced as a destination by the map from T087: **343,607 km at
transit, 5.4 hours at a gravity of thrust**, surface gravity 1.28 m/s², no air.
The sky and the map are two views of one system; if they disagreed, the body
whose rising is timed here would not be the body whose crossing is quoted.

## The camera, and a wrong idea about it

The first version held **one fixed frame all night**, on the theory that a camera
which tracks its subject cannot show it rising. True, and it produced seven
photographs of empty sky: the moon transits 79° up at this site and no frame
contains both that and the horizon. A sky is not a stage.

The camera is now aimed at the moon's *predicted* direction at each moment. That
is not tracking — nothing looks for the moon, the bearing and the altitude are
both read off the ephemeris before anything is drawn, and the frame's job is to
show whether the moon is there. At rise and set the aim is level, so the horizon
runs through the middle of the frame and the moon should sit on it.

## What the photographs actually show

They show that this is blocked, and by something that already has a name.

- **passage-1 (rise)** — night. A hard horizon under faint cloud, and nothing
  above it.
- **passage-3 (transit)** — the moon is 79° up and the frame is filled edge to
  edge with cloud.
- **passage-5 (set)** — a mountain ridge against a sky washed out to near-white.

The moon is 0.72° across at this distance, which is 69 pixels in a 20° frame at
1080p. It is not too small to see. It is behind an atmosphere that is too dense
and a cloud layer at the wrong altitude, and the daylight frames are saturated
on top of that.

**This is the same blocker as T074's moon phase, T075's totality and T077's star
field** — four tasks whose arithmetic is checked and whose pictures are of a
white sky. It is calibration work, and it belongs to M04: T089 (atmosphere
profile per body) and T094 (cloud layers at real altitudes).

## Re-run after T089: half the blocker is gone

With the atmosphere computed from the body's own air rather than from four
hardcoded floats, the sky is no longer washed out — the home world's daylight
sky is blue and the deck is broken cumulus at 1.2 to 5.2 km rather than solid
overcast at 2 to 8.

The transit frame is now a broken deck with black gaps in it, and the moon is
behind a patch. That is a weather problem — *where* the clouds are — rather than
a sky problem, so the remaining block moves from T089 to **T092** (the
pressure-cell weather model) and **T094** (cloud layers at real altitudes).

## And then it was photographed

The frames exist now. The night was black for three reasons and none of them was
the orbital model — see `docs/comparisons/night-sky/`:

- an auto-exposure floor at −4 EV100, which is a moonlit landscape and clamped
  every starlit one to black;
- a star field computed in T077 and never drawn by anything;
- and a 1.5-second settle photographing the middle of a twenty-seven-stop
  exposure adaptation.

With those fixed, the rise frame shows the moon on the horizon under a field of
stars, the transit frame shows it at 79° with a gibbous limb, and the set frame
shows it pale and low in a brightening sky over snow. Every one at a time the
ephemeris chose before anything was drawn.

**What is left of T088 is its second clause**, not its first: "flown to and
landed on, in one session". The world builds one body's terrain at startup and
the sky renderer places bodies for an observer on the home surface, so the
crossing between them is still two processes.

## So the gate does not pass yet

Of the eight M03 gate checks, the six that are arithmetic are green and have
been for several tasks. The two that are captures — the eclipse observed from
the surface, and this passage — are recorded as **blocked behind the atmosphere,
not as failures of the orbital model**. Nothing in M03 is known to be wrong. The
evidence that would close it cannot be photographed through the sky M04 has yet
to calibrate.

Rule 6 asks for a milestone to end with something watched. This one ends with
something computed correctly and photographed through fog, and saying so is
worth more than a screenshot taken with the atmosphere switched off.
