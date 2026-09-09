# Cliff and scree by slope (T054) — two of three

> A 60-degree face reads as rock with bedding, and there is debris at its foot
> that was not placed by hand.

This was blocked until tonight because the planet's steepest ground anywhere was
57°, with 0.42% of land above 30 and none at all above 60. T428 gave it relief:
62.1° in the whole-planet survey, 1.08% above 30, 0.015% above 60.

## Scree, at the angle of repose

Loose rock will not stand steeper than about 34°; above that it slides, and what
is left is the face it slid off. So scree is not a third thing decided by hand —
it is the band between ground too steep to hold soil and rock too steep to hold
anything, and the debris under a face is derived from the same number that makes
the face bare.

A triangle in slope: rising from where soil lets go, falling to where rock takes
over, peaking at cos 34°. `gravel_ground_vi0maebg`, no biome tint — broken rock
is not ground and does not take a climate's colour.

`scree-at-the-foot.png` is the flight's town phase. The settlement sits in a
valley at the foot of a mountain, and there is a grey-green debris band along
the base of the slope with sand on the flat below it and snow above. Nothing
placed it there; it is where the ground passes through the angle of repose.

`ridge.png` is the same treatment on the ridge phase's peak.

## Bedding — in, but not shown

Strata are horizontal, so they are a function of altitude and of nothing else —
which on a sphere means distance from the centre, not world Z. Sampling the rock
scan a second time at a flattened scale would cost nine more texture reads for a
band pattern; modulating what is already sampled by a function of altitude costs
none. Two frequencies, forty metres and nine, so the beds group into courses
instead of reading as corduroy, and only where the rock is bare — bedding
through a meadow is a bug.

**No capture in this directory demonstrates it.** The amplitudes are 0.10 and
0.05 on albedo, chosen to be subtle rather than photogenic, and at any distance
that survives the material's 600 m detail fade they are invisible. That is an
honest gap rather than a defect, and tuning the numbers up to make a screenshot
would be tuning for the screenshot.

## The fixture, and four things it got wrong

`-cliffsite` searches the planet for a face worth photographing. It took five
attempts to make it find one, and every failure was the fixture rather than the
terrain:

1. **It maximised the gradient.** A gradient is scale-free, so the steepest
   thing on a planet is a kerb: it found an 80.8° step four metres above the sea
   and photographed a coastal terrace. The score is now the drop over the two
   hundred metres below, with the angle as a gate.
2. **The face was in shadow.** The lit test asked whether the *point* was on the
   day side. A steep face turned away from the sun is dark at noon, and the
   answer came back as a black silhouette against a blue sky — the fourth
   night-side capture misread in one session. It now tests the surface normal.
3. **The face was under snow.** A lit 59° face at 1,900 m and sixty north is an
   ice cap, where the biome tint is 0.85 and nothing shows through it. It now
   requires the latitude band minus the lapse rate to be above 6 °C.
4. **The framing was past the detail fade.** 530 m back from a 295 m face
   photographs the material fading to neutral. Framings are multiples of the
   drop now, and the exposure is no longer pinned, because a slope facing the
   sun head-on clips to white against a pinned one.

What it finds now: **57.5° at 1,900 m, falling 295 m over the two hundred metres
below it**, on warm sunlit ground. The close-up is still the weakest evidence
here — the steepest *point* is a small feature and the shot frames the hillside
around it — which is why the flight's own captures carry this write-up instead.

Raw report: `cliff.txt`.
