# The night was black for three reasons

T077 computed eight hundred stars in M03 and **nothing ever drew them**. Every
night frame in this project came back black, and it was chased as a lighting
bug, a clock bug, a subsystem-ordering bug and a cloud bug in turn. It was three
things, and none of them were the ephemeris.

## One: the exposure floor was a floor

```
AutoExposureMinBrightness = -4.0f   ->   -8.0f
```

−4 EV100 is a landscape under a bright moon. A floor there is a floor **below
which nothing gets any brighter**, so a starlit scene simply clamped to black.

This is worth contrasting with the ceiling right beside it. That was raised from
17 to 19 in M2S and **changed not one pixel**, which is how the real cause of the
blown daylight frames was found — a bound that is not being hit cannot be the
thing that is wrong. This one was being hit by every night in the project.

## Two: nothing rendered the star field

`LedgerStarField` had a catalogue, a distance modulus, an apparent magnitude and
a naked-eye cut, all tested since T077. There was no code anywhere in the client
that turned any of it into pixels.

It is now an instanced mesh on the same shell the moons hang on:

```
stars: 5544 of 150000 catalogue entries are naked-eye, brightest magnitude -3.02
```

- **The catalogue had to grow.** Three thousand stars in four hundred parsecs
  gave *thirty-seven* naked-eye — one every three hundred square degrees, so a
  twenty-degree frame contained none and the sky looked as empty as before. The
  lever is the volume rather than the count: brightness is the distance modulus,
  so pulling the shell in to 150 pc makes the same stars far brighter. A hundred
  and fifty thousand gives 5,544, which is the order the real sky has.
- **Colour is temperature**, carried as per-instance custom data — an instanced
  mesh does not have a vertex colour per instance, whatever the name suggests,
  and eight hundred stars cannot be eight hundred materials.
- **Size is not honest and says so.** A star is a point source and would be
  sub-pixel at any true size. What stays honest is the ordering: a brighter star
  is drawn brighter and bigger on the magnitude scale the eye uses, so the
  constellations keep the shape the catalogue gave them.
- **The sky turns because the ground does.** The catalogue is inertial and the
  terrain is the body frame laid onto world space, so the whole field is hung on
  one holder carrying the body's orientation. One transform a frame, not 5,544.

## Three: the frames were photographs of an adaptation

Widening the range to −8..+19 made it **twenty-seven stops to travel**, and
auto-exposure moves at about 1.2 stops a second. The passage fixture settled for
1.5 seconds between frames, so the descending frame came back pure white — the
middle of the journey from a starlit night to daylight.

Settle is now 14 seconds a step. **This is the third time this project has
photographed an adaptation and called it something else**, and the pattern is
always the same: the number that changed was not the number at fault.

## What it shows

`UnrealEditor.exe client/Ledger.uproject -game -passage`

- **rise** — the moon on the horizon under a field of stars, the ground in
  twilight.
- **transit** — the moon at 79°, a gibbous limb, four stars around it.
- **set** — a pale moon low in a brightening sky over snow, the last stars out.

All three at times the ephemeris chose before anything was drawn, and the
renderer puts the moon within 0.000 arcminutes of where it predicted.

## One thing ruled out on the way

The first suspicion for the white frame was the real-time sky light capturing
5,544 emissive spheres and inferring an enormous ambient from them. Excluding
the stars from the capture changed the frame by 0.1% of its file size, so it was
not that — though the exclusion is kept, because starlight is about a millilux
and is not something a cubemap capture should be inferring.
