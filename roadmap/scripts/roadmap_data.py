# -*- coding: utf-8 -*-
"""Roadmap content for LEDGER.

Authored as data rather than typed into a UI: the roadmap is a year of
interdependent work and it belongs in version control next to the code it
describes, where a change to it shows up in a diff.

Every task carries an `acceptance` line. A task without a falsifiable finish
condition is a wish, and a roadmap of wishes cannot tell you whether it is on
schedule -- which is the whole reason the design document insists on gates
(design SS2, SS13).

Design section references point at `docs/design/ledger-design.md`.
"""

# Milestones. Each one is a checkpoint with a gate that can be answered yes or
# no by looking at a build, plus space for evidence proving it was passed.
MILESTONES = [
    dict(
        id="M0", title="Foundations", week_start=-6, week_end=0,
        goal="A deterministic headless sim and a real-scale planet you can fly to and from.",
        gate="Read out/world-dump.txt: can you trace a corporate collapse back four causes, "
             "and do two NPCs disagree about the same fact for legible reasons? "
             "Separately: does the client fly orbit to ground to orbit with no loading screen?",
        refs=["SS13.1", "SS6.8", "SS15.1"],
    ),
    dict(
        id="M1", title="Terrain to production standard", week_start=1, week_end=5,
        goal="The terrain stops being a spike: water, stable LOD, collision everywhere it is "
             "needed, and assets that survive a cook.",
        gate="Fly a 200 km transect at 300 m altitude at 60 fps with no holes, no popping "
             "that reads as a seam, and no frame over 20 ms.",
        refs=["SS6.8", "SS14 R3"],
    ),
    dict(
        id="M2", title="Surface fidelity", week_start=6, week_end=11,
        goal="Ground that holds up at walking distance: biome-blended materials, real texture "
             "sets, and detail that survives every LOD transition.",
        gate="Stand on the surface at three biomes and at three altitudes. No tiling is "
             "visible, no shimmer, and the material does not change identity when a patch splits.",
        refs=["SS6.8"],
    ),
    dict(
        id="M3", title="Ship systems and flight", week_start=12, week_end=17,
        goal="A ship you can actually fly: cockpit, HUD, landing gear, fuel, damage, and a "
             "flight model that behaves in vacuum and in air.",
        gate="Take off from the pad, reach orbit, re-enter, and land on the same pad using only "
             "the cockpit view and the HUD.",
        refs=["SS6.9", "SS1.1"],
    ),
    dict(
        id="M4", title="Sim to client bridge", week_start=18, week_end=22,
        goal="The client stops reading a file. protobuf over TCP, live world state, and a "
             "contract board that reflects the running simulation.",
        gate="Kill the sim mid-flight: the client degrades visibly and recovers when it "
             "restarts, without a crash and without inventing state.",
        refs=["SS5", "SS9", "ADR-0001"],
    ),
    dict(
        id="M5", title="The bounty loop, end to end", week_start=23, week_end=29,
        goal="Design SS4.2's canonical mission, built properly: accept, investigate by "
             "disclosure, narrow a search volume, locate physically, resolve, choose delivery.",
        gate="Phase 1 gate (SS13.2): does the bounty feel like investigation rather than a quest "
             "marker? Can a player describe how they found the target?",
        refs=["SS4.2", "SS6.3", "SS13.2"],
    ),
    dict(
        id="M6", title="Narration", week_start=30, week_end=34,
        goal="A local model that renders state into prose and selects from a closed action "
             "space. It never decides anything that matters.",
        gate="Tier 5: a full sim run with narration stubbed produces byte-identical world state "
             "to one with narration live.",
        refs=["SS6.4", "SS11"],
    ),
    dict(
        id="M7", title="Economy and the news feed", week_start=35, week_end=40,
        goal="Markets that move because something happened, and a news feed that is a lossy, "
             "biased rendering of true underlying state.",
        gate="Watch a convoy die, then trade on it before the article appears. The article, when "
             "it appears, is late and slightly wrong.",
        refs=["SS6.5", "SS12"],
    ),
    dict(
        id="M8", title="Crew", week_start=41, week_end=44,
        goal="Named, simulated people you hire, rely on, and can lose. The losable thing "
             "(SS2.1).",
        gate="Lose a crew member to a chain of events that was your fault and was foreseeable, "
             "and be able to reconstruct the chain afterwards.",
        refs=["SS2.1", "SS15.4"],
    ),
    dict(
        id="M9", title="All six mission archetypes", week_start=45, week_end=48,
        goal="Audit, Interception, Extraction, Fabrication and Collection, all on the one "
             "hidden-variable algorithm. No second implementation.",
        gate="SS12 oatmeal detector: no archetype exceeds 35% of generated contracts over a "
             "10,000 tick run, and every one uses the same investigation code path.",
        refs=["SS6.6", "SS6.3", "SS12"],
    ),
    dict(
        id="M10", title="Simulation LOD and reification", week_start=49, week_end=52,
        goal="A world that ticks statistically where nobody is watching and instantiates "
             "consistently when somebody arrives.",
        gate="Property test: aggregate(reify(A)) == A for arbitrary aggregate state. And a "
             "player cannot find the seam by flying between regions.",
        refs=["SS6.7", "SS15.3"],
    ),
    dict(
        id="M11", title="Multiplayer foundation", week_start=53, week_end=56,
        goal="A 16 to 32 player shard, sim-authoritative, with the PvPvE persistence classes "
             "resolved and IP counsel's review complete.",
        gate="Sixteen players on one shard for an hour. No desync, no duplicated bounty target, "
             "and the belief graph survives a coordinated rumour-injection attempt.",
        refs=["SS6.10", "SS7", "SS15.2"],
    ),
    dict(
        id="M12", title="MVP gate", week_start=57, week_end=58,
        goal="The four pillar tests, on a live build, with real players.",
        gate="P1: log out for a week, find three causally-explicable changes. P2: reconstruct "
             "four causes of any world change. P3: two players hold contradictory sincere "
             "beliefs. P4: lose something you spent twenty hours building, foreseeably.",
        refs=["SS2", "SS13.3"],
    ),
]


