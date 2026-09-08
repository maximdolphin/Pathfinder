# ADR-0002 — Terrain: build vs buy

**Status:** Accepted — **build**. See the resolution at the end.
**Date:** 2026-09-08
**Resolves:** design §15.1 (the highest-value unresolved decision in the plan)
**Related:** design §6.8, §14 R3

## Context

§15.1 asks whether to build the cube-sphere quadtree terrain or adopt a plugin,
and §6.8 says the answer must come from a spike that flies a continuous descent
from orbit to ground and **measures collision cook time under motion** — not
from an opinion.

The v1.1 amendment to §6.8 also carved this spike out of the pre-gate Unreal
freeze precisely so it could run early. This is that spike.

## What was built

A from-scratch implementation in `client/Source/LedgerClient`, roughly 700 lines:

- six cube-sphere roots, subdivided against a screen-space error metric
- horizon culling
- crack prevention by edge-index stitching, not skirts
- vertices generated relative to each node's own centre, so float precision is
  local and the same code works at 60 km or at planetary scale
- `UProceduralMeshComponent` pool with `bUseAsyncCooking`, collision cooked only
  near the camera and predictively ahead along the velocity vector
- `Ledger.Terrain.Stats`, and an automatic trace every five seconds

It renders a planet with continents and coastlines from orbit, and a horizon
with a shoreline from two kilometres up. The architecture works.

## What it measured

| | orbit (2.4 R) | 2 km altitude |
|---|---|---|
| visible leaf nodes | 496 | 1,886 |
| nodes carrying collision | 0 | 94 |
| builds still queued | 0 | 862 |
| worst frame build cost | 16.4 ms | 20.9 ms |
| worst collision cook request | 0.0 ms | 0.3 ms |

Two findings, and the second is the one that matters.

**Async collision cooking is not the bottleneck.** §6.8 predicted Chaos
heightfield cooking would be the hitch source. With `bUseAsyncCooking` and a
near-camera radius it costs a fraction of a millisecond per frame to request.
The design's fear was well-founded in general and unfounded here, because the
mitigation it prescribed works.

**Vertex generation on the game thread is the bottleneck.** A 33x33 patch costs
roughly 5–8 ms, almost all of it in height sampling and tangent calculation. At
a budget of three per frame the visible set at low altitude takes several
seconds to fill, and 862 nodes stay queued. Raising the budget trades holes for
hitches; that is not a fix, it is a choice of symptom.

The current settings — `MaxDepth 8`, a 96-pixel error threshold — are a
**ceiling, not a solution**. They keep the node count inside what the builder
can serve. The error metric asks for depth 10 near the ground, and at depth 10
the visible set passes eight thousand nodes and the terrain renders with holes.

## Decision

**Not yet taken.** The spike has produced the trace §6.8 asked for; the choice
between the two paths below needs one more measurement.

**Option A — build, moving height generation to the GPU.** §6.8 already
specifies "heightfield from a GPU compute pass over layered noise", and this
implementation does it on the CPU. That is the whole gap. A compute pass writing
to a render target, read back or sampled directly, removes the 5–8 ms and the
node budget stops mattering. Estimated: the six weeks §6.8 warns about.

**Option B — buy.** Evaluate a planetary terrain plugin against the same trace.
The v1.1 amendment is explicit that this is not a drop-in: Voxel Plugin is
SDF/voxel meshing, a *different* architecture that replaces this one, and
adopting it is lock-in rather than a shortcut.

**Recommended next step:** timebox one week to port height generation to a
compute shader and re-run this trace. If the worst-frame build cost drops below
2 ms, build. If it does not, buy — and accept the architectural lock-in with
open eyes.

## Addendum — atmosphere, and a budget that works

Sky Atmosphere, volumetric clouds and height fog were added afterwards. §6.8 is
right that this part is configuration rather than construction, but the
parameters do not survive being copied from Earth:

- **Scale height is the setting that decides whether you can see.** Earth's
  Rayleigh scale height is 13% of its atmosphere; that ratio on a 5 km shell
  puts nearly all the air in the bottom 650 m, and everything past a couple of
  kilometres washes to flat pale green. It reads as a broken material and is in
  fact a correctly-rendered soup. Half the shell restores ground visibility.
- **Scattering coefficient scales inversely with path length.** A twelfth of
  Earth's atmosphere needs several times Earth's coefficient for the sky to be
  blue from the ground, not less.
- **Exposure clamps must be wide.** Clamped to 0.25–4.0 the ground could not be
  exposed down; left unclamped the auto-exposure hunts empty space and blows the
  planet to white. 0.03–8.0 with a −1 bias spans orbit and surface, and the
  ~2 s adaptation is itself the transition the eye reads.

