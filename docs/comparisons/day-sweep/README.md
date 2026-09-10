# One place, one camera, one day

T072. The sun's place is a consequence of the orbit and the rotation, and this is
that claim photographed rather than asserted.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -daysweep`. It takes the
site the world already chose, stands a camera 40 m above it looking east, and
captures eight frames across one rotation with **nothing changing between them
except the time**. It writes `out/day-sweep-0.png` … `-7.png` and
`out/day-sweep.txt`.

## What it measured

```
rotation period       77719 s (21.59 hours)
local noon at         72471 s from epoch
axial tilt             17.2 degrees

step   fraction of day   solar altitude
   0           -0.500         -63.43
   1           -0.375         -38.18
   2           -0.250           2.26
   3           -0.125          44.04
   4            0.000          75.09
   5            0.125          44.01
   6            0.250           2.22
   7            0.375         -38.29
```

Symmetric about noon, sunrise and sunset landing either side of it within a
fortieth of a degree of each other, and the peak exactly at step 4 — which is
the step the ephemeris put noon at, computed before any of the frames were
rendered.

The small asymmetry is real and is the same effect
`Ledger.Sky.LocalNoonIsWhenTheSunIsHighest` reports: 44.04 against 44.01, and
2.26 against 2.22, because the star keeps moving in declination while the planet
turns. It is not noise. Running it again gives the same numbers.

The frames agree with the table:

| step | solar altitude | sky | distant ground |
|-----:|---------------:|----:|---------------:|
| 0 | −63.43 | 8.7 | 12.4 |
| 1 | −38.18 | 0.0 | 0.0 |
| 2 | +2.26 | 145.0 | 115.3 |
| 3 | +44.04 | 200.9 | 179.3 |
| 4 | +75.09 | 152.7 | 126.3 |
| 5 | +44.01 | 127.0 | 115.6 |
| 6 | +2.22 | 69.0 | 44.2 |
| 7 | −38.29 | 4.3 | 3.7 |

Mean brightness out of 255. Step 1 is fully black: the sun is 38° below the
horizon and there is no moon in the frame. Steps 3 and 5 are the same altitude
and read 200.9 against 127.0, which is the atmosphere being looked through in
different directions rather than a difference in the light.

## The near ground is black, and it is not this task's doing

Every daylight frame has a foreground reading 0 to 7 out of 255 while the sky
and the distant ground track the sun exactly. That is worth stating plainly
because it makes these photographs worse than they should be, and because the
cause is now measured:

```
noon, sun 75 degrees above the horizon, eye 40 m above the ground

unlit base colour                      foreground 128.90   distant 217.91
lit, with fog and atmosphere off       foreground   0.00   distant  26.56
```

**The terrain has a perfectly good albedo and takes no direct light.** What a
normal frame shows is in-scattered air in front of a black surface, which is why
the brightness falls smoothly from 190 at the horizon to 0.24 at the bottom of
the frame: that gradient is the scattering integral, not the ground.

Ruled out, each by measurement rather than by argument:

- **Shadows.** `showflag.DynamicShadows 0` changes the foreground from 6.96 to
  6.95 and 3.87 to 3.87.
- **A stale baked material.** Re-running `-bakematerials` changes nothing.
- **The camera being under the ground.** The terrain query and the height field
  agree on the site to 0.0 m; the fixture reports both.
- **Another fixture fighting for the ship.** The flight harness was indeed still
  running — that is fixed, it now stands down for `-daysweep` — and standing it
  down changed no measurement.
- **The triplanar normal missing its sign.** That was a real defect and is fixed
  in `LedgerMaterialGraph.h`: a normal map's blue channel is the component out
  of the surface and is always positive, so swizzling it onto +x, +y or +z
  assumed every projection faced the positive axis, which on a sphere is half of
  it. Correcting it moved the render by nothing at all — the frames are
  bit-identical — which is itself the finding: **if changing the surface normal
  changes no pixel, the surface is not being shaded.** The cause is upstream of
  shading.

So the sun is where the ephemeris says, at every hour of the day, and something
between the directional light and the terrain is dropping the light entirely.
The second half is not T072 and is written down here so the next person starts
from these numbers instead of from the pictures.
