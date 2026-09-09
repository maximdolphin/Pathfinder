# Giving the planet relief (T428)

The mountain band of the height function was exactly zero on ninety per cent of
the land, and the whole planet topped out at **1,372 m** against a configured
9 km. This is what was wrong, what it was replaced with, and what it cost.

## What was wrong

`LedgerNoise::ErodedRidged` ends with

```cpp
return Normalisation > 0.0 ? (Sum / Normalisation) * 2.0 - 1.0 : 0.0;
```

which is the right remap for a field whose mean is 0.5. This one's is not. A
ridged multifractal with a continuity weight and erosion damping concentrates
its energy into a few ridges, so most of the surface sits low. Surveyed over the
land of this planet, the returned value runs:

```
ridge field before the clamp, min -1.0000 max 0.4399, deciles:
  -0.8383 -0.7311 -0.6385 -0.5646 -0.4926 -0.4297 -0.3566 -0.2708 -0.1388
```

Median **−0.49**, not 0. So `FMath::Max(0.0, ...)` in `Elevation` was deleting
everything below about the ninety-seventh percentile.

**The measurement came first.** The distribution above is printed by the
`-climate` fixture, and the replacement is chosen against it rather than against
an assumption about what a ridged multifractal returns.

## What replaced it

Two changes, and the second only became visible once the first was made.

**A pivot and a span, from the survey.** `RidgeStrength` maps the field onto
[0,1] over [−0.20, 0.44] — the pivot near the eighty-fifth percentile so ranges
stay rare, the span carrying the rest up to one so a peak gets its full share of
`MaxElevation` instead of the 0.44 the old remap left it.

That took the planet from 1,372 m to **1,911 m**, and then stopped.

**`Land^0.7` instead of `Land^1.4` in the range mask.** The continent mask
itself only reaches 0.581, so `Land^1.4` capped the range mask at 0.472 — and
the survey found no point where a strong ridge and high land coincided at all,
because the two are independent fields. At 0.7 a range can stand on ordinary
continent while the mask still keeps it off the shelf and out of the sea.

## What the planet is now

| | before | after |
|---|---|---|
| highest land | 1,372 m | **2,462 m** |
| land altitude p99 | 1,139 m | 1,225 m |
| steepest slope | 57.0° | **62.1°** |
| land at or above 30° | 0.42% | **1.08%** |
| land at or above 60° | 0.000% | **0.015%** |
| major ranges on the planet | **2** | **609** |
| mountain term, max | 0.056 | 0.333 |

`ridge-sweep.png` is the flight's ridge phase: a peak with a snow line, rock
showing through the steep faces, a ridge running off it, and further ranges on
the horizon. The biome and material work from earlier in M02 needed exactly this
— there was nothing for a lapse rate or a slope-driven material to act on.

**T054 is unblocked by this**: a sixty-degree face now exists. So is T059's
premise: there are ranges to silhouette.

**T051 is not.** The rain shadow still is not there — 609 ranges, 297 of them
shadowed, and the whole-planet ridge statistic at 58% against a coin toss. Half
the land is desert, so on most ridges the air arrives dry and there is nothing
left to wring out. That is the climate model's business, not the terrain's, and
it is now a question that can actually be asked.

## What it cost

A flat planet is cheap. This is the price of a world, measured over the same
scripted flight:

| phase | terrain, before | terrain, after | GPU, before | GPU, after |
|---|---|---|---|---|
| surface | 1.28 | 10.5 | 7.2 | 11.7 |
| town | 1.30 | 9.4 | 7.4 | 11.2 |
| ridge sweep | 8.40 | 8.7 | 6.5 | 6.3 |

Frame p99 went from 31.5 ms to 32.8. The flight's budget verdict was already
FAIL before this and still is.

That is a real regression and it is the point: T063's streaming budget and
T064's shadow question were both being asked about a planet with nothing on it.
The budget they compete for has gone from tenths of a millisecond to several.

**And it found a defect in T063's own LOD brake.** The brake raises the error
threshold when the section pool fills; with relief, occupancy reaches that point
and the loop began to chase itself — raise the threshold, collapse nodes, free
sections, lower the threshold, split again. The signature was the patch cache at
88% reuse over 158,000 hits: patches cycling rather than work being done, and
the terrain at 11 ms. A dead band fixes it — raise above full, fall back only
below 85%, hold in between:

| | terrain (surface) | p99 |
|---|---|---|
| brake chasing itself | 11.0 | 38.0 |
| brake off (`-nolodbrake`) | 9.0 | 31.3 |
| brake with a dead band | 10.5 | 32.8 |

`-nolodbrake` stays as a control arm, because a feedback loop that cannot be
switched off cannot be measured.

Raw reports: `relief-survey.txt`, `before.txt`, `after.txt`.