The build budget was also changed from a node count to **6 ms per frame**. A
count cannot know that eight nodes cost 53 ms this frame and two cost 12 ms the
next; the count version produced exactly that spread. Worst frame fell from
53 ms to 12.9 ms. The queue did not shrink — 1,360 nodes still outstanding at
low altitude — which is the same finding stated more precisely: **the budget can
buy smooth frames or a filled horizon, not both, until generation moves off the
game thread.**

One structural flaw worth naming: an undersized component pool does not degrade
gracefully. Leaves with no component are simply not drawn, so a starved pool
punches black holes through the planet rather than showing coarser ground. The
fix is to keep a parent's geometry until all four children exist — a restructure
of the split path, not a constant.

## Consequences

**Good.** The spike is cheap to keep either way: the LOD, horizon culling,
stitching and collision scheduling are independent of where the heights come
from, and Option A is a change to one function.

**Bad.** The terrain currently on `main` is a working spike, not a shippable
feature. It has no material beyond a debug vertex-colour shader, no atmosphere,
no streaming of anything but the mesh, and its LOD ceiling is set by a constant
rather than by the error metric. Nobody should mistake the screenshots for a
milestone.

**Risk.** §14 R3 rated terrain collision hitching as the high risk. This spike
says the real risk is elsewhere — in per-node generation cost — which means the
mitigation the plan had budgeted for was aimed at the wrong target.


---

## Resolution — build, and the fix was threads rather than the GPU

The recommendation above was to spend a week porting height generation to a
compute shader and re-measure. That turned out to be the wrong first move.

**Generation moved to the task graph instead**, as free functions over copied
inputs that never touch the actor or the quadtree. The game thread now only
uploads finished vertex buffers.

| | before (game thread) | after (worker threads) |
|---|---|---|
| per-patch generation | 5–8 ms of frame time | 4–8 ms of *no* frame time |
| worst game-thread cost | 53 ms | 6.6 ms (upload only) |
| patches starved at low altitude | 862–15,383 | 0 |
| planet radius | 60 km | **6,371 km** |
| finest quad | 11 m | **4.8 m** |
| noise per vertex | 20 samples | 33 samples, 8-octave ridged |

A compute shader would still be faster, but it would also need a GPU→CPU
readback for every patch that carries collision, because Chaos cooks on the CPU.
Threads avoid that entirely and cost about eighty lines. **Revisit the GPU only
if patch throughput becomes the limit again**; it is not the limit now.

Two structural fixes landed with it, both of which had been showing up as
"the renderer is broken":

- **A parent keeps its geometry until all four children have theirs.** Releasing
  at split time meant a starved pool punched black holes through the planet.
  Now a starved budget costs detail, which is what a budget is supposed to cost.
- **Patch resolution matters more than the error threshold.** 65×65 patches
  cover four times the ground of 33×33 at the same screen error, so the same
  triangle count arrives in a quarter of the draw calls. At 33 the visible set
  was eighteen thousand components; at 65 it is about 2,500.

### What real scale changed, and what it fixed for free

Most of the atmosphere tuning recorded in the addendum above was chasing a
problem that did not exist. A 60 km planet with a 5 km atmosphere is 8% air by
radius where Earth is 1%: there was no self-consistent set of scattering
parameters, so every fix traded a black sky for a washed-out ground. At 6,371 km
the settings are simply Earth's — Rayleigh 8 km scale height, Mie 1.2 km, and
the ozone layer that gives the deep zenith and the violet twilight band.

Exponential height fog was **removed**. Its density is a function of absolute Z
against an infinite horizontal plane; on a sphere that plane cuts through the
planet, and from orbit it fills space itself. That was why the sky outside the
atmosphere came back navy instead of black. Sky Atmosphere is spherical and
already supplies aerial perspective.

**And the terrain's noise frequencies had to be rescaled with the planet.** A
frequency of `f` on the unit sphere has wavelength `2*pi*R/f`; at 6,371 km the
old highest band was a 440 km feature, so from a kilometre up the entire visible
world was one smooth gradient. The bands now run from continents to 70 m. This
was invisible at 60 km — the same numbers gave visible mountains there — which
is exactly why it survived so long.

### Still open

- No erosion model. Ridged multifractal gives ranges; it does not give drainage,
  and the absence reads as uniform crumple at the small end.
- Detail shading is a noise node with no distance fade, so it aliases. Real
  close-up fidelity needs a triplanar material with mip-mapped textures.
- The material is generated at runtime and is therefore editor-only. A packaged
  build needs a real asset.
- Collision is cooked but nothing has been dropped on it yet.