def task(title, detail, acceptance, days, refs=None):
    """One unit of work. `acceptance` is what makes it checkable rather than vibes."""
    return dict(title=title, detail=detail, acceptance=acceptance, days=days, refs=refs or [])


# ---------------------------------------------------------------- M0: done
M0 = [
    task("Rust workspace and CI-ready sim crate",
         "Scaffold sim/ as a zero-dependency Rust crate. Event log, deterministic fold, seeded "
         "PRNG, fixed-point arithmetic.",
         "cargo test green on an empty suite; replay determinism test exists before any domain logic.",
         3, ["SS16", "SS5.3"]),
    task("Fixed-point arithmetic with no floats in authoritative state",
         "Fx as scaled i64 with i128 intermediates. Display, clamp, exact rational scaling.",
         "Multiplication at 1e12 does not overflow; display is stable and signed.", 1, ["SS5.2"]),
    task("Deterministic PRNG seeded per region per tick",
         "SplitMix64 derived from (world_seed, region_id, tick). Never thread_rng.",
         "Same seed gives the same sequence; adjacent ticks and regions do not correlate.", 1, ["SS5.3"]),
    task("Event log and the fold",
         "Append-only log; World::apply is the only write path; every event carries its cause.",
         "Causal chain walks back and terminates; every cause points at an earlier event.", 2, ["SS5.3"]),
    task("Belief graph with propagation, decay and distortion",
         "Typed Proposition enum, Belief with confidence and provenance, single-hop propagation.",
         "Confidence never rises without independent corroboration; propagation terminates on a "
         "cyclic social graph.", 4, ["SS6.1"]),
    task("Disposition computed, never stored",
         "Disposition is a pure function of an entity's belief set. No reputation field anywhere.",
         "grep the codebase for a stored reputation or relationship scalar: zero hits.", 1, ["SS6.1"]),
    task("Obligation ledger as a consumed-on-redemption multigraph",
         "Typed ObligationClass; favours are spent, not renewable.",
         "A redeemed favour cannot be redeemed twice; ordering is deterministic.", 2, ["SS6.2"]),
    task("Investigation algorithm with search volumes",
         "Search volume over a hidden variable; disclosures constrain it; contradictions widen it.",
         "A contradictory claim widens the volume rather than silently picking a winner.", 3, ["SS6.3"]),
    task("Economy with conservation laws",
         "Cash and territory move between corps, never evaporate. Mean-reversion via overhead.",
         "Money supply and lane count constant over 20,000 ticks; no faction ossifies.", 3, ["SS6.5"]),
    task("Mission generation as a query over existing tension",
         "find (A,B) where A holds unresolved obligation against B and the tension predates the query.",
         "Over 90% of contracts reference tension incurred before they were posted.", 2, ["SS6.6"]),
    task("Text world-dump and the SS12 metrics",
         "A readable log of what happened and why, with the metrics printed into it.",
         "A corporate collapse can be traced back four causes from the dump alone.", 3, ["SS13.1", "SS12"]),
    task("Tier 2 and Tier 4 test suites",
         "Invariant property tests and bit-identical replay determinism.",
         "69 tests green; replay from the same seed is identical, from a different seed is not.",
         3, ["SS11"]),
    task("UE5 C++ client with a typed snapshot boundary",
         "No Blueprint logic. Archetypes parse into a UENUM; unknown variants are a parse failure.",
         "Six automation tests pass headless via UnrealEditor-Cmd.", 3, ["SS9", "SS11"]),
    task("Cube-sphere quadtree terrain with screen-space error LOD",
         "Six roots, horizon culling, edge-index stitching, vertices relative to node centres.",
         "A planet renders from orbit with no cracks at face seams.", 5, ["SS6.8"]),
    task("Move patch generation to worker threads",
         "Free functions over copied inputs; the game thread only uploads.",
         "Worst game-thread cost falls from 53 ms to under 8 ms; zero starved patches at altitude.",
         3, ["SS6.8", "ADR-0002"]),
    task("Scale the planet to 6,371 km",
         "Real radius, MaxDepth 15, noise frequency bands rescaled to the circumference.",
         "4.8 m quads at the finest LOD; LWC carries the coordinates without precision loss.",
         2, ["SS6.8"]),
    task("Sky Atmosphere, volumetric clouds, ozone",
         "Earth's own scattering values at Earth's radius. Height fog removed as flat-world.",
         "Sky is black outside the atmosphere and blue from the ground, with no parameter fighting.",
         3, ["SS6.8"]),
    task("Analytic erosion via slope-damped octaves",
         "Exact noise derivatives; each octave damped by the accumulated slope above it.",
         "Terrain shows drainage. No seams at LOD boundaries, because it is a pure function.",
         3, ["SS6.8"]),
    task("Triplanar surface material from generated mipped textures",
         "256px detail textures with a full mip chain; world-aligned projection; depth fade.",
         "No aliasing at any distance; no UV seam anywhere on the sphere.", 3, ["SS6.8"]),
    task("Ship, gravity, and a town",
         "6-DOF flight, inverse-square gravity, atmospheric drag; 32 buildings and 342 trees.",
         "The ship lands under gravity and climbs back to orbit through its own flight model.",
         4, ["SS6.9"]),
    task("ADR-0001 and ADR-0002",
         "Stack decision and the terrain build-vs-buy decision, with measurements attached.",
         "Both ADRs are in docs/adr with Context, Decision, Consequences and rejected alternatives.",
         1, ["SS8.5", "SS15.1"]),
]

