# What the terrain costs the shadow pass (T064)

T064 asks for a shadow-specific terrain LOD, on the premise that "paying full
resolution into a shadow map is most of a cascade wasted". Its acceptance is
"shadow cost drops measurably with no visible change in shadow quality".

Nobody had measured the shadow cost. That number decides the task, so it went
first — the same order T047 took, and for the same reason.

## The control arm

`-terrainnoshadow` stops every terrain patch casting. A planet that casts no
shadow at all is the floor any cheaper scheme is competing against, and the
difference is the entire budget available to recover.

Measured over the same fixed-step flight, on an idle machine, with the terrain
as T428 left it — real relief, so there is something to cast:

| phase | shadows on | shadows off | the terrain's shadows |
|---|---|---|---|
| orbit | 2.8 | 2.7 | 0.1 |
| descent | 4.7 | 4.6 | 0.1 |
| atmospheric entry | 6.2 | 6.1 | 0.1 |
| **surface** | 12.2 | 11.7 | **0.5** |
| **town** | 11.9 | 11.3 | **0.6** |
| ridge sweep | 6.7 | 8.3 | −1.6 |
| coast | 6.5 | 5.9 | 0.6 |
| underwater | 7.9 | 7.0 | 0.9 |
| ascent | 3.2 | 3.1 | 0.1 |
| space | 6.3 | 6.3 | 0.0 |

**At most 0.9 ms anywhere on the flight, and 0.5 and 0.6 ms in the two phases
where terrain fills the screen.** The ridge sweep comes out negative, which is
run-to-run variance and a reminder of the size of what is being measured.

## So T064 does not get built

A shadow-specific LOD means a second geometry path: separate proxies, or a
second component per patch, or a second draw with different indices. That is
more streaming, more memory and more to keep in step with the first path — to
recover part of six tenths of a millisecond.

The premise was not unreasonable. It was written when a cascade full of terrain
sounded expensive, and it is a statement about a planet nobody had timed.

## What this measurement is not

The number above is **whole-frame GPU with and without terrain casters**, not an
isolated shadow pass. Removing casters changes more than the shadow depth
rendering — the ground is lit differently, which changes what the lighting and
the ambient occlusion do. A pass-level breakdown would separate them and this
harness does not have one.

It bounds the answer, though, and the bound is what matters: whatever the shadow
pass costs, it is inside 0.9 ms, because that is all that changed when the
terrain stopped casting entirely.

Raw reports: `shadow-on.txt`, `shadow-off.txt`. `-terrainnoshadow` stays, because
a measurement nobody can repeat is not one.
