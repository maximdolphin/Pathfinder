# A proper Unreal world

ADR-0006, finished. **Everything that can be an asset is one, and everything
the world is made of is an actor in the level.**

The question that started it was the right one to ask: *is the render still
runtime, and not a proper world?* Checked rather than remembered, the answer
was "mostly not, and some of it was my doing":

| | before | now |
|---|---|---|
| terrain, water, underwater, flat materials | saved assets | saved assets |
| star material (T077, reused by T095's rain) | built in code every launch | `M_Star`, saved |
| three-deck cloud material (T094) | built in code every launch | `M_Clouds`, saved |
| wind parameter collection (T093) | **never existed** — vegetation read no wind | `MPC_LedgerWind`, saved |
| tree canopies | flat material, no wind | `M_Foliage`, leaning with the wind |
| town buildings and pad | a procedural mesh rebuilt each launch | `SM_Building`, `SM_Pad`, instanced |
| planet, atmosphere, town | spawned into the map by code | **placed in `/Game/Maps/Ledger`** |
| a crossing to another body | destroyed three actors, spawned three more | reconfigures the same actors in place |

The planet's *terrain* is still streamed at runtime, on purpose. A 6,371 km
planet flown from orbit to the ground cannot be an asset; the ADR's second tier
exists for exactly that and nothing else. The actor that streams it is now a
thing in the level like any other.

## The level, read back from disk

```
Level: /Game/Maps/Ledger
In the saved level, read back from disk:
  Atmosphere (LedgerAtmosphere)
  Planet (LedgerPlanet)
  Settlement (LedgerSettlement)
  SkyLight (SkyLight)
  Sun (DirectionalLight)
  WorldPostProcess (PostProcessVolume)
VERDICT: PASS
```

**Read back, because the first version lied.** `tools/make_level.py` had only
ever run once, when there was no map, and claimed to be idempotent. Run again,
it failed three ways and reported PASS each time:

1. `does_asset_exist` answers False for a map — the same blind spot
   `verify_assets.py` already records about World assets — so the old level
   was never deleted, and `new_level` refused to overwrite it.
2. Deleting the file by hand left the editor's asset registry, scanned at
   startup, still certain it was there. `new_level` refused again, and for a
   few minutes the project had no level at all. It is tracked in git, which is
   the only reason that was a restore rather than a rebuild.
3. The verdict was computed from the list of actors the script had *asked* to
   spawn, including on runs where the save had been refused.

It now opens an existing map, removes only the actors it owns, places fresh
ones and saves; only a missing map is created. And the verdict comes from
loading the saved file again and counting what is in it.

## Materials: 8 of 8, and the one that did not compile

```
  MPC_LedgerWind /Game/Materials/MPC_LedgerWind
  M_Terrain      M_Water      M_Underwater   M_Flat
  M_Clouds       M_Star       M_Foliage
  8 of 8 saved
```

`M_Star` and `M_Clouds` were already in the bake's list. The bake had simply
not been run since they were added, so both were rebuilt in memory on every
launch — "star material built in memory" was in every log this week.

`M_Foliage` saved and then **failed to compile**, silently, into the engine's
default material:

```
(Node CollectionParameter) CollectionParameter has invalid parameter WindDirection
```

A collection node finds its parameter by GUID; the name is for people. The
editor fills in both when a parameter is picked by hand, and a builder that sets
only the name gets a node the compiler rejects. The cloud material had the same
bug and never showed it, because its branch only runs when a collection exists
— which until now it never had. Both go through one helper now,
`FGraph::CollectionParameter`, which sets the id as well.

And the collection's parameters get **deterministic ids** from their names.
A parameter constructed fresh gets a random GUID, so every re-bake of the
collection would have orphaned every material baked against the last one.

## The body switch, in place

```
switching from body 1 (Home) to 3 (Companion)
planet rebuilt in place: radius 1976 km, seed 20260908
site: body 3, sun.site 0.946, solar altitude 71.1 deg
buildings: 32 instances of SM_Building across 5 colours, pad SM_Pad
crossing 6/6: landed
```

`ALedgerPlanet`'s `BeginPlay` and `EndPlay` became `SetUp` and `TearDown`,
and `Rebuild` is the two of them. The pool of 3,600 section components is made
once for the life of the actor and only emptied on a rebuild. The atmosphere
stays on an airless moon and draws nothing; the town is laid out again on the
new body's site.

## The black town

With the buildings baked, the town came back as black silhouettes under a white
sky. Ruled out in order, each by a run rather than an argument:

| suspicion | test | result |
|---|---|---|
| memory pressure from three concurrent runs | the fixture alone | same frame |
| the placed actors | the fixture on `/Engine/Maps/Entry`, where everything is spawned as before | same frame |
| the lighting setup | sun, sky, post-process and atmosphere lines diffed against the last good run | identical |
| triangle winding | boxes and cones share one `AddTriangle` | same convention |
| the materials | `viewmode unlit` | **correct colours** — walls grey and beige, roofs grey, trees green |
| recomputed normals | the bake keeps the authored normals now | same frame |
| ray-traced shadows | engine default checked | off: the sun uses shadow maps |
| mobility | instanced components default | Movable, like the procedural town was |
| distance fields | `r.DistanceFieldShadowing 0, r.DistanceFieldAO 0` | same frame |

Lit wrong, coloured right, and none of the usual suspects. The recomputed
normals were a real defect all the same — `FMeshBuildSettings` defaults
`bRecomputeNormals` to true, `Mesh->Build()` applies it, and `Describe()` says
the authored normals come across — so the bake keeps them now. It was not this.

**What finally said something was measuring the light at the moment of each
frame** rather than reasoning about it:

```
front light: sun 59.4 deg above the horizon, 125 deg from the camera's view
front light: Sun points 0.0 deg from the ephemeris sun, intensity 102893
front light: sun -42.6 deg above the horizon, 133 deg from the camera's view
front light: Sun points 0.0 deg from the ephemeris sun, intensity 102855
```

The light is exactly where the ephemeris puts it. And at the second step the
sun is **forty-two degrees below the horizon at full strength**, and the frame
taken there shows a building face glowing white. Shadows reach six kilometres;
the planet is thirteen thousand across. A sun below the horizon was lighting
everything from beneath, through the body, wherever no shadow caster happened to
be within range — so what a "lit" frame meant depended on which way the nearest
walls faced relative to the ground, which is not lighting at all.

The fix is the physical one rather than a switch on the site's clock:
`bPerPixelAtmosphereTransmittance`. The atmosphere's transmittance along a ray
through the ground is zero, and asked per pixel it makes the terminator a place
on the planet — so a camera in orbit over the dark side still sees the day side
lit, which a global switch would get wrong.

**And that was the town as well.** The rain frame, with the sun 44.6 degrees
up and 140 degrees from the camera's view, came back fully lit the first time
per-pixel transmittance was on: walls, roofs, snow, sea, the shadows the
buildings throw. The reason is in the engine. An atmosphere sun light's
*global* transmittance is set once per frame from
`FAtmosphereSetup::GetTransmittanceAtGroundLevel(sun direction)`, which traces
from a single fixed point on the atmosphere's sphere — not from the camera —
and multiplies the result into the sun for deferred lighting, Lumen's direct
lighting and ray-traced lighting alike. On a flat level that point is where
everybody is standing. On a planet it is one place, and at this time of year
the sun is below that place's horizon all day (its direction has z = −0.068).
So every lit surface in the world got a fraction of nothing, while the sky —
which the atmosphere computes per pixel anyway — stayed bright, and the
auto-exposure blew it white trying to find anything to meter.

It never looked like a lighting bug because nothing about the light was wrong:
it was at the right angle, at the right intensity, and visible. Every run that
changed something about the *town* was a run that changed nothing.

One more thing ruled out on the way, and worth knowing: the code as it stood at
T095 renders the same black frame today, so the baked buildings and the placed
level were never the cause.

| | |
|---|---|
| `perpixel-before.png` | −18 h, sun 59.4° up: town, snowfields, sea, lit |
| `perpixel-rain.png` | the rain frame, sun 44.6° up: the same, with the buildings' shadows |

## Also fixed on the way

**The low-memory warning cried wolf.** It reported free *physical* memory as if
it were commit headroom. With a game running alongside, physical memory was
under two gigabytes on every run while twenty-seven gigabytes of commit were
free, and the warning fired nineteen times a run. An allocation fails when
commit runs out, so that is what it reads now (`GlobalMemoryStatusEx`,
`ullAvailPageFile`), with physical memory reported beside it.
