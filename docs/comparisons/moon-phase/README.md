# A moon, photographed around its month

T074. The acceptance: a moon's phase matches the sun-moon-observer geometry at
any time and from any body in the system.

Two things had to be true, and they are checked separately because they can fail
separately: that the computed phase is right, and that the thing in the sky is
the thing the computation describes.

## The computation

`Ledger.SkyBody.PhaseMatchesTheSunTargetObserverGeometry` compares
`(1 + cos α) / 2` against counting. The counting spreads 200,000 points evenly
over the target's sphere, marks each one lit if it faces the star and visible if
it faces the observer, and weights it by how square-on it is. No phase angle
appears anywhere in it.

Twenty combinations — every non-star body observed from every other, at eight
points around a month plus two much later times:

```
worst disagreement 0.003751
  body 1 seen from body 2 at t=1.96e+06 s, phase angle 125.3 deg,
  formula 0.21099 against sampled 0.20724
```

**The residual is not error.** The formula is the far-field one and assumes the
observer sees exactly a hemisphere; a real observer a finite distance away sees
slightly less. The worst case is the planet seen from its own moon, which is
where that ratio is largest in this system. Four parts in a thousand of the
disc.

`FullAtOppositionAndNewAtConjunction` pins the ends and the direction of travel,
because two computations that both ran backwards would agree with each other
perfectly. Across one month the moon runs from **0.0003 lit to 0.9996 lit**.

## The photograph

`UnrealEditor.exe client/Ledger.uproject -game -moonshot` stands a camera on the
ground with a 10° lens and photographs the largest non-star body at eight points
around its month, forwarding at each point to the moment it is highest.

```
step   lit      across      altitude
   0   61.0%   0.642 deg     +85.0 deg
   1   26.6%   0.608 deg     +85.9 deg
   2    4.9%   0.585 deg     +78.0 deg
   3    0.7%   0.581 deg     +66.6 deg
   4   14.6%   0.596 deg     +56.0 deg
   5   44.5%   0.627 deg     +51.1 deg
   6   80.3%   0.659 deg     +57.0 deg
   7   99.7%   0.669 deg     +71.5 deg
```

**Nothing draws a phase.** The body is a sphere in the sky lit by the same
directional light as the ground, so the terminator is wherever the geometry puts
it and there is no second implementation to disagree with `LedgerSky`.

Which makes the pixel count an independent check on both. Counting lit pixels in
each frame, against what the computed fraction and angular diameter predict:

| step | computed | rendered px | predicted px | error |
|-----:|---------:|------------:|-------------:|------:|
| 0 | 61.0% | 3030 | 3145 | −3.6% |
| 1 | 26.6% | 1105 | 1230 | −10.2% |
| 2 | 4.9% | 252 | 210 | +20.2% |
| 3 | 0.7% | 95 | 30 | +221% |
| 4 | 14.6% | 649 | 649 | +0.1% |
| 5 | 44.5% | 1996 | 2188 | −8.8% |
| 6 | 80.3% | 4107 | 4362 | −5.8% |
| 7 | 99.7% | 5581 | 5581 | 0.0% |

Within about ten per cent everywhere the disc is more than a sliver, and
badly out at step 3 — where the crescent is twenty pixels of limb and the count
is measuring antialiasing and the sky light's faint contribution rather than
sunlight. That row is the threshold's problem, not the phase's.

The angular diameter is worth noticing on its own: **0.581° to 0.669° around one
month.** The orbit is eccentric, so the moon is nearer at some phases than
others, and nothing was written to make that happen.

## Three bugs found by looking

The tests were green through all three. Each one produced eight frames of empty
sky while the log insisted the moon was overhead.

- **A degenerate camera basis.** The fixture forwards to the moment the body is
  *highest*, which puts it within a few degrees of the zenith — and there the
  look direction and local up are the same vector, so `MakeFromXZ` has no basis
  to build. It now falls back to east for the roll reference when the target is
  overhead.
- **A one-step-stale sky.** The spheres are placed by `ULedgerSkyBodies` on its
  own tick, so setting the clock and photographing in the same frame
  photographs the sky as it was one step ago. Aiming and capturing are now two
  ticks.
- **A clock that moved the moon but not the sun.** `SetWhenSeconds` set the
  number; the directional light kept the direction it was given at begin play.
  So a body the ephemeris called 99.7% lit was photographed as a thin
  crescent — lit correctly, for the wrong moment. The clock now moves the sun,
  which is what a clock is for.

## What is still wrong

**With the atmosphere on, the moon is invisible.** These frames were captured
with `showflag.Atmosphere 0`. The aerial perspective in this project is strong
enough that a body on a 500 km sky shell is erased completely — the same
over-dense atmosphere measured in `docs/comparisons/day-sweep`, where ground two
kilometres away is already washed to flat blue.

That is not this task's to fix, and it is not a small thing: a moon nobody can
see through the air is a moon nobody can see. It belongs with the atmosphere
calibration in M04, and the measurement is here so that work starts from a
number.
