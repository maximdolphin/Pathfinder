# The star as a physical light

T076. The acceptance: illuminance at each planet matches the inverse-square
prediction, and auto-exposure handles the range without clipping.

## Nothing new is stored

The star already had a mass and a radius. Everything else follows:

- **Luminosity** from the mass, by the main-sequence mass-luminosity relation.
  Piecewise, because one exponent does not cover the sequence — a star half the
  Sun's mass is about a twelfth as bright under an exponent of 3.5 and about a
  fifth under 2.3, and the second is what matches observation down there.
- **Temperature** from the luminosity and the radius, Stefan-Boltzmann solved
  backwards. This is where the light's colour comes from, so a small red star
  and a large blue one light their planets differently without anybody picking a
  tint.
- **Illuminance** from the luminosity and the distance.

A system read from disk therefore cannot carry a luminosity that disagrees with
the body it belongs to, because it does not carry one.

This system's star: **1.861e30 kg (0.936 solar), 2.936e26 W (0.767 solar),
5781 K.**

## Inverse square

```
     1e+10 m ->    2.173e+07 lux,  E*d^2 = 2.17263e+27
     3e+10 m ->    2.414e+06 lux,  E*d^2 = 2.17263e+27
     1e+11 m ->    2.173e+05 lux,  E*d^2 = 2.17263e+27
   1.4e+11 m ->    1.108e+05 lux,  E*d^2 = 2.17263e+27
     5e+11 m ->         8691 lux,  E*d^2 = 2.17263e+27
     4e+12 m ->        135.8 lux,  E*d^2 = 2.17263e+27
     1e+13 m ->        21.73 lux,  E*d^2 = 2.17263e+27
```

Three decades of distance, and **the drift in E·d² is exactly zero**. Twice as
far is 0.250000 as bright.

That on its own is a weak test, because the product is constant by construction.
So `Ledger.Star.TheDiscAndTheDistanceAgree` computes the same illuminance a
second way — surface flux times the solid angle the disc subtends, `σT⁴sin²α` —
which goes through the star's **apparent size** and never mentions distance at
all. That is the quantity T074 draws with. Worst relative difference between the
two roads: **3.5e-16**.

And the model is checked against a number somebody else measured. One solar mass
and one solar radius comes out at **5772 K**, which is the Sun's figure, and its
illuminance at one astronomical unit at **126,588 lux** against a real ~127,000
above the atmosphere.

## The exposure, and two wrong diagnoses

The sun used to be the number **11**. It is now **101,367 lux** at this planet.
Four orders of magnitude, and everything downstream that had quietly assumed a
scale had to be found.

The exposure bounds were `0.03` to `8.0` — raw luminance multipliers. They are
now `-4` to `+19` **EV100**, which required turning on
`r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange`. Stops rather than
multipliers, so the bounds survive the next change to what the light is worth
instead of having to be rescaled by hand.

Getting there took two wrong answers:

- **"The bright bound is too low."** Daylight frames were 48–62% pure white, so
  the ceiling was raised from 17 EV to 19. **It changed not one pixel** — which
  is how the real cause was found. A bound that is not being hit cannot be the
  thing that is wrong.
- **"It just needs longer."** Partly, but the first attempt lengthened the wrong
  fixture. The day sweep was given twelve seconds a step and barely improved,
  because it was already nearly settled; the eclipse watch, which walks from a
  total eclipse to full daylight in 1.5-second steps, was the one still climbing
  when the shutter went.

The actual fault was neither the range nor the light: it was photographing an
adaptation. A fixture that jumps the clock by hours between frames has to pay
for its own discontinuity, because nobody moves through a day in seven jumps.

## The result

The eclipse sequence at the world's own site, ground only, after the fix:

| offset | covered | lux | ground mean | % black | % white |
|-------:|--------:|----:|------------:|--------:|--------:|
| −180 min | 0.0% | 101,367 | 0.00 | 100.00 | **0.00** |
| −45 min | 35.4% | 65,483 | 65.27 | 24.92 | **0.00** |
| −15 min | 84.1% | 16,117 | 66.21 | 25.66 | **0.00** |
| 0 min | 100.0% | 0 | 0.00 | 100.00 | **0.00** |
| +15 min | 100.0% | 0 | 0.00 | 100.00 | **0.00** |
| +45 min | 71.6% | 28,783 | 67.40 | 26.09 | **0.00** |
| +180 min | 0.0% | 101,367 | 71.54 | 21.48 | **0.00** |

**Nothing clips white anywhere**, across a range from 101,367 lux to nothing at
all. The frames that are entirely black are night and totality, which are meant
to be. The daylight ground sits at 65–72 out of 255 with about a quarter of it
in shadow, which is a landscape rather than a wash.

## Known simplification

Luminous efficacy is a constant 93 lm/W. It is not: a cool red star puts most of
its output where an eye sees nothing and a hot blue one loses it the other side,
so this makes a red dwarf's planets brighter than they should be. It is right
for the star this system has, and it is the difference between a lux and a watt
rather than between bright and dim. Correcting it means integrating a Planck
curve against the photopic response, which is a task and not a line.
