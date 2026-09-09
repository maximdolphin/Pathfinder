# The sky light was at the centre of the planet

Not a task. A bug found while auditing what the shading model receives for
M2S, a fix for it, and an honest account of the fact that the fix changed
nothing measurable — because the thing that prompted the search turned out not
to be a defect at all.

## What prompted it

`out/near-field-standing.png`, the eye-height capture from T429, measured
**21.4% of its pixels at pure black (0,0,0)** in full daylight. Photographs of
sunlit ground do not do that. Something looked like it was receiving no light.

## What was actually wrong

`ULedgerWorldBuilder` spawns the sky light with `Placed<ASkyLight>(InWorld,
Params)`, whose default location is `FVector::ZeroVector`. On this project the
world origin is the **centre of the planet**. Logged, to be sure rather than to
assume:

```
lighting: sky light moved from V(0) to V(X=350622603.87, ...) (6376 km from the centre)
```

It had been sitting 6,371 km underground for as long as there has been a sky
light. `SLS_CapturedScene` with `bRealTimeCapture` renders its cubemap from the
light's own position, so it was capturing the inside of a planet.

And moving it was only half of it. `bRealTimeCapture` re-captures when the *sky*
changes — the sun moves, the atmosphere changes — not when the light moves. The
single `RecaptureSky()` at world build time stayed the answer forever. Both are
fixed: the light follows the viewer, and re-captures when it has moved more than
a kilometre from where it last captured.

## The fix changed nothing measurable

Stated plainly, because a fix that is reported without its measurement is a fix
nobody can check.

| | pure black | mean difference |
|---|---|---|
| before | 21.4% | — |
| sky light off entirely | 21.4% | 0.63 / 255 |
| light at the viewer | 21.4% | — |
| light at the viewer, re-captured | 21.4% | 0.63 / 255 |

Turning the sky light **completely off** changes 12% of pixels by an average of
two thirds of one 8-bit level. It was contributing almost nothing before the
fix, and almost nothing after it.

## Because the premise was wrong

The 21.4% was not a rendering fault. It was the site.

The same measurement across the scripted flight's own ground captures:

```
terrain-surface.png       0.0% pure black
terrain-sweep.png         0.0%
terrain-town.png          0.1%
terrain-coast.png         0.1%
terrain-orbit.png        20.8%   (mostly space)
terrain-entry.png        55.1%   (mostly space)
```

Every capture of lit ground is at or below a tenth of a per cent. The
near-field fixture had parked inside a dense stand of the placeholder cone
trees, backlit, and those trees and their cast shadows fill a fifth of the
frame. The black was opaque black props, not unlit terrain — and in a frame
that is one fifth opaque prop and four fifths direct-sunlit sand, a sky light's
marginal contribution genuinely is under one level. The test that looked like
proof of a broken sky light was measuring a forest.

An earlier test in the same search was worse than useless: `showflag.DirectLighting 0`
rendered the world as a pure black silhouette, which looked like conclusive
proof that nothing received ambient. That show flag disables sky light diffuse
as well, so a black result was the expected result and proved nothing.

## Kept anyway

The change stays. A real-time capture sky light at the core of a planet,
captured once at startup, is wrong on its own terms and will matter the moment
anything depends on sky fill — dawn, dusk, overcast, or a scene not lit by a
sun directly overhead. It is about twenty lines and it is now correct.

It is not, however, why the ground looked bad. That was 4.77 m triangles
(T429), and it is now placeholder props (T434).

## Files

| | |
|---|---|
| `client/Source/LedgerClient/Private/LedgerWorld.cpp` | `KeepSkyWithViewer`, and the tick that calls it |
| `client/Source/LedgerClient/Public/LedgerWorld.h` | the builder became tickable for this one thing |
