# The ground lost its detail three hundred metres out

T432, the distance half. The multi-scale bands already existed; what was wrong
was when each of them was switched off.

## What was there

The terrain material already samples the ground at three scales — the scan's own
2 m tile, a 90 m macro, and a 1.2 km macro — each mean-normalised so it
modulates rather than tints. That part was right and is untouched.

Each was faded out with distance, and both fades were wrong in the same
direction:

```cpp
Fade = saturate(Depth / 60000)   // 600 cm-metres: a ramp starting at the camera
FadedVariation = lerp(Variation, 1.0, Fade)   // the 2 m detail
FadedMacro     = lerp(Macro,     1.0, Fade)   // the 90 m macro, same curve
```

A ramp that begins at the camera reaches a quarter at 150 m and a half at
300 m. So ground three hundred metres away was drawn with half its detail
already deleted — and the 90 m macro, which exists precisely to carry variation
once the fine detail is gone, was deleted alongside it on the same curve. The
mid-ground collapsed to a flat tint, and that is what the wash in every
eye-height capture was.

## The criterion, which is arithmetic

A pattern stops being detail when its tile is about two pixels across.

A tile of size `S` at distance `D` subtends `S/D` radians. On a 1920 px viewport
at 90° horizontal that is `(S/D) × 960` pixels. Two pixels happens at:

| tile | two pixels at |
|---|---|
| 2 m | **960 m** |
| 90 m | **43 km** |
| 1.2 km | 576 km |

So the 2 m band should hold to several hundred metres, not fade from zero; and
the 90 m band is never near its mip range at any distance this planet draws
ground at.

## What it is now

```cpp
Fade      = saturate((Depth -   400 m) /  800 m)   // 2 m detail: held to 400 m, gone by 1.2 km
MacroFade = saturate((Depth - 20 km)   / 40 km)    // 90 m macro: held to 20 km
```

The 1.2 km macro still never fades, for the reason it always did: at ten
kilometres its features are hundreds of pixels across, which is the whole point
of it.

## Measured

`out/near-field-standing.png`, before and after, from the same site.

Before, the mid-ground is one flat olive tint from about a hundred metres out to
the base of the hill. After, ground texture is legible to the hillside and the
hill itself carries visible variation rather than a single colour. The near
ground is unchanged, which is the check that this did not simply brighten
everything: the fade was already zero there and still is.

The change is in the material only. No geometry, no new samples, no new
textures — the same three bands, switched off at the distances they actually
stop resolving at.

## Not fixed by this

The near ground is still smeared at grazing angles. That is a different problem
— anisotropic filtering and the absence of any parallax — and it is T430.

## Files

| | |
|---|---|
| `client/Source/LedgerMaterial/Private/LedgerTerrainMaterial.cpp` | `Fade` and `MacroFade` |
