# The ground gets a thickness

T430, partial. Parallax offset mapping is in and measured. Parallax *occlusion*
mapping — the thing the acceptance actually asks for — is not.

Run it: `-nearfield`, with `-noparallax` as the control arm.

## What was wrong

Every one of the 23 scans ships a height map. All three were sampled and used
only to decide which layer won a height blend. Nothing was ever displaced, so
the ground was a photograph of gravel printed on glass: a normal map relights a
flat surface but never moves it, so nothing slides against anything as the
camera does and there is no depth cue at all at the distance a person stands.

## Offset in world space, not in UV space

The ground is triplanar — three projections of the same position, blended by the
surface normal. Offsetting a UV would move one projection and leave the other
two where they were, and the seams between them would swim. Displacing the
*position* all three are derived from moves them together, by the same amount,
in the same direction, and the blend stays put.

```
ProbeHeight   = triplanar(slot 0 packed).b        // one extra sample, not four
Tangential    = EyeDir - N·(EyeDir·N)             // the part lying in the surface
Facing        = max(EyeDir·N, 0.30)               // floored, or grazing divides by nothing
Amount        = (ProbeHeight - 0.5) × ParallaxDepth × (1 - fade)
Position     -= (Tangential / Facing) × Amount
```

Centred on 0.5 so the mean surface stays where the mesh put it and only the
relief moves. Divided by how square-on the surface is, because that is how
parallax behaves in reality — largest at grazing — with a floor so a two-degree
view does not throw the sample across the texture. Faded out from 3 m to 15 m,
past which three centimetres is under a pixel and the sample it costs is not.

`ParallaxDepth` is 3 cm, which is about what a gravel scan's height map covers.
Past about five the offset outruns the sample it was measured from and the
surface smears.

## Measured

The claim is that this acts near the camera and fades with distance. On a ground
plane from eye height, the bottom of the frame is near and the top is far, so
difference-by-row is difference-by-distance. Same build, same frame, with and
without:

```
row band   mean abs difference / 255
  1/12  (far)      0.66
  4/12              0.83
  8/12              1.32
 10/12              2.32
 12/12  (near)      3.33
```

Monotonic, five times larger at the near edge than the far one. That is the
signature parallax should have and nothing else in the material has. It is also
small — three centimetres of displacement is a subtle thing, and the honest
report of a subtle thing is a small number.

## What it does not do, and the acceptance it does not meet

T430's acceptance is *gravel occluding gravel*. This does not do that, and the
difference is worth being exact about: **offset mapping moves the surface,
occlusion mapping hides part of it.** One step of offset makes the texture
parallax correctly under a moving camera. It cannot make a pebble hide the
pebble behind it, because that needs a ray marched against the height field per
pixel — a loop, not an offset.

The loop is the next rung and it is not free at a screenful of ground, so it is
measured before it is taken rather than after.

**It also does not fix the near-ground softness**, which was the reason for
reaching for parallax in the first place, and that is a useful negative result.
At a two-degree grazing angle the texture footprint ratio is about 30:1 and 16 —
the most any anisotropy setting recovers — is not enough; parallax on a blurred
sample produces blurred parallax. Displacing the *geometry* rather than the
sample position is what breaks the flat-plane assumption that causes it. That is
Nanite tessellation territory and a bigger decision than this task.

## Files

| | |
|---|---|
| `client/Source/LedgerMaterial/Private/LedgerTerrainMaterial.cpp` | the offset, and `-noparallax` |
