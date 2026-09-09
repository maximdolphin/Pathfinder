# Climate, and the terrain it has to describe (T051)

Temperature and moisture as continuous functions of position, so biomes can be
read from them rather than painted. `LedgerClimate::At` is a pure function on
the same terms as the terrain math — no world, no actor, safe off the game
thread.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -climate`. It writes
`out/climate.txt` in a few seconds and exits.

## Temperature: done

Latitude band plus a lapse rate. `SeaLevelTemperatureC` is the band alone and is
what the acceptance's "monotonic pole to equator" is about; `TemperatureC`
includes altitude and is **not** monotonic, because a mountain at the equator is
colder than a beach at forty and a model where it isn't is broken.

Four automation tests, all passing, in about 90 ms:

- `Ledger.Climate.SeaLevelTemperatureIsMonotonicPoleToEquator` — strictly
  increasing at every one of 91 degrees, ends at exactly 30 °C and −25 °C.
- `Ledger.Climate.AltitudeCoolsAtTheLapseRate`
- `Ledger.Climate.MoistureStaysInRange` — the march multiplies and lerps its way
  along forty steps and an unbounded result would poison every biome read from
  it.
- `Ledger.Climate.WindBandsReverseWithLatitude` — trade easterlies, mid-latitude
  westerlies, polar easterlies, so a shadow falls on opposite sides of a range
  at 20° and 45°.

## Rain shadow: not demonstrated, and the reason is the terrain

The transect finds **2 major ranges on the whole planet** and both are shadowed.
That reads like a pass and is not one: at a base rate of 56% (below), two
successes in two attempts happens by chance about a third of the time.

So the report also asks the question of every wind-crossing ridge on the
surface — a point higher than both the ground the air crossed to reach it and
the ground it descends onto, the two flanks compared against each other:

```
every wind-crossing ridge: 572 of 1018 drier on the lee side (56%)
```

56% is a coin toss. **There is no rain shadow**, and the verdict now says so
rather than passing on two landmarks.

### It is not the model's resolution

Two guesses were tried and both were wrong, which is worth recording because
each looked obviously right:

- **A step too long to see ridges.** The march was sixteen steps over 400 km —
  25 km a step, wider than a ridge, so orographic lift never fired. Cut to 40
  steps over 300 km, 7.5 km a step. The figure moved from 54% to 50%.
- **A confounded statistic.** Comparing every point where the ground rose
  against every point where it fell said rising ground is *wetter*, 0.361
  against 0.293 over 2,600 points each. Real, and it measures distance from the
  sea: ground that rises on the last step is the windward coast, where the air
  has just come off the ocean. Hence the paired design above.

### It is that this planet has no mountains

The relief survey at the top of the report, 16,110 points:

```
lowest -4735 m, highest 1372 m
land is 31.0% of them; land altitude p50 241 m, p90 673 m, p99 1139 m
```

The highest land on the planet is **1372 m**, and `MaxElevation` is configured
at **9 km**. The height function is using about fifteen per cent of its range,
and what it produces is smooth low noise rather than ranges. A rain shadow needs
a barrier and there is nothing here tall enough or steep enough to be one.

That is also, separately, the answer to why the world looks bland from the air:
there is nothing to look at above 1.4 km.

### Why the terrain has no mountains

`ElevationTerms` exposes the pieces the height function multiplies, and the
survey over the land surface says which one collapses:

```
height terms over land (mountains = ridges * land^1.4 * province):
  ridges     p50 0.000  p90 0.000  p99 0.183  max 0.440
  province   p50 0.332  p90 0.773  p99 1.000  max 1.000
  land       p50 0.111  p90 0.298  p99 0.494  max 0.581
  mountains  p50 0.000  p90 0.000  p99 0.002  max 0.056
```

**The mountain band is exactly zero on ninety per cent of the land**, and the
reason is one line in each of two files.

`LedgerNoise::ErodedRidged` ends with

```cpp
return Normalisation > 0.0 ? (Sum / Normalisation) * 2.0 - 1.0 : 0.0;
```

which remaps its accumulation to [-1,1]. That is right for a signed fBm. But a
ridged field with a continuity weight and erosion damping concentrates its
energy into a few ridges, so `Sum / Normalisation` averages far below 0.5 and
the remap puts most of the surface **negative**. `Elevation` then does

```cpp
const double Ridges = FMath::Max(0.0, LedgerNoise::ErodedRidged(...));
```

and the clamp deletes it. What survives is the top decile.

The arithmetic checks out against the measurement: the largest height fraction
the terms can produce is `0.581 * 0.26 + 0.056 * 0.64 = 0.187`, and
`0.187 x 9 km = 1.68 km` against a surveyed maximum of 1.37 km.

**This is not fixed here.** Changing the remap or the pivot reshapes every
landform on the planet and invalidates every committed reference capture, and
which of those it should be — a different pivot, an unsigned return, a
renormalisation against the field's own distribution — is a decision about how
the world should look. The diagnosis is the deliverable; the reshaping is not
mine to choose.

### And there are no cliffs either

The same survey, at the finest spacing the mesh resolves — a node edge of 305 m
over 64 quads, so 4.8 m. A slope finer than that is a slope no vertex has:

```
land slope at the mesh vertex spacing of 4.8 m, 19426 points:
  p50 4.2  p90 12.3  p99 25.7  max 57.0 degrees
  at or above 30 degrees: 0.42%   at or above 60: 0.000%
```

**The steepest ground on the planet is 57 degrees, and there is one of it.**
T054 asks for a sixty-degree face reading as rock with bedding, with scree at
its foot; neither the face nor the foot exists. That is the same root cause as
the missing rain shadow, measured a second way.

## Where this leaves T051

The climate model is written, tested and correct for everything that can be
tested against this terrain. Its acceptance cannot be met until the terrain has
ranges — which is the height function's business, not the climate's. Blocked on
that rather than closed, so the transect stays as the check that will start
passing when the terrain earns it.
