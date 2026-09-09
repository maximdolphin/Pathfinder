# -*- coding: utf-8 -*-
"""M02-M04: terrain to production standard, planetary bodies, atmosphere.

The first block of the technical model. Everything here is a *framework* — a
biome system rather than a biome, a weather model rather than a storm — because
the plan is one system built properly rather than thirty built once.
"""

# ---------------------------------------------------------------------------
# M02 - terrain to production standard.
# ---------------------------------------------------------------------------

M02 = [
    # The surface material foundation runs first, ahead of the LOD work.
    #
    # Two reasons, and the second is the real one. The visible one: every
    # texture on screen today is noise generated at load, and that is the whole
    # of why the world does not look real. The structural one: a material
    # architecture designed against procedural noise and then handed real
    # texture sets is a material architecture that gets rebuilt. Triplanar
    # projection, height blending, channel packing and mip policy are all
    # decisions that depend on what the source data actually is.
    #
    # It is also not obviously a cost. The current triplanar takes twelve
    # texture samples per pixel from textures with hand-built mip chains;
    # authored sets composited through a virtual texture are sampled once.
    dict(title="Source a PBR surface library and audit its licences",
         detail="Quixel Megascans through Fab, decided 2026-09-08. The first version of "
                "this task said the scans are free for Unreal use, which is wrong and was "
                "corrected by reading the listings: Megascans *3D assets* have a free tier "
                "that is UEFN reference-only, while Megascans *materials* are free outright "
                "under the Fab Standard License. Materials are what terrain needs. See "
                "docs/asset-licensing.md. Needs an Epic sign-in once, which is manual. "
                "Twelve to sixteen ground surfaces spanning the biomes the climate model "
                "will produce: sand, gravel, scree, granite, sandstone, grass, moss, snow, "
                "mud, clay. Every set records its source and licence in a manifest as it "
                "arrives — a project that audits licences later is one that finds out at "
                "ship time that it cannot.",
         acceptance="A manifest lists every surface set with source and licence, and a "
                    "validator fails the build on any texture present without an entry.",
         days=1, refs=["SS14"]),
    dict(title="Texture import pipeline for surface sets",
         detail="Pulled forward from M12, because sixteen sets imported by hand is sixteen "
                "chances to get a normal map's colour space wrong and one afternoon "
                "wondering why the lighting is inside out. Channel packing — roughness, "
                "ambient occlusion and height into one texture — compression settings per "
                "map type, sRGB flags, mip and streaming policy.",
         acceptance="A surface set dropped into the source directory is import-ready with "
                    "no manual step, and a validator rejects wrong channel layout or "
                    "colour space with a message naming the file.",
         days=2, refs=["SS14"]),
    dict(title="Real surface sets in the triplanar material",
         detail="Replace the runtime-generated noise with authored albedo, normal, "
                "roughness and height. Height blending rather than alpha blending, so "
                "gravel interlocks with sand instead of dissolving into it. Detail "
                "normals at a second scale, distance-faded before they can alias.",
         acceptance="One biome at three distances and two lighting conditions, captured "
                    "beside the noise version and beside reference photography, with the "
                    "frame cost measured rather than assumed.",
         days=2.5, refs=["SS6.8"]),
    dict(title="Runtime Virtual Texture for surface composition",
         detail="Compose the blended surface into an RVT so the expensive blend happens "
                "once per texel rather than once per pixel, and so decals and roads have "
                "something to write into. Moved up beside the material work: it is what "
                "makes layered authored sets affordable at all.",
         acceptance="Terrain material cost falls measurably against the direct-blend "
                    "version with no visual difference.",
         days=2, refs=["SS6.8"]),
    dict(title="Geomorphing across LOD transitions",
         detail="Vertices interpolate toward their coarser position as the transition "
                "approaches, so a split is a blend rather than a jump. The current pop is "
                "small at altitude and unmissable at walking pace.",
         acceptance="Walk a ridge line at 2 m/s across four LOD boundaries with no visible "
                    "vertex movement at any of them.",
         days=3, refs=["SS6.8"]),
    dict(title="Stitching for the finer-neighbour case",
         detail="Edge stitching currently handles a coarser neighbour. The mirror case "
                "arises whenever LOD is driven by anything other than pure distance, which "
                "it is about to be.",
         acceptance="A forced-depth region adjacent to a natural one shows no crack from "
                    "any angle.",
         days=2, refs=["SS6.8"]),
    dict(title="Collision streaming with a cook budget",
         detail="Cooking is the expensive half of a patch. Predictive cook along the "
                "velocity vector, bounded per frame, with a guarantee rather than a hope.",
         acceptance="At 900 m/s at 50 m altitude, a downward trace hits terrain on every "
                    "frame of a 200 km transect.",
         days=3, refs=["SS6.8"]),
    dict(title="Give the planet relief: the height function has no mountains",
         detail="The mountain band of the elevation function is exactly zero on ninety "
                "per cent of the land, and the whole planet tops out at 1,372 m against "
                "a configured 9 km. LedgerNoise::ErodedRidged returns "
                "(Sum / Normalisation) * 2 - 1, remapping to [-1,1] the way a signed fBm "
                "should -- but a ridged field with a continuity weight and erosion "
                "damping concentrates its energy into a few ridges, so its mean sits far "
                "below 0.5 and the remap puts most of the surface negative, where "
                "Elevation's FMath::Max(0.0, ...) deletes it. Four tasks are blocked on "
                "this and it has been living in their blocker notes; it is work in its "
                "own right. Whatever the fix -- a different pivot, an unsigned return, a "
                "renormalisation against the field's own measured distribution -- it "
                "reshapes every landform and invalidates every reference capture, which "
                "is why it is a task and not a patch.",
         acceptance="Ranges that read as ranges from 80 km, land reaching a decent "
                    "fraction of MaxElevation, and slopes steep enough to hold scree -- "
                    "measured by the existing relief and slope surveys, not by eye. The "
                    "reference captures are re-adopted in the same commit and the "
                    "write-up says what changed.",
         days=2, refs=["SS6.8"]),
    dict(title="Climate model: temperature and moisture fields",
         detail="Latitude, altitude, prevailing wind and distance from water produce "
                "temperature and moisture as continuous functions. Biomes are read from "
                "them rather than painted, so they are consistent with the terrain that "
                "produced them.",
         acceptance="A pole-to-equator transect shows monotonic temperature and a rain "
                    "shadow on the lee side of every major range.",
         days=4, refs=["SS6.8", "LW SS7.2"]),
    dict(title="Biome definition framework",
         detail="A biome is a region of climate space with material sets, scatter rules, "
                "colour response and slope behaviour. Data, not code.",
         acceptance="Adding a biome is one data file and requires no recompile.",
         days=3, refs=["ARCH Rule 7"]),
    dict(title="Biome-driven surface composition",
         detail="The climate field selects and weights the surface sets already in the "
                "material, so a slope crossing three biomes reads as three grounds meeting "
                "rather than as a gradient between three colours. Macro variation at "
                "kilometre scale on top, because uniform ground at any scale is the "
                "tell that it was generated.",
         acceptance="Three biomes meet on one slope with no visible blend band, no "
                    "repetition at any distance, and the boundary explained by the "
                    "climate field.",
         days=4, refs=["SS6.8"]),
    dict(title="Cliff and scree treatment by slope",
         detail="Slope drives material, not just colour: exposed rock strata on steep "
                "faces, scree accumulating at the angle of repose below them.",
         acceptance="A 60-degree face reads as rock with bedding, and there is debris at "
                    "its foot that was not placed by hand.",
         days=3, refs=["SS6.8"]),
    dict(title="Hydrology: flow accumulation and river networks",
         detail="Rivers derived from the height field by flow accumulation, carving their "
                "own valleys, reaching the sea. The erosion model already computes slope; "
                "this is what slope is for.",
         acceptance="Every river reaches the sea or a basin, none flows uphill, and the "
                    "same seed produces the same network.",
         days=5, refs=["SS6.8"]),
    dict(title="Caves and overhangs",
         detail="A height field cannot express an overhang. A sparse volumetric layer over "
                "the height field, meshed only where it is non-trivial, so the cost is paid "
                "where there are caves and nowhere else.",
         acceptance="Walk into a cave mouth, through a passage, and out the other side, "
                    "with collision and lighting correct throughout.",
         days=6, refs=["SS6.8"]),
    dict(title="Asset review harness: turntable, framing and lighting rig",
         detail="One command puts a generated asset on a stand and photographs it: eight "
                "yaw angles, three framings from silhouette to close, and two lighting "
                "rigs, at pinned exposure. Scheduled before the first generator that "
                "needs it, and for the same reason the capture comparison was built "
                "before the material work. ADR-0005 makes every asset in the game an "
                "iterated one, and iteration is only affordable if looking at the result "
                "is cheap and honest. It was not: three separate readings of the surface "
                "captures were wrong -- a camera underground, an exposure in stops "
                "mistaken for lux, a camera standing inside a building -- and each cost "
                "an hour because the picture was believed before the arithmetic.",
         acceptance="One command turns any generated asset into a contact sheet on disk, "
                    "with the camera provably outside the geometry, exposure fixed across "
                    "every frame, and two consecutive runs agreeing within a stated "
                    "tolerance rather than by eye.",
         days=1.5, refs=["ARCH Rule 6", "SS14"]),
    dict(title="Scatter framework: rocks, vegetation, debris",
         detail="Deterministic placement from biome rules, GPU-instanced, LOD'd, with "
                "density that survives the frame budget at ground level.",
         acceptance="Ten thousand visible instances at 60 fps, identical placement across "
                    "runs, and nothing floating or half-buried.",
         days=4, refs=["SS6.8"]),
    dict(title="Vegetation with wind response",
         detail="Trees and grass that move with the wind field M04 will provide, and stop "
                "moving when it stops.",
         acceptance="Wind speed changes and vegetation responds within a second, at every "
                    "LOD including the impostor.",
         days=3, refs=["SS6.8"]),
    dict(title="Far-field impostors and horizon detail",
         detail="Beyond the last real LOD, the planet needs to keep its silhouette. "
                "Impostors for scatter, and a horizon that does not go smooth.",
         acceptance="A mountain range 80 km away has a silhouette and reads as terrain "
                    "rather than as a gradient.",
         days=3, refs=["SS6.8"]),
    dict(title="Snow, ice and seasonal cover",
         detail="Cover driven by the climate field and the season, accumulating by altitude "
                "and latitude, affecting material and scatter.",
         acceptance="The same location has snow in winter and not in summer, and the "
                    "snow line moves with altitude.",
         days=3, refs=["SS6.8"]),
    dict(title="Terrain sampling API with a stable contract",
         detail="One place gameplay asks for height, normal, biome, slope and material at a "
                "point, agreeing exactly with what is rendered. Everything built after this "
                "depends on it.",
         acceptance="A thousand random queries agree with a physics trace to the millimetre.",
         days=2, refs=["ARCH SS3"]),
    dict(title="Terrain modification API",
         detail="Levelling, excavation and fill, persisted as a sparse delta over the "
                "generated field. Construction in M19 needs ground it can flatten.",
         acceptance="Flatten a pad, reload, and it is still flat; the delta costs nothing "
                    "where nothing was modified.",
         days=4, refs=["LW SS7.4"]),
    dict(title="Streaming budget manager",
         detail="One authority deciding how much generation, cooking and upload happens per "
                "frame, with the budget split by priority rather than first come first "
                "served.",
         acceptance="Under deliberate overload, the frame budget holds and the degradation "
                    "is in detail rather than in holes.",
         days=3, refs=["SS6.8"]),
    dict(title="Shadow-specific terrain LOD",
         detail="Shadow casters do not need the LOD the camera needs, and paying full "
                "resolution into a shadow map is most of a cascade wasted.",
         acceptance="Shadow cost drops measurably with no visible change in shadow quality.",
         days=2, refs=["SS14"]),
    dict(title="Disk cache for generated patches",
         detail="Generation is deterministic, so it need only happen once per machine. A "
                "content-addressed on-disk cache under the terrain cache.",
         acceptance="A second visit to the same ground generates nothing and is bit-identical.",
         days=2, refs=["SS6.8"]),
    dict(title="Terrain debug visualisers",
         detail="LOD level, patch boundaries, collision presence, biome, climate fields, "
                "cache state, streaming queue — all toggleable in-game.",
         acceptance="Every terrain bug class in M00's history is diagnosable from the "
                    "visualisers without adding code.",
         days=2, refs=["SS13"]),
    dict(title="Terrain regression suite",
         detail="Automated flights over fixed seeds producing captures and metrics, "
                "compared against references. Holes, cracks, popping and budget overruns "
                "all become build failures.",
         acceptance="Each of those four failure modes, introduced deliberately, is caught "
                    "by CI.",
         days=3, refs=["SS13"]),
    dict(title="Playable proof: the 200 km transect",
         detail="Rule 6. The milestone gate, run and recorded.",
         acceptance="200 km at 300 m and 900 m/s: no holes, no seams, no frame over 16 ms, "
                    "collision present throughout.",
         days=2, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M03 - planetary bodies and orbital mechanics.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------- M2W
#
# ADR-0006. Everything in this project is built at runtime, and that was never
# decided -- it is what the prototype did to answer a rendering question, and it
# has been the architecture ever since. The bill: a packaged build has no
# materials at all, Nanite is unavailable to every object in the world, nothing
# has a mesh distance field, and generation runs on the game thread, which is
# where the frame budget is failing.

M2W = [
    dict(title="A project level, with the world placed in it",
         detail="The project loads /Engine/Maps/Entry -- an empty engine map -- and a "
                "subsystem spawns the planet, the sun, the atmosphere and the ship into "
                "it. So the world exists only while code is running, and there is nothing "
                "to open, look at or select. A real map, with the things that are not "
                "generated placed as actors, and the generated ones attached to something "
                "a person can find.",
         acceptance="The project opens on its own map, the world's fixed actors are "
                    "visible and selectable in the editor, and the scripted flight runs "
                    "from it unchanged.",
         days=2, refs=["ARCH Rule 1"]),
    dict(title="Materials as saved assets, generated once",
         detail="Every material is RF_Transient and built as a node graph on each launch "
                "inside an editor-only block, which is why LedgerSurfaceCooked.cpp "
                "returns null for all of them and a packaged build has no look at all. "
                "The graphs stay in C++ and stay the source of truth -- that is what "
                "makes them diffable and is worth keeping. A commandlet runs them once "
                "and saves the result, and the game loads assets. The same arrangement "
                "the surface textures already use.",
         acceptance="Every material in the game is a saved asset; a build with the editor "
                    "stripped renders the terrain, water and underwater passes correctly; "
                    "and regenerating produces no diff unless the C++ changed.",
         days=3, refs=["ARCH Rule 1", "SS14"]),
    dict(title="Asset generation as an editor commandlet",
         detail="The mechanism the generators of ADR-0005 plug into: a generator is a "
                "function from parameters to geometry, and this is what runs it offline, "
                "names the package, saves it, and records what produced it. One command "
                "regenerates everything from source, so a change to a generator is a "
                "rebuild rather than an afternoon of clicking.",
         acceptance="One command regenerates every generated asset from its definition; a "
                    "second run leaves the version-controlled ones byte-identical and the "
                    "derived ones equivalent by settings and render; and every generated "
                    "asset records the generator that made it.",
         days=3, refs=["SS14"]),
    dict(title="Static mesh output with Nanite, LODs and collision",
         detail="What the commandlet actually emits. Nanite enabled where it belongs, "
                "automatic LOD chains, simplified collision matching the silhouette, and "
                "distance-field generation. None of this is available to a procedural "
                "mesh and all of it is free once the output is a static mesh, which is "
                "the whole argument of ADR-0006 in one task.",
         acceptance="A generated hull comes out as a static mesh with Nanite on, a full "
                    "LOD chain, collision matching its silhouette and a mesh distance "
                    "field, with no manual step.",
         days=3, refs=["SS14"]),
    dict(title="Instanced rendering for repeated and scattered assets",
         detail="Three hundred and forty-two trees are three hundred and forty-two "
                "procedural mesh components today, each with its own draw and its own "
                "generation cost on the game thread. Once assets are static meshes they "
                "instance, and a forest becomes one draw. The scatter framework in M02 "
                "decides where they go; this decides what they are placed as.",
         acceptance="A settlement's trees and props render as instanced draws, the draw "
                    "count is independent of the instance count, and the measured "
                    "game-thread cost of placing them falls against the current build.",
         days=2, refs=["SS6.8", "SS14"]),
    dict(title="Asset layout, naming and ownership in the content tree",
         detail="Where generated assets live, what they are called, and how a stale one "
                "is found. Boring, and the alternative is a content directory nobody can "
                "navigate in two years and a set of orphans nobody dares delete.",
         acceptance="Every asset's package path is derived from its definition rather "
                    "than typed, and a generated asset with no definition behind it is "
                    "reported by a validator naming the file.",
         days=1, refs=["ARCH Rule 1"]),
    dict(title="Terrain component type, measured rather than assumed",
         detail="After this milestone the terrain is the only runtime-generated geometry "
                "left, and it owns the game-thread cost failing the frame budget: 20 to "
                "31 ms through every low-altitude phase against 16.7, with the GPU at 5 "
                "to 9. UProceduralMeshComponent was the prototype's choice and has never "
                "been compared against anything. Measure it against the alternatives on "
                "generation, upload and draw cost before deciding to keep it.",
         acceptance="A written comparison of at least two component types over the same "
                    "flight, with generation, upload and draw cost measured for each, and "
                    "a decision recorded with its numbers.",
         days=4, refs=["SS6.8"]),
    dict(title="Shader compilation and PSO caching",
         detail="Building material graphs at runtime means compiling shaders at runtime, "
                "which is a stall the moment anything new comes into view. Once materials "
                "are assets their shaders cook, and the pipeline state objects need "
                "collecting so that first sight of a surface is not a hitch.",
         acceptance="A packaged build shows no compilation stall on first sight of any "
                    "material, and a first run's frame-time trace matches a second run's "
                    "within the stated tolerance.",
         days=2, refs=["SS14"]),
    dict(title="Package the technical slice and run it without the editor",
         detail="The narrow version of what M12 does properly later: prove it packages "
                "and runs at all. Today it cannot, and every milestone spent adding "
                "content on top of a build that has never shipped once is a milestone "
                "spent finding out later.",
         acceptance="A packaged Windows build launches with no editor present, flies the "
                    "scripted flight, and writes its captures and frame-time report.",
         days=3, refs=["SS14"]),
    dict(title="Write down the asset creation strategy",
         detail="What is baked, what is streamed, what is placed, and how a new kind of "
                "thing gets added to each. ADR-0006 records the decision; this is the "
                "document somebody follows. It exists because the current architecture "
                "was never chosen -- it is what the prototype happened to do -- and the "
                "way that happens again is by leaving the choice implicit.",
         acceptance="A document that answers, for any new asset type, which tier it "
                    "belongs in and what producing one involves, with a worked example "
                    "for each of the three tiers.",
         days=1, refs=["SS14"]),
    dict(title="Playable proof: the packaged build flies the same flight",
         detail="Per ARCH Rule 6 the milestone ends with something that runs: a build with "
                "no editor, flying orbit to ground to orbit, producing captures that "
                "match the editor build.",
         acceptance="Captures from the packaged build match the committed references "
                    "within the perceptual tolerance, and its frame-time report is no "
                    "worse than the editor build's.",
         days=1, refs=["ARCH Rule 6"]),
]

M03 = [
    dict(title="Body definition: mass, radius, rotation, tilt, orbit",
         detail="One description a planet, moon, station or asteroid is built from, in "
                "double precision, deterministic from seed.",
         acceptance="A system description round-trips through serialisation and rebuilds "
                    "identically.",
         days=2, refs=["LW SS8"]),
    dict(title="Keplerian ephemeris",
         detail="Position and velocity of every body at any time, from orbital elements. "
                "Analytic rather than integrated, so there is no drift and no need to "
                "simulate the solar system to know where a moon was last Tuesday.",
         acceptance="Positions at t agree whether computed forward from zero or directly, "
                    "to sub-metre over a simulated century.",
         days=4, refs=["LW SS8"]),
    dict(title="Reference frame hierarchy",
         detail="System, body, surface and vehicle frames with transforms between them. "
                "Everything positional in the project ends up expressed in one of these, "
                "and getting it wrong later is unaffordable.",
         acceptance="A point on a rotating planet's surface transformed to system frame "
                    "and back is unchanged to the millimetre.",
         days=3, refs=["SS6.8"]),
    dict(title="Rotation, day and night from the real rotation rate",
         detail="Sun position derived from the body's rotation and orbit, not from a "
                "time-of-day slider.",
         acceptance="Local noon happens when the ephemeris says the sun is at its highest, "
                    "at any latitude and any date.",
         days=2, refs=["SS6.8"]),
    dict(title="Axial tilt and seasons",
         detail="Tilt drives insolation, which drives the climate field from M02 and the "
                "snow line with it.",
         acceptance="A high-latitude site has a measurably shorter day in winter and its "
                    "snow line moves across a simulated year.",
         days=2, refs=["SS6.8"]),
    dict(title="Multi-body rendering with correct illumination",
         detail="Other planets and moons visible in the sky, at the right place, with the "
                "right phase and the right apparent size.",
         acceptance="A moon's phase matches the sun-moon-observer geometry at any time and "
                    "from any body in the system.",
         days=3, refs=["SS6.8"]),
    dict(title="Eclipses and body shadows",
         detail="A moon casting a shadow on a planet, a planet eclipsing its moon, and the "
                "lighting change on the ground when it happens.",
         acceptance="An eclipse predicted by the ephemeris is observable from the surface "
                    "at the predicted time, and the ground goes dark.",
         days=3, refs=["SS6.8"]),
    dict(title="The star as a physical light",
         detail="Intensity, colour temperature and angular size from the star's class and "
                "the observer's distance — so an inner planet is genuinely brighter and an "
                "outer one genuinely dimmer.",
         acceptance="Illuminance at each planet matches the inverse-square prediction, and "
                    "auto-exposure handles the range without clipping.",
         days=2, refs=["SS6.8"]),
    dict(title="Star field from a generated catalogue",
         detail="Stars with position, magnitude and colour, correct for the observer's "
                "location in the galaxy, and parallax between systems.",
         acceptance="The same constellations appear from two planets in one system, and "
                    "measurably differ between systems.",
         days=3, refs=["SS6.8"]),
    dict(title="Moons on the shared terrain framework",
         detail="A moon is a body with a different radius, no atmosphere and different "
                "climate inputs. If it needs its own renderer, the framework is wrong.",
         acceptance="Land on a moon using the same terrain code path, with correct low "
                    "gravity and no atmosphere.",
         days=3, refs=["SS6.8"]),
    dict(title="Gas giants",
         detail="Volumetric banding, storms, and a surface you can descend into until "
                "pressure ends the attempt.",
         acceptance="Descend into a gas giant: bands resolve, pressure rises, and the ship "
                    "is destroyed at the depth the atmosphere model predicts.",
         days=4, refs=["SS6.8"]),
    dict(title="Ring systems",
         detail="Particle rings with self-shadowing and a shadow cast on the planet, "
                "flyable through rather than a texture on a disc.",
         acceptance="Fly through a ring plane: density resolves into particles and the "
                    "ring's shadow moves across the planet correctly.",
         days=3, refs=["SS6.8"]),
    dict(title="Asteroid fields and small irregular bodies",
         detail="Non-spherical bodies need a different mesh path from the cube-sphere, "
                "and orbits that are individually tracked rather than instanced.",
         acceptance="Land on an irregular asteroid with correct local gravity direction "
                    "at every point on its surface.",
         days=4, refs=["SS6.8"]),
    dict(title="Orbital stations and platforms",
         detail="Structures on rails around a body, with their own local frame that things "
                "can dock to and stand in.",
         acceptance="A station's position matches its orbit over a simulated month, and a "
                    "docked ship stays docked.",
         days=3, refs=["SS6.9"]),
    dict(title="Gravity field from real masses",
         detail="Inverse-square from every significant body rather than one hardcoded "
                "constant, including the transition between spheres of influence.",
         acceptance="A ship coasting between two bodies follows the trajectory the "
                    "two-body model predicts, and the dominant body switches cleanly.",
         days=3, refs=["SS6.9"]),
    dict(title="System generation from seed",
         detail="Star class, planet count, orbits, compositions and moons, generated to be "
                "plausible rather than uniform — hot rocks close in, gas giants further "
                "out, habitable zones where the star puts them.",
         acceptance="Twenty generated systems are all physically plausible and none is a "
                    "rearrangement of another.",
         days=4, refs=["LW SS8"]),
    dict(title="Time acceleration for testing",
         detail="Run the ephemeris and climate at a thousand times normal so a year can be "
                "inspected in minutes, without the renderer trying to keep up.",
         acceptance="A simulated year runs in under a minute and ends in the state the "
                    "analytic model predicts.",
         days=2, refs=["SS13"]),
    dict(title="Ephemeris determinism and agreement tests",
         detail="Two processes, one seed, identical positions; and the sky agrees with the "
                "ephemeris at a hundred random times and places.",
         acceptance="Sky and ephemeris agree to the arcminute in every sample.",
         days=2, refs=["ARCH Rule 5"]),
    dict(title="System map and query API",
         detail="What is where, how far, how long to get there. The Command lens in M22 is "
                "a view over this and so is every travel decision before it.",
         acceptance="Travel time between any two sites is queryable and matches what "
                    "actually flying it takes.",
         days=2, refs=["LW SS4"]),
    dict(title="Playable proof: watch a moon transit and then fly to it",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="A moon rises, transits and sets on schedule; then it is flown to and "
                    "landed on, in one session.",
         days=2, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M04 - atmosphere, weather and environment.
# ---------------------------------------------------------------------------

M04 = [
    dict(title="Atmosphere profile per body",
         detail="Composition, surface pressure, scale height and lapse rate as data. "
                "Earth's numbers become one row rather than the only row.",
         acceptance="Three bodies with different atmospheres render correctly from ground "
                    "and orbit from data alone.",
         days=3, refs=["SS6.8", "LW SS7.2"]),
    dict(title="Scattering for non-Earth compositions",
         detail="Rayleigh and Mie coefficients derived from composition, so a CO2 sky is "
                "the colour physics says and not the colour someone liked.",
         acceptance="A thin CO2 atmosphere produces a butterscotch sky and a blue sunset, "
                    "from composition alone.",
         days=3, refs=["SS6.8"]),
    dict(title="Volumetric fog that is not planar",
         detail="Exponential height fog fills space from orbit because it has no notion of "
                "a sphere. Ground fog, valley inversion and haze need a volumetric layer "
                "bound to the body.",
         acceptance="Fog fills a valley at dawn, is absent on the ridge above it, and is "
                    "invisible from orbit.",
         days=4, refs=["SS6.8"]),
    dict(title="Pressure-cell weather model",
         detail="A coarse global field of pressure cells advected by rotation, producing "
                "wind, fronts and storm systems that move and evolve. Deterministic from "
                "seed and time.",
         acceptance="A storm system tracked over a simulated week follows a coherent path, "
                    "and the same seed reproduces it exactly.",
         days=5, refs=["LW SS7.2"]),
    dict(title="Wind field queryable by everything",
         detail="Speed and direction at any point and altitude, read by flight, vegetation, "
                "particles, audio and the settlement layer.",
         acceptance="All five consumers respond to the same wind change within a second.",
         days=2, refs=["SS6.9"]),
    dict(title="Cloud layers at real altitudes",
         detail="Multiple volumetric decks — low cumulus, mid, high cirrus — at altitudes "
                "the atmosphere profile determines, advected by the wind field.",
         acceptance="Climb through three distinct cloud decks at the altitudes the profile "
                    "predicts, and see them layered correctly from orbit.",
         days=4, refs=["SS6.8"]),
    dict(title="Precipitation: rain, snow and dust",
         detail="Driven by the weather model, rendered volumetrically and as surface "
                "effects, with the type decided by temperature and composition.",
         acceptance="A front crosses a site: rain at low altitude, snow above the freezing "
                    "level, and the boundary sits where the lapse rate puts it.",
         days=4, refs=["LW SS7.2"]),
    dict(title="Surface wetness, puddles and drying",
         detail="Rain changes roughness and colour, water collects where the terrain says "
                "it should, and it dries at a rate the temperature decides.",
         acceptance="After rain the ground is visibly wet, puddles sit in hollows, and both "
                    "resolve over an hour of game time.",
         days=3, refs=["SS6.8"]),
    dict(title="Storms: lightning, dust and severe weather",
         detail="The extreme tail of the weather model, with the visual and audio "
                "signature to match, and enough force to matter to a ship.",
         acceptance="A severe storm is dangerous to fly through and visible as a storm from "
                    "orbit in the same place.",
         days=4, refs=["LW SS7.2"]),
    dict(title="Turbulence and wind loading on vehicles",
         detail="Wind shear, gusts and thermals as forces on the flight model, not as "
                "camera shake.",
         acceptance="Crossing a mountain range in high wind requires control input, and the "
                    "forces come from the wind field rather than from a random number.",
         days=3, refs=["SS6.9"]),
    dict(title="Temperature and pressure as physical fields",
         detail="Queryable at any point, driven by latitude, altitude, season and weather. "
                "Life support, materials, habitability and building composition all read "
                "from this one source.",
         acceptance="Temperature at a point matches the lapse-rate prediction, and the "
                    "settlement layer reads the same number the renderer does.",
         days=2, refs=["LW SS7.2"]),
    dict(title="Re-entry heating from atmospheric density",
         detail="Heating as a function of density and velocity, with the visual effect and "
                "the ship damage both derived from it rather than triggered by altitude.",
         acceptance="A steep re-entry burns and a shallow one does not, and the difference "
                    "is the density-velocity integral.",
         days=3, refs=["SS6.9"]),
    dict(title="Cockpit and visor effects",
         detail="Rain streaks that respond to airspeed, dust accumulation, icing, fogging, "
                "and the wipers or heaters that clear them.",
         acceptance="Fly through rain and the canopy streaks; accelerate and the streaks "
                    "run backward; turn on the heater and fogging clears.",
         days=3, refs=["SS6.9"]),
    dict(title="Environmental audio",
         detail="Wind by speed and air density, precipitation on surfaces, thunder delayed "
                "by distance, and silence in vacuum.",
         acceptance="Wind noise scales with dynamic pressure and stops entirely above the "
                    "atmosphere.",
         days=2, refs=["SS6.9"]),
    dict(title="Aurora at magnetic poles",
         detail="Driven by the body's magnetic field and stellar activity, visible from "
                "the ground and from orbit.",
         acceptance="Aurora appears at high latitude during a modelled solar event and not "
                    "otherwise.",
         days=2, refs=["SS6.8"]),
    dict(title="Weather forecast API",
         detail="What the weather will be at a place and time. Flight planning, settlement "
                "shutters and mission generation all need to anticipate rather than react.",
         acceptance="A forecast issued six hours ahead matches what happens, within the "
                    "model's stated confidence.",
         days=2, refs=["LW SS7.1"]),
    dict(title="Volumetric performance budget",
         detail="Clouds, fog and precipitation are the easiest way to lose the frame. "
                "Resolution, temporal reprojection and distance-based quality, under one "
                "budget.",
         acceptance="Worst-case weather costs no more than 4 ms and degrades in detail "
                    "rather than in frame rate.",
         days=3, refs=["SS14"]),
    dict(title="Weather determinism and orbit-to-ground agreement",
         detail="Same seed and time gives the same weather, and the storm seen from orbit "
                "is the storm flown into.",
         acceptance="Both properties hold across a hundred sampled times and places.",
         days=2, refs=["ARCH Rule 5"]),
    dict(title="Playable proof: fly into a storm front",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Visibility, wind loading and audio change together on entry, and the "
                    "same storm is in the same place from orbit.",
         days=2, refs=["ARCH Rule 6"]),
]
