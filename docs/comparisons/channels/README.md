# What the shading model actually receives

T433. Five channels, isolated and photographed, and one of them was wrong.

Run it: `-nearfield -noscatter -livematerials -channel=albedo|normal|roughness|ao|height`.
The overhead capture becomes that channel alone, emissive and unlit, with
exposure pinned — routed *after* every mix and fade, so it shows what the
shading model is handed rather than what the scan contains. That is the
question, and it is why this is a material switch rather than a texture viewer.

## The checklist

Overhead from 8 m, scatter off, exposure pinned.

| channel | mean (sRGB) | range | verdict |
|---|---|---|---|
| albedo | (144, 142, 123) | 95–246 | coloured, ~(0.27, 0.26, 0.20) linear — a sandy albedo |
| normal | (216, 228, 206) | 142–255 | **world space, and correct** — see below |
| roughness | (227, 228, 229) | 145–255 | grey, ~0.77 linear. Rough ground, and not gloss |
| ao | (235, 236, 238) | 146–255 | grey, ~0.83 linear. Mostly unoccluded, with crevices |
| height | (186, 187, 189) | 135–255 | grey, ~0.47 linear. Centred, as a height map should be |

The three scalars are grey to within 3 of 255, which is the check that matters
for them: a scalar that arrives coloured is a scalar being read from the wrong
channel of the packed map.

### The normal, which was being summed wrong

`FGraph::Triplanar` blends three projections by weighting and adding them. That
is correct for a scalar and for a colour, and wrong for a normal, because **each
projection's normal map is in its own tangent frame**: the X projection reads
the YZ plane, so its red channel points along world Y and its blue along world
X. Adding three of those together adds three vectors that are not in the same
space.

A planet makes it worse than it would be on a level. Triplanar weights come from
the surface normal, and on a sphere that is the radial direction — so all three
projections carry real weight nearly everywhere, and the incoherent sum is the
common case rather than the edge case.

`TriplanarNormalParameter` swizzles each projection into world space, blends,
and normalises. The material declares `bTangentSpaceNormal = false`, and the
distance fade now fades to the *vertex* normal rather than to +Z — in world
space, "flat" is the surface the mesh describes, and +Z is flat in exactly one
place on a sphere.

**Verified against something known.** The site's radial direction, from the
world builder's own log of where it put the sky light, is (0.55, 0.75, 0.37).
The normal channel decodes to (0.53, 0.76, 0.35). Ground that is mostly flat
should have a world normal pointing straight up away from the planet's centre,
and it does, to two decimal places on every component.

The change is worth 1.18 of 255 across the lit frame. Small, and real, and it
means every lit terrain pixel was previously shaded from a vector that was not
the surface.

## The measurement that lied, twice

Written down because the first two attempts at this table were both wrong and
both looked authoritative.

**Auto exposure.** The first reading was taken with the fixture's normal camera,
so every value went through auto exposure and a film tone curve before reaching
the file. It reported the terrain's normals as having a mean length of 0.4 and
pointing sideways — alarming, and a fact about the tone mapper rather than about
the normals. A debug view that lies is worse than no debug view.

**`AEM_Manual`.** The second attempt pinned exposure with the manual method,
which does not mean "no exposure adjustment" — it derives exposure from the
physical camera's aperture, shutter and ISO, whose defaults rendered every
channel between 0 and 7 of 255. Every check passed, on five black images.

Pinning exposure is `AutoExposureMinBrightness == AutoExposureMaxBrightness == 1`,
with the tone curve and gamut expansion off: the histogram method still runs and
is clamped to one value, so 1.0 in is 1.0 out.

The fix to the normals was made before any of this measured correctly, and it
was made on the code rather than on the picture — three tangent frames summed is
wrong whatever a photograph says. The pictures now agree, which is the check,
not the reason.

## Files

| | |
|---|---|
| `client/Source/LedgerMaterial/Private/LedgerMaterialGraph.h` | `TriplanarNormalParameter`, and why it exists |
| `client/Source/LedgerMaterial/Private/LedgerTerrainMaterial.cpp` | `-channel=`, and the world-space declaration |
| `client/Source/LedgerHarness/Private/LedgerNearField.cpp` | the pinned exposure |
