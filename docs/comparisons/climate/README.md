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

## Where this leaves T051

The climate model is written, tested and correct for everything that can be
tested against this terrain. Its acceptance cannot be met until the terrain has
ranges — which is the height function's business, not the climate's. Blocked on
that rather than closed, so the transect stays as the check that will start
passing when the terrain earns it.