# ---------------------------------------------------------------- M1
M1 = [
    task("Water surface as a separate render pass",
         "Oceans are currently coloured terrain. Build a sea-level shell mesh with its own "
         "material: depth-based colour, Fresnel, and a screen-space reflection fallback.",
         "Standing on a beach, the waterline is a surface with a horizon, not a colour change.",
         4, ["SS6.8"]),
    task("Wave displacement and shoreline foam",
         "Gerstner waves in the water material, amplitude falling off in shallow water. Foam "
         "where the terrain approaches sea level.",
         "Waves are visible from 200 m and flatten correctly against the shore.", 3),
    task("Underwater fog and camera transition",
         "A post-process volume that engages when the camera crosses the waterline.",
         "Flying into the sea transitions without a pop and back out without a flash.", 2),
    task("Ocean-floor terrain profile",
         "Seabeds currently use one smoothing rule. Add continental shelf, slope and abyssal "
         "plain so the depth profile reads as bathymetry.",
         "A cross-section from coast to deep ocean shows a shelf break.", 2, ["SS6.8"]),
    task("Patch cache keyed by node id",
         "Regenerating a patch the camera just left is pure waste. LRU cache of recent vertex "
         "buffers, bounded by memory not count.",
         "Flying back and forth across a ridge regenerates nothing after the first pass.", 3),
    task("Predictive patch prefetch along the velocity vector",
         "The collision cook already leads the camera. Do the same for geometry so a fast "
         "approach arrives at ground that already exists.",
         "A 300 m/s descent shows no unfilled patches at any point.", 2, ["SS6.8"]),
    task("LOD transition blending",
         "Patches currently swap. Blend between levels over a short distance band using vertex "
         "morphing toward the parent's surface.",
         "No visible pop at any LOD boundary during a continuous descent.", 4, ["SS6.8"]),
    task("Fix stitching for the finer-neighbour case",
         "Stitching only collapses vertices on the fine side. Verify the coarse side never "
         "needs it, and add a test that walks every visible edge pair.",
         "An automated test finds zero cracks over 500 randomised camera positions.", 3, ["SS6.8"]),
    task("Collision for the town footprint and landing pads",
         "Terrain collision is camera-radius only. Pin collision permanently around persistent "
         "structures so a ship cannot fall through an unloaded patch.",
         "Teleport to the pad from orbit: the ship rests on it immediately.", 2),
    task("Collision cook budget and instrumentation",
         "Track cook time per frame and per patch; alert in the stats when the budget is exceeded.",
         "Ledger.Terrain.Stats reports cook time distribution, not just the worst case.", 2, ["SS6.8"]),
    task("Save generated terrain material as a real asset",
         "The material is built at runtime and therefore editor-only. Write an editor commandlet "
         "that builds it once and saves it to /Game/Materials.",
         "A packaged build renders the terrain correctly with no runtime shader compilation.",
         3, ["SS9"]),
    task("Save generated detail textures as assets",
         "Same problem, same fix: generate the mipped textures at cook time.",
         "The packaged build ships the textures; no UTexture2D is created at runtime.", 2),
    task("Package a standalone game build",
         "The game target links but has never been cooked. Get a packaged build running.",
         "Ledger.exe runs the reentry sequence with no editor present.", 3),
    task("Gauntlet performance test for the descent",
         "Automate the orbit-to-ground descent as a Gauntlet test that records frame times.",
         "The test fails if any frame exceeds 20 ms or the 99th percentile exceeds 16.6 ms.",
         3, ["SS11"]),
    task("Terrain unit tests for the maths layer",
         "LedgerTerrainMath has no tests. Cover cube-sphere round-tripping, noise determinism, "
         "derivative correctness against finite differences, and screen-space error.",
         "Analytic derivatives agree with central differences to 1e-6 over 10,000 samples.",
         3, ["SS11", "SS8.1"]),
    task("Deterministic terrain across machines",
         "The terrain is a pure function of a seed, so two machines must agree. Hash a grid of "
         "elevation samples and compare in CI.",
         "The elevation hash is identical on two machines and across two builds.", 2, ["SS5.2"]),
    task("Profile and reduce per-patch generation cost",
         "Erosion doubled the sample cost. Profile the noise inner loop and cut it: cache "
         "gradients, hoist the hash, consider SIMD.",
         "Patch generation falls below 4 ms for a 65x65 patch with erosion on.", 4),
    task("Bound the mesh component pool by memory",
         "The pool is a fixed 2,560 components. Size it against a memory budget and report when "
         "it is the limiting factor.",
         "Stats show whether the pool or the job queue is the binding constraint.", 2),
    task("Horizon culling correctness test",
         "Culling silently disabled itself once already. Add a test that asserts the visible set "
         "matches a brute-force horizon check at many altitudes.",
         "Culled set matches brute force at 12 altitudes from surface to 3 radii.", 2, ["SS8.1"]),
    task("Terrain streaming under a moving observer, soak test",
         "Fly a great-circle route for 30 minutes and assert no leak, no starvation, no crack.",
         "Memory is flat after the first orbit; zero starved patches; zero cracks.", 3),
    task("M1 evidence: transect flight recording",
         "Capture the 200 km transect with frame times overlaid.",
         "Video and frame-time trace uploaded as milestone evidence.", 1),
]

