# The same constellations from two planets, different ones from two systems

T077. The acceptance names both halves, and they are the same fact seen from two
distances: a solar system is nothing against a parsec, and a parsec is not.

## A catalogue, not a skybox

Stars have positions in three dimensions and absolute magnitudes. A skybox is
the same from everywhere, so two systems light years apart would share a sky and
travelling between them would be a loading screen dressed as a journey. Real
positions give the opposite for free — nothing had to be written to make
constellations hold within a system or come apart between them; both fall out of
subtracting an observer's position from a star's.

## Measured

A constellation is not a list of stars, it is a set of **angles** between them.
So the test takes the brightest twelve, measures every angle between them from
one place, measures the same angles from another, and asks how far they moved.

```
naked-eye stars from the inner planet: 839, brightest magnitude -0.42 at 77.1 pc

across 41 au inside one system, the worst angle moved by     1.4334 arcseconds
across 12 parsecs, angles moved by                     0.17 to 20.91 degrees
the brightest stars themselves move by up to                   18.6 degrees

the sky is 52,527 times more rigid across a system than between two
```

One arcsecond is a fortieth of what an eye resolves. Twenty degrees is forty
full moons. **The same sky, and then a different one.**

The bound on the second half was five degrees at first, on no reasoning, and the
first measurement came back at 3.47 — so the bound was wrong, not the code. It
is now one degree, which is twice the width of the full moon and a thing that
can be pointed at, and the measured figure is quoted beside it either way.

## The bug the chart found and the tests did not

`out/t077-star-chart.png` draws all three skies. The first version of it came
back with all 819 naked-eye stars on **a single one-dimensional arc**.

The catalogue's hash was FNV-1a over three small integers, which leaves
consecutive streams correlated: the stream choosing the cosine of the latitude
and the stream choosing the longitude moved together, so every star landed on a
curve. SplitMix64's finalizer instead — three multiplies and three xorshifts,
designed for exactly this.

**Every test passed on the degenerate catalogue.** They measured angles between
stars, and a constellation drawn on a wire holds its shape across a solar system
and comes apart between two exactly as one drawn on a sphere does. The tests were
not weak about the thing they tested; they simply never asked whether the sky was
full.

`Ledger.StarField.TheStarsCoverTheWholeSky` now asks it, in equal-area cells —
bands in the sine of the latitude, not the latitude, or the polar cells would be
small and read as empty for the wrong reason:

```
839 naked-eye stars over 128 equal-area cells:
  mean 6.6, fewest 1, most 11, empty 0
```

That is the assertion that was missing rather than a new kind of test, and it is
worth naming what found it: a picture. Three tests, all green, all correct about
their own claims, and none of them able to see that the sky was a line.

## The catalogue's shape

Stars are spread uniformly through the **volume**, which is a cube root, not
uniformly in radius — sampling the radius evenly would pile the catalogue near
the observer and give a sky of a few dozen enormous stars.

Absolute magnitudes are weighted heavily towards the faint end, because a real
luminosity function is: there are far more red dwarfs than giants, and a
catalogue drawn flat would be a sky of beacons. Under 1% of the catalogue is
intrinsically bright. Temperature correlates with brightness rather than being
drawn separately, so the bright ones come out blue and the dim ones red without
a second decision that could disagree.

## Not yet in the sky

This is the catalogue and the projection. Putting it on screen has the problem
T074 already measured: the atmosphere is dense enough to erase a body on a
500 km sky shell, and stars are very much fainter than a moon. Rendering waits
for the atmosphere calibration in M04, and the chart is what the claim rests on
until then.
