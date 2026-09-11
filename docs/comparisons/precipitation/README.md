# A low crosses a site, and it rains

T095. **Rain and snow are the same event at different temperatures.**

Run it: `UnrealEditor.exe client/Ledger.uproject -game -front -RenderOffScreen`

## The acceptance, in one table

```
body            1 (Home)
site            -20.7 N, 127.8 E, 9.8 C
deepest anomaly -675 Pa
cloud           1237 to 5242 m

  moment        hours   anomaly Pa   falling      mm/h   drops
  before          -18            0   none        0.00       0
  arriving         -5         -622   rain        0.54     805
  rain             +0         -675   rain        0.63     938
  snow             +0         -675   snow        0.63     938
  after           +18         -335   rain        0.06      87

  altitude m   falling
           0   rain
        1000   rain
        1500   rain
        2000   snow
        3000   snow
        5000   snow
        5500   none

freezing level declared 1517.304 m, changeover found by bisection 1517.304 m, 4.77e-12 apart
the boundary is where the lapse rate puts it: yes
```

Three clauses, three kinds of evidence:

- **A front crosses a site.** Nothing at −18 h, rain building as the low
  arrives, heaviest at the peak, tailing off after. The moment is not chosen: the
  fixture scans twenty days of weather for when the pressure anomaly over the
  site is deepest.
- **Rain at low altitude, snow above the freezing level.** Two frames at the
  *same instant*, one at 25 m and one 900 m above the freezing level, so the
  difference between them is altitude and not weather.
- **The boundary sits where the lapse rate puts it.** Found by bisecting what
  the model answers, not by reading the freezing level off it.

## Why the boundary test can fail

The rain/snow decision walks the lapse rate down from the surface temperature
and asks whether the air there is below 273.15 K. The freezing level is solved
for separately, as the height at which it is exactly 273.15. Two routes to the
same place. A version that compared the altitude against the freezing level
would agree with itself for ever — T086 learned that the hard way with a
navigator test that agreed to zero arcseconds because both sides came from the
same function.

On a textbook Earth the same arithmetic gives **2,288 m against the 2,280 m**
in the book: 288 K over the environmental lapse rate, which is two-thirds of the
dry adiabatic one because condensation releases heat on the way up. The dry
rate would have put it at 1,500 m.

## The temperature is local, not planetary

The first run said it was snowing at the ground under every low on the planet,
because the profile's datum temperature is one number for the whole world. A
pole and an equator share an atmosphere and not a freezing level. The latitude
model already exists — it is the terrain's climate, which also decides where
snow lies — so the caller passes the local surface temperature in rather than
LedgerCore growing a second climate. This site is 9.8 °C, and its freezing level
is 1,517 m rather than the planet-average figure.

## Where the rain comes from

Air is made to rise where the pressure is low, and rising air condenses. The
rate comes from the **cell anomaly alone** — how far the lows push the pressure
below the background — rather than from the total pressure, because the
subtropical ridge is a high everywhere and is not the reason it rains in one
place and not another. That needed `LedgerWeather::CellAnomalyPascals`, split
out of `PressureAt` so there is one falloff written once.

A deep mid-latitude low, thirty hectopascals, gives steady frontal rain of about
5 mm/h. This site's worst in twenty days is 675 Pa, so it gets 0.63 — a light
rain, and the frames show a light rain. No convection is modelled, and a bigger
number standing in for a thunderstorm would be worse than none.

Worlds with no condensable water get no rain and no snow at any pressure. A
strong enough wind lifts **dust** instead: 17 m/s, near the Martian saltation
threshold.

## What is drawn

A box forty metres across that follows the camera, with up to nine thousand
particles wrapping around its faces. Each one moves at **the wind where it is,
from `ULedgerWind`, plus its own terminal velocity** — so rain slants and snow
drifts without either being a parameter someone set. That makes this T093's
particle consumer: it reads the same wind as the flight model, the grass and
the audio.

| | fall speed | drawn as |
|---|---|---|
| rain | 9 m/s | a streak, the fall speed through a 1/25 s exposure — 36 cm |
| snow | 1 m/s | a flake, three centimetres |
| dust | 0.05 m/s | brown, hanging |

The speed, not the colour, is what makes snow look like snow: the same mass over
ten times the area.

## Still open

The sky over the rain frame is clear, because the cloud deck is still the T094
problem — the volume material draws, but it does not yet shape into cloud with
sky between. Precipitation is falling out of a deck the renderer is not showing.

## The frames

| | |
|---|---|
| `front-0-before.png` | dry, eighteen hours ahead of the low |
| `front-1-arriving.png` | the first rain |
| `front-2-rain.png` | the peak, at the ground: streaks slanting with the wind over the town |
| `front-3-snow.png` | the same instant, 900 m above the freezing level: snow |
| `front-4-after.png` | the tail |