# ---------------------------------------------------------------- M2
M2 = [
    task("Biome classification from climate rather than altitude",
         "Colour is keyed to height and slope. Derive latitude, temperature and moisture from "
         "position and circulation, then classify biome from those.",
         "A desert appears in a rain shadow, not at a fixed altitude band.", 4, ["SS6.8"]),
    task("Author a texture set per biome",
         "Albedo, normal, roughness and height for rock, sand, grass, forest floor, snow.",
         "Five biomes, each with four maps, all generated procedurally and cooked as assets.", 5),
    task("Height-blended material layering",
         "Blend biomes by their height maps rather than by linear interpolation, so transitions "
         "interlock instead of dissolving.",
         "A grass-to-rock transition shows grass in the crevices and rock on the high points.",
         3, ["SS6.8"]),
    task("Parallax occlusion on the near LOD",
         "Give close ground depth without geometry, faded out past 30 m.",
         "Standing still, the ground has relief below the vertex scale; no cost past 30 m.", 3),
    task("Distance-based macro variation",
         "Break the tiling that shows up from the air with a low-frequency colour and roughness "
         "modulation driven by the same noise the terrain uses.",
         "From 2 km, no repeating pattern is visible anywhere in frame.", 3),
    task("Triplanar seam reduction on steep slopes",
         "The three projections blend by normal; sharpen the blend so cliffs do not smear.",
         "A vertical cliff face shows one dominant projection, not three overlapping.", 2),
    task("Material LOD continuity across patch splits",
         "A patch split must not change material identity. Drive everything from world position, "
         "never from patch UVs.",
         "Recording a split at 60 fps shows no material change on the frame the split happens.",
         2, ["SS6.8"]),
    task("Runtime Virtual Texture evaluation for the surface",
         "Design SS6.8 proposes RVT. Prototype six RVT volumes on the cube faces and measure "
         "against the current triplanar path.",
         "An ADR recording whether RVT beats triplanar here, with measurements.", 4, ["SS6.8", "SS8.5"]),
    task("Snow and sand accumulation by slope and exposure",
         "Deposit on shallow slopes and lee faces, expose rock on steep and windward.",
         "Snow lies in gullies and off cliff faces without being authored.", 3),
    task("Wetness near the waterline and in valley floors",
         "Darken albedo and drop roughness where drainage accumulates.",
         "River valleys read as wet without a water mesh in them.", 2),
    task("Ambient occlusion baked into vertex colour",
         "Approximate AO from the local heightfield curvature at generation time.",
         "Valleys and crevices darken correctly with no screen-space cost.", 3),
    task("Scatter rocks and vegetation by biome",
         "Instanced meshes placed from the same deterministic rules the biome uses.",
         "Ten thousand instances within 500 m at no measurable frame cost.", 4),
    task("Procedural rock and plant meshes",
         "Extend the mesh builder: rocks by displaced icosphere, shrubs and grass by billboard "
         "clusters.",
         "Six rock variants and four plant variants, all generated from a seed.", 4, ["SS6.9"]),
    task("Vegetation wind animation",
         "World-position-offset in the material, driven by a global wind vector.",
         "Grass and canopies move coherently; nothing is animated per-instance on the CPU.", 2),
    task("Material parameter collection for global look",
         "One collection holding wind, wetness, season and time of day, so the whole surface can "
         "be retuned without touching every material.",
         "Changing one parameter changes the whole planet's look consistently.", 2),
    task("Surface fidelity soak test",
         "Fly a route through every biome at three altitudes and capture stills.",
         "Fifteen stills, no tiling, no shimmer, no material identity change.", 2),
    task("M2 evidence: biome gallery",
         "Capture each biome at walking distance, at 200 m and from orbit.",
         "Gallery uploaded as milestone evidence.", 1),
]

