# Wind, heard

T102. **Sound is a property of the air, not of the weather.**

Run it: `UnrealEditor.exe client/Ledger.uproject -game -windheard -RenderOffScreen`

## The one quantity

The same 30 m/s is a gale at sea level, a whisper on a thin-aired world and
nothing at all above the atmosphere, because what arrives at an ear is the
momentum flux the air is carrying:

```
q = ½ ρ v²
```

So there is no "is the player in space" flag to fall out of step with the
ephemeris, and no altitude fade tuned by hand. `LedgerAir::DensityAt` gives ρ
where the listener is, `ULedgerWind::SpeedAt` gives v at the same point — the
same subsystem the flight model and the vegetation read, which is T093's whole
point — and the level is what the two of them make.

```
  altitude m   rho kg/m3   wind m/s     q Pa    level   cutoff Hz    rms
           2     1.24058        7.5     34.78    0.058         874  0.033
         801     1.12280       19.7    216.84    0.361        1969  0.218
        4005     0.75264       21.5    174.13    0.290        2136  0.179
        8010     0.45650       21.5    105.61    0.176        2136  0.106
       16019     0.16794       21.5     38.85    0.065        2136  0.039
       32038     0.02273       21.5      5.26    0.009        2136  0.005
       64077     0.00042       21.5      0.10    0.000        2136  0.000
       96115     0.00001       21.5      0.00    0.000        2136  0.000
      144173     0.00000       21.5      0.00    0.000        2136  0.000

level / q over 7 rungs varies by 2.22e-16 -- proportional
above the top of the air the level is exactly zero
```

Both halves of the acceptance are in those last two lines, and both are
measured rather than asserted. `rms` is what the generator actually put out, not
what it was asked for, so "silent in vacuum" is a statement about the audio.

**The loudest rung is not the lowest one.** At two metres the wind is 7.5 m/s
and at eight hundred it is 19.7, because T093's log profile has the ground
dragging on the air — so the noise climbs for the first kilometre and only then
begins to fall with the density. Nothing had to be written for that to happen;
it is what q does when both of its terms vary.

## Vacuum is a place, not a limit

`DensityAt` returns exactly zero at and above the top of the air, which the
profile puts at twelve scale heights — a millionth of the surface density. An
exponential that merely gets small leaves "silent above the atmosphere" a matter
of opinion; a profile that ends makes it a fact, and the test asserts the
equality rather than a tolerance:

```
at and above the top there is nothing at all
a hundred metres a second above the air is zero pressure
an airless world is vacuum at the ground
```

## Generated, not played

Wind has no loop point. The voice is a `USynthComponent` producing white noise
through a one-pole low pass, which is the discretisation of a first-order lag
and about as much filter as a turbulent boundary layer's spectrum deserves.

The cutoff follows the speed — 200 Hz in still air, 3 kHz in a gale — because
the energy-containing eddies get smaller as the flow quickens. That is the
difference between a moan around a building and a hiss past an ear, and it is
the reason the table reports a cutoff at all: it is a second, independent thing
the wind is doing to the sound.

The pole's attenuation is divided back out, so the level means what it says
rather than depending on where the cutoff happens to be.

## Where the numbers come from

| | |
|---|---|
| full scale | 600 Pa — about 32 m/s of sea-level air, past which louder conveys nothing |
| silence | 0.02 Pa — a metre a second at sea level, a still day |
| fade | a twentieth of a second across the whole range, on the audio thread |

The level is linear in **pressure**, not in speed. Squaring the speed here
instead would give the same curve on one world and the wrong one on every other,
which is the mistake the acceptance is written to catch.

## What this does not do yet

Precipitation on surfaces and thunder delayed by distance are the other two
clauses of T102's description; neither has anything to make a sound about until
T095 lands. Wind noise is the clause the acceptance names, and it is what is
measured here.
