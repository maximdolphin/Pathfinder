# -*- coding: utf-8 -*-
"""Roadmap content for LEDGER. Milestones, plus M00-M01.

Two rewrites are folded into this plan.

The first: framework-first instead of feature-first. The prototype answered the
hard rendering question and left behind three god-modules, and a system that has
to grow for years cannot be grown out of those. See ARCH SS0.

The second: **the technical model comes before the simulation.** Ships, flight,
planets, travel, physics, bodies and rendering are built to a standard that
holds up next to Star Citizen, and only then does the living-world simulation go
on top. The reasoning is that the technical model is the part that can fail
outright -- seamless travel, local physics grids and planet-scale streaming are
each capable of being impossible -- and there is no point simulating a galaxy
that cannot be flown through.

On the comparison, plainly: Star Citizen has had hundreds of engineers for over
a decade. What is targeted here is the *technical model* at that standard, with
a fraction of the content breadth. One system done properly beats thirty done
like a student project, and the plan is built that way.

The total below is about four years. That is what this scope costs with one
engineer, and a schedule that flatters itself is worse than no schedule.

References:
  LW SSn    docs/design/living-world.md
  ARCH SSn  docs/architecture/module-map.md
  SSn       docs/design/ledger-design.md
"""

MILESTONES = [
    dict(
        id="M00", title="Prototype spike (delivered)", week_start=-6, week_end=0,
        goal="Answer the two questions that could have killed the project: can a "
             "deterministic belief simulation be made legible, and can one engineer "
             "render a real-scale planet you fly to and from without a loading screen.",
        gate="Both answered yes. out/world-dump.txt traces a corporate collapse back "
             "four causes and shows two NPCs disagreeing for legible reasons; the client "
             "flies orbit to ground to orbit continuously, with water, weather and a town.",
        refs=["SS13.1", "SS6.8", "SS15.1"],
    ),
    dict(
        id="M01", title="Architecture and the split", week_start=1, week_end=4,
        goal="Turn the spike into a structure before building anything complex on it. "
             "Cargo workspace, Unreal modules, the god-files split along real seams, and "
             "a build that refuses to compile a layering violation.",
        gate="No source file over 500 lines; every UE module declares three or fewer "
             "dependencies except LedgerGame; adding an illegal dependency fails CI with "
             "a named rule; the scripted flight renders identically to before the split.",
        refs=["ARCH SS2", "ARCH SS3", "ARCH SS5"],
    ),
    dict(
        # The id is not sequential, and that is deliberate. Task ids are issued
        # against a key that begins with the milestone id, so renumbering M03
        # onward would orphan every task id after M02 and reissue three hundred
        # of them -- including the ones already referenced in commit messages
        # and on the published roadmap. A non-sequential id costs a line of
        # explanation; renumbering costs the project's own references to itself.
        id="M2W", title="The world as an Unreal project", week_start=5, week_end=9,
        # Moved ahead of the rest of M02 on 2026-09-09. Four things were true
        # and all of them got worse with every terrain task added on top:
        # nothing packaged, the frame budget was failing on the game thread
        # that M02's remaining work adds to, Nanite was unavailable to every
        # object in the world, and four of M02's own tasks produce assets
        # that would have had to be rebuilt against ADR-0006 afterwards.
        # The cost is five weeks before the world looks better; the terrain
        # *shape* tasks -- climate, biomes, cliffs, hydrology, caves -- do
        # not depend on this and can interleave if that matters more.
        goal="Stop being a tech demo that only runs inside the editor. A real level, "
             "materials that are assets rather than node graphs rebuilt on every launch, "
             "and a mechanism that turns a generator into a saved asset with Nanite, "
             "LODs, collision and instancing -- so that everything generated after this "
             "is a first-class Unreal asset and not a procedural mesh with none of it.",
        gate="A packaged build, with no editor present, flies the scripted flight and "
             "renders within the perceptual tolerance of the editor build.",
        refs=["ARCH Rule 1", "SS14"],
    ),
    dict(
        id="M02", title="Terrain to production standard", week_start=10, week_end=21,
        goal="The terrain stops being a spike. Stable LOD with no popping, collision "
             "wherever a body can be, caves and overhangs, biome-driven material blending, "
             "and a streaming budget that holds at speed.",
        gate="Fly a 200 km transect at 300 m and at 900 m/s: no holes, no popping that "
             "reads as a seam, no frame over 16 ms, and collision present under the ship "
             "at every point along it.",
        refs=["SS6.8"],
    ),
    dict(
        # Inserted 2026-09-09 and everything after it moved seven weeks. The
        # ground was measured -- 4.77 m triangles, a height field with nothing
        # under 33 m, height maps sampled and thrown away -- and the terrain
        # milestone's own gate says nothing about whether the surface is worth
        # standing on. See scripts/roadmap_data_surface.py for what "on par with
        # Star Citizen" was taken to mean, and for the part that is already
        # equal: the scans themselves.
        id="M2S", title="Close-range surface fidelity", week_start=22, week_end=28,
        goal="Make the ground convincing at walking distance and out to the horizon. "
             "The source scans are already photogrammetry at 4K; what is missing is "
             "displacement, variation across scales, an end to the visible tiling grid, "
             "and geometry fine enough to carry any of it.",
        gate="Twelve side-by-side pairs against real photographs at 2 m, 20 m and 200 m, "
             "each with a written verdict; no terrain triangle edge over 40 px while "
             "standing; no visible tiling period in an overhead capture; and the 200 km "
             "transect still holds its frame budget.",
        refs=["SS6.8", "QB", "ADR-0005"],
    ),
    dict(
        id="M03", title="Planetary bodies and orbital mechanics", week_start=29, week_end=36,
        goal="A solar system that moves. Planets, moons, rings and stations on real "
             "orbits, axial tilt, rotation, day and night, seasons, eclipses — all "
             "deterministic from seed and all consistent between the map and the sky.",
        gate="Stand on a surface and watch a moon rise, transit and set on the schedule "
             "the orbital model predicts. Fly to it. The ephemeris and the sky agree to "
             "the arcminute.",
        refs=["LW SS8", "SS6.8"],
    ),
    dict(
        id="M04", title="Atmosphere, weather and environment", week_start=37, week_end=44,
        goal="Air that behaves. Layered atmospheres per body, volumetric weather that "
             "moves and matters, storms with real wind fields, temperature and pressure "
             "as physical quantities the rest of the game reads.",
        gate="Fly into a storm front: visibility, wind loading on the ship and cockpit "
             "audio all change together, and the same storm is visible from orbit in the "
             "place the weather model puts it.",
        refs=["SS6.8", "LW SS7.2"],
    ),
    dict(
        id="M05", title="Ship framework: hulls, components, subsystems", week_start=45, week_end=54,
        goal="Ships as systems, not props. A component graph — power plant, thrusters, "
             "fuel, cooling, avionics, life support, shields — with real dependencies, "
             "damage that propagates through it, and hulls assembled from parts.",
        gate="Shoot out a power coupling and watch thrusters on that bus go dead, heat "
             "climb, and the affected systems degrade in an order the component graph "
             "explains. Repair it and the ship comes back.",
        refs=["SS6.9"],
    ),
    dict(
        # Added 2026-09-11 after the owner's first playtest: the ship could not be
        # flown, the picture was blurred, the engine hissed, and the session ended
        # in a divergence. See roadmap_data_play.py.
        id="M5P", title="Playability: a build you can fly", week_start=55, week_end=57,
        goal="A build a person can sit down and fly for ten minutes without it falling "
             "apart: a crisp picture, a ship that answers the stick and stays in one "
             "piece near the ground, an engine that sounds like one, and no freeze -- "
             "checked every time by an offscreen playtest that flies the ship the way a "
             "player does.",
        gate="A scripted offscreen playtest -- off the pad, out over the town and back, "
             "down to a hover -- runs with no divergence, no ensure and no crash, holds "
             "the frame budget, and captures a crisp picture; and the owner flies the same "
             "build and signs it off.",
        refs=["SS6.9", "ARCH Rule 6"],
    ),
    dict(
        id="M06", title="Flight model in vacuum and in air", week_start=55, week_end=63,
        goal="Flight worth doing. Six-degree thruster allocation solved rather than "
             "faked, aerodynamic lift and drag in atmosphere, control surfaces, landing "
             "gear, gravity, and the transition between the two regimes.",
        gate="Take off from a pad, fly aerodynamically to another continent, exit the "
             "atmosphere, manoeuvre in vacuum, re-enter with heating and buffeting, and "
             "land on the pad you left. Cockpit view and instruments only.",
        refs=["SS6.9"],
    ),
    dict(
        id="M07", title="Seamless travel across a system", week_start=64, week_end=72,
        goal="Distance made crossable without a loading screen. Origin rebasing, "
             "multi-scale rendering from centimetres to astronomical units, and a "
             "high-speed travel mode with a real spool, a real interruption, and a real "
             "cost.",
        gate="Fly from a planet's surface to a moon of another planet in one continuous "
             "shot, with no loading screen, no cut, and no precision artefact anywhere "
             "along the path.",
        refs=["LW SS8", "SS6.8"],
    ),
    dict(
        id="M08", title="Physics at scale and local grids", week_start=73, week_end=81,
        goal="The hardest engine problem in the project: walking around inside a ship "
             "that is itself moving at speed near a rotating planet. Nested reference "
             "frames, which Chaos does not have.",
        gate="Walk from the back of a ship to its cockpit while it accelerates, rolls and "
             "flies through atmosphere. Set a cup down and it stays on the table. Step "
             "out onto a landing pad on a rotating planet and inherit the right frame.",
        refs=["SS6.9"],
    ),
    dict(
        id="M09", title="Embodiment and animation", week_start=82, week_end=92,
        goal="A body worth being in. Character controller with real momentum, full-body "
             "IK, procedural foot and hand placement, first and third person from one "
             "skeleton, interaction, seats, ladders, EVA.",
        gate="Walk out of a room, climb a ladder, cross a pad in wind, board a ship, take "
             "a seat, fly, and get out again — one continuous shot, no cut, no teleport, "
             "no animation that slides.",
        refs=["SS6.9", "LW SS4"],
    ),
    dict(
        id="M10", title="Interiors and modular architecture", week_start=93, week_end=101,
        goal="Insides. Ship interiors, station interiors and building interiors built "
             "from a modular kit, generated where they should be generated and authored "
             "where they should be authored, with no door that is a loading screen.",
        gate="Walk from a city street into a building, up through it, out onto a roof pad, "
             "into a docked ship and through it to the cockpit, with no load, no fade, and "
             "no drop below the frame budget.",
        refs=["LW SS7", "SS6.8"],
    ),
    dict(
        id="M11", title="Rendering fidelity", week_start=102, week_end=112,
        goal="The look. A real PBR material pipeline, lighting that works from a lit "
             "cockpit at night to a sunlit dune, volumetrics, decals, wear, post, and the "
             "LOD and impostor chain that lets it survive at range.",
        gate="Side by side against reference footage at three distances and three lighting "
             "conditions, a stranger cannot immediately sort ours from theirs on material "
             "and lighting alone.",
        refs=["SS6.8", "SS14"],
    ),
    dict(
        id="M12", title="Asset pipeline and world content", week_start=113, week_end=121,
        goal="A way to get content in that is not a person placing things by hand -- "
             "and under ADR-0005 there is no other way, because nothing is bought and no "
             "artist is assumed. Generators for hulls, plating, surface detail, markings, "
             "buildings, settlements and biomes; validation, LOD generation and material "
             "assignment over their output; and the variation framework that stops all of "
             "it reading as the same thing repeated.",
        gate="Add a new ship and a new biome end to end through the pipeline, with no "
             "manual step outside the tool, and both appear correctly at every LOD.",
        refs=["SS14"],
    ),
    dict(
        id="M13", title="Performance and optimisation", week_start=122, week_end=129,
        goal="Budgets met with everything running at once. Rendering, streaming, physics "
             "and memory, at the densities the design actually asks for.",
        gate="Sixty frames a second with a full city, a docked ship interior and traffic "
             "in view, no frame over 16 ms, and a two-hour session with no memory growth.",
        refs=["SS14"],
    ),
    dict(
        id="M14", title="Technical vertical slice", week_start=130, week_end=137,
        goal="Prove the model. One system, one planet, one moon, one city, one station, "
             "three ships — everything above running together and holding up under an "
             "hour of unscripted play.",
        gate="An hour of free play with no loading screen, no crash, no frame over 16 ms, "
             "and nothing that reads as placeholder. This is the milestone the whole "
             "technical plan exists to reach.",
        refs=["ARCH Rule 6"],
    ),
    dict(
        id="M15", title="Simulation core and determinism", week_start=138, week_end=145,
        goal="The substrate the living world is written against: event log, reducer "
             "contract, snapshots, replay, and a harness that proves reproducibility "
             "before there is anything complicated to reproduce.",
        gate="Replay a 100,000-tick log twice from one seed and diff the snapshots byte "
             "for byte. CI fails on divergence and names the first differing tick.",
        refs=["SS3", "ARCH Rule 5"],
    ),
    dict(
        id="M16", title="Agent framework", week_start=146, week_end=157,
        goal="The longest simulation milestone, and the one every other system reduces "
             "to. Needs, goals, planning, schedules, competence, loyalty — and agents "
             "that act on their own beliefs rather than on world state.",
        gate="Run 500 agents for 30 simulated days. Any one agent's whole history is "
             "explained by its needs and its beliefs, with no appeal to world state it "
             "never observed, and two agents holding different beliefs act differently.",
        refs=["LW SS6", "SS4"],
    ),
    dict(
        id="M17", title="Organisation framework", week_start=158, week_end=165,
        goal="Membership, roles, holdings, treasury, doctrine and span of control. A "
             "player organisation and an NPC organisation are the same type, founded "
             "through the same API, subject to the same drift.",
        gate="Over-extend an organisation and watch drift appear as subordinates pursuing "
             "their own goals — with no code path in the org layer that asks whether the "
             "owner is a player.",
        refs=["LW SS2", "ARCH Rule 4"],
    ),
    dict(
        id="M18", title="Economy and logistics", week_start=166, week_end=173,
        goal="Goods, markets, production, contracts and convoys. Materials that have to "
             "physically arrive, which is what turns a build order into a logistics "
             "problem and a convoy into a target.",
        gate="A supply shock in one system moves prices three systems away with a lag that "
             "matches travel time. Ninety days with no negative inventory and no money "
             "created outside a mint event.",
        refs=["SS7", "LW SS7.4"],
    ),
    dict(
        id="M19", title="Sites, construction and city life", week_start=174, week_end=183,
        goal="Claims, construction projects, four utility networks, condition that "
             "degrades, maintenance crews that respond, and a first-person tell for every "
             "mechanic.",
        gate="Build the same habitat archetype on a temperate world and an airless storm "
             "moon: different composition and cost from one definition. Cut power to a "
             "district and watch life support fail, crews respond, and people put masks on.",
        refs=["LW SS7"],
    ),
    dict(
        id="M20", title="Power, threat and coalitions", week_start=184, week_end=191,
        goal="The ceiling. Threat models built on belief rather than truth, alarm that "
             "rises with believed power and falls with dependency, and coalitions that "
             "form because several organisations independently reached one conclusion.",
        gate="Grow an organisation until rivals independently cross alarm and form a "
             "coalition against it. Feed one member a fabricated belief about another and "
             "watch the coalition fracture.",
        refs=["LW SS3", "SS4"],
    ),
    dict(
        id="M21", title="Missions and directives", week_start=192, week_end=200,
        goal="Missions generated from tension rather than authored, with stakes that are "
             "changes to the world. Directives: standing orders resolved by agents whether "
             "or not anyone is watching.",
        gate="Every generated mission traces to a named tension between real parties and "
             "none has a stake that is only money. A directive left alone for a simulated "
             "week resolves through subordinates, correctly or otherwise.",
        refs=["LW SS5", "LW SS4.1"],
    ),
    dict(
        id="M22", title="Presence and Command", week_start=201, week_end=207,
        goal="The second lens. A map of what your organisation is doing, built from what "
             "your people have reported rather than from ground truth, with directives "
             "issued from it and your body still standing where you left it.",
        gate="Open Command mid-flight: the world does not pause and the ship does not "
             "stop. A report three days stale is visibly stale, and a fact nobody has "
             "observed does not appear at all.",
        refs=["LW SS4"],
    ),
    dict(
        id="M23", title="The living world, integrated", week_start=208, week_end=216,
        goal="Everything at once, for a long time, without supervision. This is where the "
             "frameworks either compose or they do not.",
        gate="Run one simulated year unattended. No runaway monopoly, no dead economy, no "
             "organisation with a thousand ships, no agent stuck in a loop, and a dump "
             "that still explains itself.",
        refs=["LW SS3", "SS12"],
    ),
    dict(
        id="M24", title="Vertical slice: nothing to coalition", week_start=217, week_end=227,
        goal="The game. Arrive with nothing, take work, build a crew, charter an "
             "organisation, claim ground, build on it, and grow until the galaxy decides "
             "you are the problem.",
        gate="A player who has never seen the game goes from nothing to a chartered "
             "organisation to a coalition forming against them, in one save, with no "
             "tutorial explaining any of the systems.",
        refs=["LW SS1", "LW SS3"],
    ),
]