# ---------------------------------------------------------------- M3
M3 = [
    task("Parametric ship kit with typed socket interfaces",
         "Design SS6.9's ~40 components. Hull sections, wings, nacelles, gear, sensors, each "
         "with a typed socket, assembled at editor time and baked to StaticMesh.",
         "Three hulls generated from the same kit by changing parameters only.", 5, ["SS6.9"]),
    task("Editor tooling for ship assembly",
         "A commandlet that builds a hull from a parameter set and saves the asset.",
         "Changing hull_length from 24 m to 18 m regenerates in seconds.", 3, ["SS6.9"]),
    task("Cockpit interior and first-person view",
         "Cockpit-only interiors per SS6.9. Seat, canopy frame, instrument panel geometry.",
         "The cockpit view shows a frame and panel that occlude correctly against the world.",
         4, ["SS6.9"]),
    task("Flight HUD: attitude, velocity, altitude, throttle",
         "Drawn in C++ via Slate or a HUD class, no UMG assets.",
         "Altitude reads correctly from orbit to ground; the horizon indicator is right at every "
         "point on the sphere.", 4),
    task("Navigation and target reticle",
         "A marker for the selected destination that works in a spherical frame.",
         "The town marker stays correct while flying around the planet.", 3),
    task("Landing gear with deployment and ground contact",
         "Gear geometry, animation, and per-leg ground contact rather than a single point.",
         "The ship rests level on a slope, on three legs, without sinking.", 4),
    task("Fuel and thrust budget",
         "Delta-v as a resource. Burn on main thrust, drain faster in atmosphere.",
         "Reaching orbit from sea level consumes a legible fraction of a full tank.", 3),
    task("Atmospheric heating on re-entry",
         "Heat accumulation from velocity and density, with a visual effect and a damage "
         "threshold.",
         "A steep fast re-entry damages the hull; a shallow one does not.", 3),
    task("Hull damage model",
         "Component-level damage: engines, control surfaces, hull integrity.",
         "Losing an engine changes the flight model asymmetrically.", 4),
    task("Flight assist modes",
         "Full 6-DOF for vacuum, an atmospheric assist that holds attitude against gravity.",
         "Assist on, the ship holds a hover; assist off, it does not.", 3, ["SS6.9"]),
    task("Ship-relative camera modes",
         "Chase, cockpit, and an external orbit camera, all correct in a spherical frame.",
         "No camera mode gimbal-locks at any point on the planet.", 2),
    task("Engine and thruster visual effects",
         "Niagara plumes scaled by throttle, with atmospheric versus vacuum behaviour.",
         "Plumes shorten and widen in atmosphere and go pencil-thin in vacuum.", 3),
    task("Sound: engines, atmosphere, and its absence",
         "Procedural engine tone by throttle, wind noise by dynamic pressure, silence in vacuum.",
         "Crossing the Karman line, wind noise fades to nothing while the engine stays.", 3),
    task("Docking and landing pad interaction",
         "Detect a valid landing, lock the ship, and expose an interaction prompt.",
         "Landing on the pad within tolerance registers; landing beside it does not.", 3),
    task("Ship save and restore state",
         "Position, orientation, velocity, fuel and damage persist across a session boundary.",
         "Quit in orbit, restart, and the ship is where it was, in the state it was.", 2),
    task("Flight model unit tests",
         "Gravity, drag and thrust as pure functions, tested without a world.",
         "Orbital velocity at 200 km matches the analytic value to within 1%.", 3, ["SS8.1"]),
    task("M3 evidence: cockpit round trip",
         "Record a full pad-to-orbit-to-pad flight from the cockpit view.",
         "Recording uploaded as milestone evidence.", 1),
]
