# Fog with a top

T091. **The model is right and the renderer only shows it close up**, which is
two statements and they are made separately.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -fogwatch`
(and `-fogoff` for the control, which lays no fog and changes nothing else).

## Why fog has a level

Exponential height fog measures density against world Z on an infinite plane. On
a sphere that plane cuts through the ground on one side and out into space on the
other, which is why T089 switched it off.

What replaces it is not a better global fog. It is the observation that ground
fog is **local with a flat top**: overnight the ground radiates its heat away,
the air touching it cools, and the cold air runs downhill and pools. The top of
that pool is level. Filling a valley and leaving the ridge clear is then not a
feature — it is what a level surface does to a landscape.

## The night, from the ground up

```
hours dark   cold pool   ground cooled   fog
       1        28 m         5.7 K      no
       4        56 m        11.4 K      no
       7        74 m        15.1 K      yes, 74 m deep, visibility 243 m
      10        89 m        18.0 K      yes, 89 m deep, visibility 124 m
```

Fog appears in the second half of the night, which is when fog appears.

- **The pool deepens as sqrt(2 K t)** — turbulent diffusion into still air, with
  a nocturnal eddy diffusivity four orders of magnitude below a sunny
  afternoon's. Eighty-odd metres by dawn, which is the depth valley fog has.
- **The ground cools as 2 F sqrt(t) / (I sqrt(pi))** — the standard result for a
  semi-infinite solid losing a constant flux, with `I = sqrt(k rho c)` the
  thermal inertia.

That second formula is the correction that made the model real. The first
version drew the heat out of the cold pool's own capacity — a hundred kilogrammes
of air per square metre — and said a clear night cools by forty kelvin, fifteen
of them in the first hour. No night does that. **What limits a night's cooling is
the soil, not the air**, and once the ground is the reservoir the numbers land:

```
ten hours of darkness cools Venus by 0.00 K and Mars by 73.5 K
Earth: 18.0 K
```

Eighteen kelvin is a clear night. Nothing on Venus, because ninety bars of carbon
dioxide radiates almost everything back. Seventy-three on Mars, against a
measured overnight drop of about seventy — and it is Mars's *dust*, not its thin
air, that does it: dry dust has a thermal inertia around 350 against damp soil's
2000, and water is what makes the difference.

## What renders

```
9.30 hours of darkness cooled the ground by 14.4 K
fog forms, 86 m deep, visibility 363 m
valley floor 1293 m, filled to 1379 m, 21 of 36 cells
valley 1298 m, ridge 4454 m -- the ridge is 3096 m clear of the fog
```

Twenty-one of thirty-six cells got fog and fifteen did not, because their ground
is above the level. That is the acceptance's second clause, decided by
arithmetic rather than by placement.

- **From inside it** the fog is plainly there: the valley frame differs from its
  no-fog control by a third of its file size and shows a glowing bank at dawn.
- **From orbit** there is nothing to see, which is the third clause. A hundred
  metres of a six-thousand-kilometre body, drawn by components that have an edge.

## What does not render, and it is not the model

**From the ridge, twelve kilometres away, the frame is indistinguishable from the
no-fog control.** The pale field filling the valleys in that shot is the
atmosphere's own aerial perspective from T089, not this fog. It looks like the
acceptance and it is not, and the only reason that is known is that the control
was run.

Things ruled out:

- **A per-view volume budget.** Dropping from 144 one-kilometre cells to 36
  two-kilometre ones — well inside any plausible cap, and each large enough to
  subtend a real angle at that range — changed the distant frame by nothing.
- **Density.** The component's extinction is quoted per unit sphere rather than
  per metre, so it wants the optical depth across the pool (`tau = k d`) and not
  `k`. Handing it `k` made a fog nobody could photograph; the corrected value is
  clearly visible from inside and still invisible from twelve kilometres.

So `ULocalFogVolumeComponent` renders near the camera and not at valley-viewing
range. A bank that reads across a landscape probably wants the volumetric cloud
system with a low layer bound to a level, which is **T094**'s subject. T091 is
marked blocked on that half rather than done on the strength of a photograph of
something else.

## Two traps worth keeping

**A full disk renders black.** Several frames in this task came back black or
zero bytes and were chased as a lighting bug, a clock bug and a subsystem
ordering bug in turn. The drive had no space. Every screenshot since has been
checked for a non-zero size before being looked at.

**A buried camera renders black too.** The terrain query and the terrain the
streamer builds are not the same surface at every level of detail, and a camera
eight metres above a coarse estimate of a mountainside can be inside a fine one.
The valley camera is placed at the tenth percentile of the sampled ground rather
than at its minimum — the single lowest sample is the bottom of the deepest
crack, where that disagreement is largest, and it is not what anyone means by a
valley either.

## A sphere is all a local fog volume can be

Every version of the fog with its height term on produced the same frame from
the ridge: one opaque orange, the peaks included. Three controls, one each:

| arm | ridge frame |
|---|---|
| fog on (height falloff 120) | opaque orange, peaks gone |
| `-noclouds` | the same — it was not the cloud deck |
| sun directly behind the lookout | the same — it was not forward-scattered glare |
| `-fogoff` | a sea of cloud with snow peaks standing out of it |
| `-fogradialonly` (height term zeroed) | the same as fog off; the fog a faint haze |
| height falloff finite (≤ 20) | opaque orange again |

The engine's own shader says why. `LocalFogVolumeCommon.ush` unpacks each
volume as a translation, two basis vectors and **one `UniformScale`**: a local fog
volume can be turned but not squashed. The "flat lenses" — 3.2 km across and
86 m tall by their scale — were spheres 3.2 km in radius, and at twenty degrees
south with the lookout 3 km above the pool, the lookout was inside one. With
the radial term alone the sphere was a faint haze; with the height term,
whose density grows as `exp(falloff · (offset − z))` towards the bottom of the
sphere, it was solid. (At a falloff of 120 the bottom was also `e^120`, past
the largest float, which is a second fault, not the first.)

So the pool is now **one** sphere, turned to the local up, with the radial term
off and the height term's offset at the fill level: fog below the level, clear
air above it, and the ground that rises above the level — the ridge — out of
the fog on its own. The level sits at `z = -0.8` of the sphere rather than at
its centre, so that a falloff of 400 (an e-fold every ~35 m above the top)
leaves the bottom at `e^80` rather than past what a float can hold; the sphere
is sized so the circle where it meets the level covers the sampled square.