# ---------------------------------------------------------------------------
# M00 - the prototype spike. Already delivered; recorded so the burn-up starts
# from the truth and the evidence attached to these tasks survives the rewrite.
# Titles stay in this order: the ids are what the evidence refers to.
# ---------------------------------------------------------------------------

M00 = [
    dict(title="Rust workspace and CI-ready sim crate",
         detail="Cargo project, zero dependencies, module skeleton.",
         acceptance="cargo test runs green from a clean checkout.",
         days=1, refs=["SS9"]),
    dict(title="Fixed-point arithmetic with no floats in authoritative state",
         detail="i64 fixed-point type with the operators the sim needs.",
         acceptance="No f32 or f64 appears in any authoritative struct.",
         days=1.5, refs=["SS3"]),
    dict(title="Deterministic PRNG seeded per region per tick",
         detail="SplitMix64 seeded from (world_seed, region_id, tick), no shared state.",
         acceptance="Two processes produce identical streams for the same seed triple.",
         days=1, refs=["SS3"]),
    dict(title="Event log and the fold",
         detail="State_n = fold(reduce, State_0, events[0..n]).",
         acceptance="Replaying the log reproduces the state exactly.",
         days=2, refs=["SS3"]),
    dict(title="Belief graph with propagation, decay and distortion",
         detail="Typed propositions, confidence, provenance, hop count.",
         acceptance="A fact propagates with latency and arrives degraded.",
         days=3, refs=["SS4"]),
    dict(title="Disposition computed, never stored",
         detail="Attitude is a function of beliefs and obligations at read time.",
         acceptance="No disposition field exists in any struct.",
         days=1, refs=["SS4"]),
    dict(title="Obligation ledger as a consumed-on-redemption multigraph",
         detail="Favours owed, with expiry and redemption.",
         acceptance="Redeeming an obligation removes it and changes behaviour.",
         days=2, refs=["SS4"]),
    dict(title="Investigation algorithm with search volumes",
         detail="Finding someone is a search over belief, not a lookup.",
         acceptance="An investigation narrows over time and can fail.",
         days=2, refs=["SS4"]),
    dict(title="Economy with conservation laws",
         detail="Goods and money conserved except at explicit mint and sink events.",
         acceptance="A long run conserves both to the unit.",
         days=2, refs=["SS7"]),
    dict(title="Mission generation as a query over existing tension",
         detail="Missions are found in the world, not authored.",
         acceptance="Every generated mission names the tension it came from.",
         days=2, refs=["SS8"]),
    dict(title="Text world-dump and the SS12 metrics",
         detail="A readable dump of the running world plus the design's metrics.",
         acceptance="A collapse can be traced back four causes by reading it.",
         days=2, refs=["SS12"]),
    dict(title="Tier 2 and Tier 4 test suites",
         detail="Invariants and diagnostics.",
         acceptance="69 tests green.",
         days=2, refs=["SS13"]),
    dict(title="UE5 C++ client with a typed snapshot boundary",
         detail="No Blueprint, no .umap content; the world spawns itself.",
         acceptance="The client builds and runs with no content assets.",
         days=2, refs=["SS9"]),
    dict(title="Cube-sphere quadtree terrain with screen-space error LOD",
         detail="Six roots, edge-index stitching, horizon culling.",
         acceptance="A planet renders with no cracks at any LOD boundary.",
         days=4, refs=["SS6.8"]),
    dict(title="Move patch generation to worker threads",
         detail="Async over copied inputs; game thread only uploads.",
         acceptance="Generation cost leaves the game thread entirely.",
         days=2, refs=["SS6.8", "ADR-0002"]),
    dict(title="Scale the planet to 6,371 km",
         detail="Real radius under LWC, vertices relative to patch centres.",
         acceptance="Terrain holds together from orbit to a metre off the ground.",
         days=2, refs=["SS6.8"]),
    dict(title="Sky Atmosphere, volumetric clouds, ozone",
         detail="Earth values: Rayleigh 8 km, Mie 1.2 km, ozone layer.",
         acceptance="The sky reads correctly from the ground and from orbit.",
         days=2, refs=["SS6.8"]),
    dict(title="Analytic erosion via slope-damped octaves",
         detail="Exact gradient derivatives; each octave damped by coarser slope.",
         acceptance="Terrain shows drainage, and there are no LOD seams.",
         days=3, refs=["SS6.8"]),
    dict(title="Triplanar surface material from generated mipped textures",
         detail="Hand-built mip chains; the mips are the anti-aliasing fix.",
         acceptance="No shimmer at any distance or angle.",
         days=2, refs=["SS6.8"]),
    dict(title="Ship, gravity, and a town",
         detail="6-DOF flight integrated manually, inverse-square gravity, buildings.",
         acceptance="Fly from orbit to a town and back with no loading screen.",
         days=3, refs=["SS6.9"]),
    dict(title="ADR-0001 and ADR-0002",
         detail="Stack and phasing; terrain build versus buy.",
         acceptance="Both accepted and dated.",
         days=1, refs=["SS15.1"]),
    dict(title="Water surface as a separate render pass",
         detail="Sea as section 1 of each patch, depth-tinted, opaque for SSR.",
         acceptance="A shoreline reads as a surface with something under it.",
         days=2, refs=["SS6.8"]),
    dict(title="Wave displacement and shoreline foam",
         detail="Crossed Gerstner-style trains, damped in the shallows.",
         acceptance="Waves are visible from 200 m and flatten against the shore.",
         days=1, refs=["SS6.8"]),
    dict(title="Underwater fog and camera transition",
         detail="Post-process murk keyed to scene depth, eased on the crossing.",
         acceptance="Entering the sea transitions without a pop or a flash.",
         days=0.5, refs=["SS6.8"]),
    dict(title="Ocean-floor terrain profile",
         detail="Shelf, break, slope, abyssal plain.",
         acceptance="A transect from coast to deep ocean shows a shelf break.",
         days=0.5, refs=["SS6.8"]),
    dict(title="Patch cache keyed by node id",
         detail="LRU over released patches, bounded by bytes.",
         acceptance="Flying back and forth over a ridge regenerates almost nothing.",
         days=1, refs=["SS6.8"]),
    dict(title="Predictive patch prefetch along the velocity vector",
         detail="LOD leads the camera. Descent holes fixed; the ascent path regressed and "
                "is carried into M01 as the collapse-cascade task.",
         acceptance="A 300 m/s descent shows no unfilled patches at any point.",
         days=0.5, refs=["SS6.8"]),
]


