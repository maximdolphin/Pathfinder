# Axial tilt, and the year that comes out of it

T073. The acceptance: a high-latitude site has a measurably shorter day in
winter, and its snow line moves across a simulated year.

Both halves are consequences of one number — how far the rotation axis leans
from the orbit's normal. Nothing else in the model produces a season.

## Day length

`Ledger.Season.AHighLatitudeDayIsShorterInWinter`, on the generated planet:
tilt 17.18°, year 30,597,692 s, solar day 77,914 s.

```
latitude  0.0: summer 10.82 h, winter 10.82 h, difference -0.00 h
latitude 15.0: summer 11.41 h, winter 10.23 h, difference +1.18 h
latitude 40.0: summer 12.69 h, winter  8.95 h, difference +3.74 h
latitude 60.0: summer 14.86 h, winter  6.79 h, difference +8.08 h
latitude 75.0: summer 21.64 h, winter  0.00 h, difference +21.64 h
```

Latitude 75 is inside the polar circle for this tilt: 21.64 h of daylight at one
solstice, none at all at the other. The equator reads 10.82 h at both, which is
half of this planet's 21.6-hour day — a twelve-hour day is Earth's number, not a
law.

**The closed form is checked against counting.** `cos(H0) = -tan φ tan δ` is one
computation; sampling the solar altitude 20,000 times across the day and
counting the lit fraction is another, and they share nothing but the answer.
Worst disagreement over every latitude and both solstices: **0.0001 of a day**,
about 8 seconds.

## Insolation

```
insolation at +65: 0.2975 summer, 0.0217 winter
insolation at -65: 0.0217 summer, 0.2974 winter
insolation at the equator: 0.3032 and 0.3032
```

A fraction of what the same ground would get with the star overhead all day. The
hemispheres are exact mirrors, and the equator does not care which solstice it
is — both are the same distance from the star's path.

## What replaced the drawn curve

The climate field's seasonal term used to be:

```
SeasonalSwingC * sin(latitude) * sin(2 pi phase)
```

which has the right shape and no cause. **The tilt does not appear in it.** That
formula gives a planet with no axial tilt a full set of seasons, and gives
planets tilted 10° and 40° exactly the same ones — and no test written against
it could notice, because the quantity it was wrong about was not one of its
inputs.

It is now the difference between the insolation a latitude receives today and
its average over the whole year, normalised so that `SeasonalSwingC` still means
what it says: degrees between summer and winter at the pole of an *Earth-tilted*
planet. Against a fixed reference rather than the body's own tilt — dividing by
its own tilt divides out the season and reproduces the original bug.

`Ledger.Snow.APlanetWithNoTiltHasNoSeasons` holds both ends:

```
at 60 degrees in midsummer: +2.87 C with a 5 degree tilt, +22.27 C with 35
```

and an offset of exactly zero, at every latitude and every phase, when the tilt
is zero.

### Two things the swap changed, both of which were the old model's artefacts

**The annual mean is not the midpoint of the two solstices.** The first version
used that midpoint, and the pole caught it: there the star sits on the horizon
at an equinox and below it all winter, so insolation is zero at both, while the
midpoint reads half the summer value. An equinox came out 20 °C below its own
annual mean. The mean is now integrated over eight points of the year — eight
sines, against a climate that already marches forty steps upwind resampling the
height field for every point it evaluates.

**The offset is not antisymmetric in latitude, and the swing is.**
`sin(latitude)·sin(2π phase)` is exactly antisymmetric by construction, and
`Ledger.Snow.SeasonsAreOppositeInTheTwoHemispheres` asserted it. Sunlight is
not: a latitude's annual mean is not halfway between its solstices, so its
summer excess and winter deficit differ — at 10° they read +1.65 and −3.51. What
*is* exactly equal and opposite is the distance between the two solstices,
because `I(-φ, δ) = I(φ, -δ)` and it is the same year. The test now asserts that.

## The snow line

`Ledger.Snow.TheSnowLineMovesAcrossAYear`, at latitude 55, walking the phase in
twelfths:

```
lowest 0 m at phase 0.00, highest 1230 m at phase 0.25, range 1230 m
```

Down to sea level in the depth of winter, up past 1,200 m at the height of
summer, and back to where it started after one year. That is the acceptance.

## In the world

The season is no longer a switch. The world reads it from the orbit at begin
play:

```
sky:    t=0 s, day 77719 s, tilt 17.2 deg, sun facing -0.217 0.971 -0.105
season: phase 0.6903 of the year, declination -6.01 deg
```

The declination agrees with the sun's own direction — `asin(-0.105)` is −6.03°
— because both come from the same ephemeris rather than from two settings that
have to be kept in step.

`-season=` still overrides it, because the fixtures that photograph a summer and
a winter side by side have to be able to ask for one. It is what the season is
when nothing asks that changed.

Patch-disk `FormatVersion` is 9 and the tilt is now mixed into the content key:
the same phase on a differently tilted planet is a different climate, and
therefore different vertex colours and different scatter.
