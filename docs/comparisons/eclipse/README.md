# An eclipse the ephemeris predicted

T075. The acceptance: an eclipse predicted by the ephemeris is observable from
the surface at the predicted time, and the ground goes dark.

The prediction comes first and is not a search for a good-looking frame. The
ephemeris is asked when; then somebody stands at the site and looks.

## Standing outside

`UnrealEditor.exe client/Ledger.uproject -game -eclipse`

```
occulting body   2
greatest cover   100.0%
predicted at     40887932 s from epoch

offset      solar altitude   covered   ground brightness
 -180 min          -22.9         0.0%     0.00
  -45 min           10.6        35.4%    28.50
  -15 min           17.6        84.1%    24.89
   +0 min           21.0       100.0%     0.00
  +15 min           24.4       100.0%     0.00
  +45 min           30.8        71.6%    38.87
 +180 min           50.9         0.0%    74.10
```

Ground brightness is the mean of the lower half of the frame, out of 255. Three
hours before, the sun has not risen — this eclipse happens shortly after dawn,
which is why one end of the sequence is night and the other is a bright morning.
At the predicted moment the star is 21° above the horizon and completely hidden,
and the ground reads **0.00 against 74.10 three hours later**.

**The light is not dimmed by an eclipse effect.** It is the star's intensity
times however much of its disc is still showing. There is no separate system
that could disagree with the ephemeris about when an eclipse is, because there
is no separate system.

Notice the middle of the sequence: at −15 min the sun is *higher* than at −45
min and the ground is *darker*, because 84% of the disc has gone. Nothing was
written to produce that; it falls out of multiplying one number by the other.

## The geometry

An eclipse is two circles on the sky overlapping, so coverage is the area they
share over the star's own area. `Ledger.Eclipse.CoverageMatchesTheDiscGeometry`
checks that lens formula against sampling — 80,000 points spread over the star's
disc in equal-area rings, each marked hidden if it falls inside another body's
disc, with no overlap formula anywhere in it — right through an eclipse rather
than only at its peak, because a formula can be right at totality and wrong on
the way in.

**Worst disagreement: 0.00034 of the disc.**

`Ledger.Eclipse.NothingIsEclipsedAtNight` covers the other easy mistake. A moon
passes between a site and the star once a month whatever that site is doing, and
half the time it is facing away; counting those would darken ground that is
already dark.

## The bug this task found

`NextEclipse` returned **first contact, not greatest coverage**. It bracketed
the peak at plus or minus one coarse step and refined inside that — converging
precisely on the wrong question. It reported 3.5% coverage at a moment when the
star was 8.5% covered ten minutes later and 19.4% covered half an hour later.

An eclipse runs for hours; the search found the edge of one and stopped. It now
walks forward a minute at a step for as long as anything is in front of the
star, keeps the deepest, and refines there.

## What the census says, and an assertion not made

From one fixed site, in two years: **2 eclipses, the deepest covering 28.2%.**

That is not a total eclipse, and it should not be. A fixed point on a planet
sees partial eclipses regularly and totality almost never — the shadow's dark
core is a track a few hundred kilometres wide crossing a globe forty thousand
around, so most of the planet is beside it every time. Earth's figure for one
spot is centuries.

An early version of the test asserted that some eclipse in two years would be
total. That is a false claim about orbital mechanics, and the natural way to
make it pass would have been to break the search until it agreed. What the test
asserts instead is cheap and true: at greatest coverage the moon subtends
**1.324** of the star, so the shadow has a dark core and totality exists
somewhere along its track. The world's own site found one — the sequence above
reaches 100%.

## What is missing

At totality the ground reads exactly 0.00. Real totality is not black: the
corona is still there, and light scatters in from the daylight outside the
shadow track, which is why photographs of totality look like deep twilight
rather than like night. This model has neither, because it has one number for
the star and multiplies it.

Both belong with the star's photometry in T076 and the atmosphere in M04. The
measurement is here so that work starts from a number rather than from an
impression.