# ---------------------------------------------------------------------------
# M01 - architecture and the split. Deliberately short: it is the cheapest it
# will ever be to do this, and everything after it is built on the result.
# ---------------------------------------------------------------------------

M01 = [
    dict(title="Fix the collapse cascade the prefetch work exposed",
         detail="A fast ascent wants to collapse many levels at once. Each pending node "
                "keeps its four children and also requests its own patch, so the visible "
                "set inflates exactly when it should shrink: 6,029 nodes, 2,429 starved, "
                "1,444 holes. Collapse has to resolve bottom-up within a frame budget.",
         acceptance="A full-thrust climb from ground to orbit reports zero holes and a "
                    "visible set that falls monotonically.",
         days=2, refs=["SS6.8"]),
    dict(title="Frame-time recording in the scripted flight",
         detail="Every run writes out/performance.txt: per-phase mean, median, p95, p99 "
                "and max, with game, render and GPU thread time beside them, and a verdict "
                "against the budget. A screenshot has no frame time in it, which is how a "
                "run at nineteen frames a second passed twenty-six task reviews.",
         acceptance="Every scripted run produces a report with a PASS or FAIL verdict, and "
                    "a stall over 33 ms is logged with its timestamp and phase.",
         days=1, refs=["SS14"]),
    dict(title="Cut the volumetric cloud cost",
         detail="ProfileGPU at the worst point put VolumetricCloud at 90 ms of a 130 ms "
                "frame — 68 percent — from a sample scale of 2.0 against a 400 km tracing "
                "distance. Twice the engine's samples over eight times its distance.",
         acceptance="Cloud cost falls below 15 ms at ground level with no visible change in "
                    "the deck from a kilometre up.",
         days=0.5, refs=["SS14", "SS6.8"]),
    dict(title="Unreal module split: LedgerCore, LedgerTerrain, LedgerMaterial",
         detail="Real UE modules with Build.cs files rather than folders, so UBT enforces "
                "the dependency declarations instead of a convention doing it.",
         acceptance="The three modules build and LedgerTerrain cannot see LedgerGame.",
         days=2, refs=["ARCH SS3"]),
    dict(title="Break up LedgerPlanet.cpp",
         detail="1,226 lines doing quadtree, LOD policy, patch jobs, section pool, cache, "
                "stats and console commands. Five files plus a diagnostics file.",
         acceptance="No file over 500 lines and the scripted flight renders identically.",
         days=3, refs=["ARCH SS3.1"]),
    dict(title="Break up LedgerSurface.cpp into one file per material",
         detail="Terrain, water, underwater and flat, with the graph-building helper as a "
                "shared header.",
         acceptance="Each material is its own translation unit and all four compile at "
                    "runtime with no default-material substitution.",
         days=1.5, refs=["ARCH SS3.1"]),
    dict(title="Extract the scripted flight into a development-only harness module",
         detail="LedgerWorld.cpp is 1,002 lines, most of it a camera script and screenshot "
                "capture — a test fixture living in production code.",
         acceptance="A shipping configuration builds with the harness module absent.",
         days=2, refs=["ARCH SS3.1"]),
    dict(title="Split the ship into LedgerFlight and LedgerPawn",
         detail="The flight model has no business knowing about pawns, cameras or input, "
                "and it is about to get much bigger.",
         acceptance="The flight model is unit-testable with no Unreal actor involved.",
         days=1.5, refs=["ARCH SS3.1"]),
    dict(title="Module dependency test for the Unreal side",
         detail="A build step asserting each module's declared dependencies against the "
                "allowed list, with LedgerGame the only exception.",
         acceptance="Adding an undeclared dependency fails the build with a named rule.",
         days=1, refs=["ARCH Rule 2"]),
    dict(title="Freeze the Phase 0 simulation as a spike",
         detail="Fifteen flat files, one of them 1,430 lines. Splitting it into the ARCH "
                "SS2 crates was planned here and is withdrawn: it is four days of "
                "restructuring code that gets rewritten in M15, against a design that has "
                "already changed underneath it — organisations, the power ceiling, "
                "coalitions, sites and directives all postdate it. Label it frozen, record "
                "what is worth keeping from it, and check in the snapshot so the client "
                "stops depending on a Rust build.",
         acceptance="src/README.md states the freeze and what survives it; the client runs "
                    "with no Rust toolchain present.",
         days=0.5, refs=["ARCH SS2", "ADR-0003"]),
    dict(title="CI: build the client and run its tests on every commit",
         detail="Unreal automation tests, headless, plus the scripted flight as a smoke "
                "test with image comparison. The frozen sim builds but is not on the "
                "critical path.",
         acceptance="A commit that breaks the client is red within fifteen minutes.",
         days=2.5, refs=["SS13"]),
    dict(title="Automated visual regression on the scripted flight",
         detail="The scripted flight already produces captures. Compare them against "
                "committed references with a perceptual diff so a rendering regression is "
                "caught by CI rather than by eye three weeks later.",
         acceptance="A deliberate one-shade material change fails the comparison; "
                    "recompiling unchanged code does not.",
         days=2, refs=["SS13"]),
    dict(title="Coding standards and formatter configuration",
         detail="Naming, file layout, comment expectations, the 500-line rule, and where "
                "each kind of thing goes. Short enough that it gets read.",
         acceptance="Both halves are formatted by tool and CI fails on unformatted code.",
         days=1, refs=["ARCH SS4"]),
    dict(title="ADR-0003: technical model first, and what it costs",
         detail="Record the decision to build the whole technical model before the "
                "simulation, the reason (seamless travel and local physics grids can fail "
                "outright, and a galaxy that cannot be flown through is worth nothing), and "
                "the risk that the simulation's needs turn out to change the technical "
                "requirements late.",
         acceptance="Accepted and dated, with the risk stated rather than argued away.",
         days=0.5, refs=["ARCH Rule 6"]),
    dict(title="ADR-0004: framework-first inside each milestone",
         detail="Every milestone builds a general mechanism, not a specific instance. The "
                "risk is a year of scaffolding nobody can build a game from; the mitigation "
                "is Rule 6 and nothing else.",
         acceptance="Accepted and dated.",
         days=0.5, refs=["ARCH Rule 6"]),
    dict(title="Playable proof: the flight still flies",
         detail="Rule 6. The full scripted sequence after a structural rewrite, compared "
                "against the captures taken before it.",
         acceptance="Every capture in out/ matches its pre-split reference.",
         days=1, refs=["ARCH Rule 6"]),
]
