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

## A correction: what the near-black foreground actually was

The first version of this page claimed the terrain took no direct light at all,
on the strength of a foreground band in these frames reading 0 to 7 out of 255
while the sky and distant ground tracked the sun. That claim was wrong, and the
way it was wrong is worth more than the claim was.

**The measurement was of the camera's framing, not of the ground.** This fixture
stands 40 m up and looks at the horizon, so the bottom fifth of the frame is a
slope seen almost edge-on and falling away. Running the same sweep at the site
the project used before T072 -- a 660 m lowland instead of a 2.4 km mountain --
gives a foreground of **0.00 at every one of the eight steps**, darker still,
while the scripted flight at that same site photographs ground at **97 and 102
out of 255**. Same build, same lighting, same hour. The ground is lit; this
fixture's lower frame edge is not a good place to ask about it.

Three separate errors produced the original claim, all of them mine:

- **Two Unreal processes at once.** Several comparisons were run with a second
  instance still going. One of them never wrote fresh captures at all, so a
  "before and after" was measured against leftover PNGs from a different commit,
  and reported a difference that did not exist. This project's notes already
  record that exact trap; it was walked into again to save wall-clock.
- **A conclusion from one site.** Everything was measured at the site T072
  happens to choose, which is a mountain. Nothing was checked anywhere else
  until much later.
- **A fix validated where it could not show.** The triplanar normal was missing
  the sign of the facing axis, which is a real defect and is fixed. It was then
  tested only in the frames above, where the foreground was dark for an
  unrelated reason, and pronounced inert. It is not inert; it was untestable
  there.

## What T072 did change: the landing site moved

The sun's direction chooses where the world lands -- `ChooseSite` wants relief
and daylight, so a different sun qualifies a different band of the sphere. The
hardcoded sun put the site on a **660 m lowland**; the ephemeris sun at t = 0
puts it on a **2.4 km mountain**.

That is the intended behaviour of a sun that is no longer hand-placed, and it
has a consequence worth stating: **every committed reference capture is of the
old site.** Comparing the flight against them now compares two different places
and reads as a regression:

| capture | old site | new site |
|---|---|---|
| terrain-surface | 97.05 | 23.30 |
| terrain-town | 102.36 | 12.89 |
| terrain-coast | 19.57 | 0.50 |

Mean brightness of the lower third, out of 255. The references need regenerating
against the site the world now chooses, or the site needs pinning independently
of the sun. That is a decision, not a bug, and it is recorded on T072 rather
than settled here.

The one measurement from the original investigation that still stands on its own
is the atmosphere's share of the light: with `SetAtmosphereSunLight(false)` the
same frame reads 177.20 against 3.87. Most of what reaches the ground at this
site is being taken by atmospheric transmittance. Whether that is correct for a
2.4 km site under a 62-degree sun is a question for M04, and it is not what this
task was about.
