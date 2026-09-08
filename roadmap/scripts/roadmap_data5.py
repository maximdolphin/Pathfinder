# -*- coding: utf-8 -*-
"""M12-M14: asset pipeline, performance, and the technical vertical slice.

M14 is the milestone the entire technical plan exists to reach: the point at
which the model is proven and the simulation can be built on top of something
that is known to work.
"""

# ---------------------------------------------------------------------------
# M12 - asset pipeline and world content.
# ---------------------------------------------------------------------------

M12 = [
    dict(title="Asset import and validation pipeline",
         detail="One path in for meshes, textures and rigs, with validation that rejects "
                "what will cause problems later — wrong scale, missing UVs, non-conforming "
                "materials, unreasonable triangle counts.",
         acceptance="A deliberately broken asset is rejected with a message naming the "
                    "problem, not imported and discovered three weeks later.",
         days=4, refs=["SS14"]),
    dict(title="Automatic LOD and collision generation",
         detail="LOD chains and collision proxies generated at import with per-class rules, "
                "because hand-authoring them for hundreds of assets is not a plan.",
         acceptance="An imported asset arrives with a full LOD chain and collision that "
                    "matches its silhouette, with no manual step.",
         days=4, refs=["SS14"]),
    dict(title="Material assignment and texture packing",
         detail="Conventions applied at import: channel packing, compression settings, "
                "streaming priorities, and material instances built from the standard.",
         acceptance="An imported asset renders correctly with no hand-assigned material.",
         days=3, refs=["SS6.8"]),
    dict(title="Procedural building generator",
         detail="Buildings generated from purpose, footprint, environment and owner, "
                "assembling exteriors that match the interiors M10 generates.",
         acceptance="A generated building's exterior openings correspond exactly to its "
                    "interior layout.",
         days=5, refs=["LW SS7.2"]),
    dict(title="Settlement layout generator",
         detail="Districts, streets, utilities and pads laid out from terrain, resources and "
                "purpose, at scales from outpost to city.",
         acceptance="Three settlements of different scale generate on different terrain, "
                    "each navigable and each explicable from its site.",
         days=5, refs=["LW SS7"]),
    dict(title="Environment-driven building composition",
         detail="The LW SS7.2 rule made real in art: an archetype gains sealed envelopes, "
                "anchoring, shielding and thermal plant from the environment it is placed "
                "in.",
         acceptance="One archetype placed on four different worlds produces four visibly "
                    "and structurally different buildings from one definition.",
         days=4, refs=["LW SS7.2"]),
    dict(title="Prop and clutter library with rule-based placement",
         detail="The vocabulary a place is dressed from, plus the rules that place it, so a "
                "generated street is not a repeated tile.",
         acceptance="Two streets generated from the same rules are recognisably the same "
                    "kind of place and not the same place.",
         days=3, refs=["LW SS7.1"]),
    dict(title="Ship art pipeline",
         detail="From model to flyable: hardpoints, component locations, interior kit "
                "attachment, damage states, LODs and materials, as a repeatable process.",
         acceptance="A new ship goes from mesh to flyable with interiors in under a day of "
                    "work.",
         days=4, refs=["SS6.9"]),
    dict(title="Audio asset pipeline and mix framework",
         detail="Import conventions, buses, attenuation curves, occlusion setup and a mix "
                "that holds from a quiet interior to a storm.",
         acceptance="No sound clips the mix, and dialogue-range audio stays legible in the "
                    "loudest scene.",
         days=3, refs=["SS6.9"]),
    dict(title="Content authoring tools",
         detail="In-editor tools for defining systems, bodies, biomes, ships, buildings and "
                "settlements, so content is authored as data by a person rather than typed "
                "as code.",
         acceptance="A complete star system is authored end to end without editing source.",
         days=5, refs=["ARCH Rule 7"]),
    dict(title="Content validation suite",
         detail="Automated checks across all content: missing references, budget violations, "
                "unreachable interiors, invalid configurations.",
         acceptance="Every class of content error seen during this milestone is caught "
                    "automatically thereafter.",
         days=3, refs=["SS13"]),
    dict(title="Asset streaming and memory budgets",
         detail="What is resident, what streams, at what priority, under a hard memory "
                "budget with visible accounting.",
         acceptance="Memory stays inside budget through a two-hour session that visits "
                    "every content type.",
         days=4, refs=["SS14"]),
    dict(title="Cook and package pipeline",
         detail="A packaged build that actually runs — including the runtime-generated "
                "materials the prototype relies on the editor for, which will not survive a "
                "cook as they stand.",
         acceptance="A packaged build runs the full technical slice with no editor present.",
         days=5, refs=["SS9"]),
    dict(title="Playable proof: new ship and new biome end to end",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Both added through the pipeline with no manual step outside the tool, "
                    "and both correct at every LOD.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M13 - performance and optimisation.
# ---------------------------------------------------------------------------

M13 = [
    dict(title="Frame budget definition and enforcement",
         detail="A published budget per subsystem, instrumented, with CI failing a build "
                "that exceeds it. A budget nobody measures is a wish.",
         acceptance="The budget is enforced automatically and a deliberate regression fails "
                    "the build.",
         days=3, refs=["SS14"]),
    dict(title="Automated performance capture across scenarios",
         detail="Fixed scenarios — city, cockpit, orbit, storm, combat — captured every "
                "build with frame time distributions rather than averages.",
         acceptance="Per-build performance history is queryable and a regression is "
                    "attributable to a commit.",
         days=3, refs=["SS14"]),
    dict(title="Draw call and instancing pass",
         detail="Batching, instancing and merging across terrain, scatter, buildings and "
                "props. The prototype's section pool alone reached thousands of components.",
         acceptance="Draw calls in the densest scene fall below the budget with no visual "
                    "change.",
         days=4, refs=["SS14"]),
    dict(title="Culling: occlusion, distance and relevance",
         detail="A city with interiors is mostly invisible from anywhere in it, and the "
                "renderer should know that.",
         acceptance="Visible triangle count in a city drops by an order of magnitude with "
                    "no visible change.",
         days=4, refs=["SS14"]),
    dict(title="Game thread cost reduction",
         detail="Tick consolidation, work distribution and removal of per-frame work that "
                "does not need to happen per frame.",
         acceptance="Game thread stays under its budget in the worst scenario.",
         days=4, refs=["SS14"]),
    dict(title="Worker thread utilisation",
         detail="Terrain, physics, animation, streaming and audio across cores without "
                "contention or priority inversion.",
         acceptance="Core utilisation is even under load and no worker starves the game "
                    "thread.",
         days=3, refs=["SS14"]),
    dict(title="GPU profiling and shader cost pass",
         detail="Per-pass GPU cost measured and reduced, with shader complexity budgets by "
                "material class.",
         acceptance="Every render pass is inside its budget in the worst scenario.",
         days=4, refs=["SS14"]),
    dict(title="Memory: fragmentation, leaks and long sessions",
         detail="Two-hour sessions with no growth, no fragmentation-driven stalls, and pools "
                "sized from measurement.",
         acceptance="A two-hour session ends with the memory profile it started with.",
         days=3, refs=["SS14"]),
    dict(title="Load and streaming hitch elimination",
         detail="Every remaining stall over 4 ms hunted down. Seamlessness is destroyed by "
                "one hitch and the player remembers only the hitch.",
         acceptance="A full traversal of the slice shows no frame over 16 ms.",
         days=4, refs=["SS14"]),
    dict(title="Scalability settings",
         detail="Quality levels that degrade gracefully across a range of hardware, each "
                "measured rather than assumed.",
         acceptance="Three quality levels are measured on three hardware profiles and each "
                    "hits its target frame rate.",
         days=3, refs=["SS14"]),
    dict(title="Playable proof: the density test",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Sixty frames a second with a full city, a docked ship interior and "
                    "traffic in view, no frame over 16 ms, no memory growth over two hours.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M14 - technical vertical slice.
# ---------------------------------------------------------------------------

M14 = [
    dict(title="Choose and build the slice system",
         detail="One star, one habitable planet, one moon, one gas giant, one asteroid "
                "field. Authored through the M12 tools, not by hand.",
         acceptance="The system is complete, plausible and generated reproducibly from its "
                    "authored definition.",
         days=4, refs=["LW SS8"]),
    dict(title="Build the slice city",
         detail="One city on the habitable planet: districts, walkable interiors, pads, "
                "utilities visible, at a scale that holds up.",
         acceptance="The city is walkable end to end with every building enterable and the "
                    "frame budget held throughout.",
         days=5, refs=["LW SS7"]),
    dict(title="Build the slice station",
         detail="One orbital station with spin gravity, docking, interiors and traffic.",
         acceptance="Dock, disembark, cross the station in spin gravity, and depart, all in "
                    "correct frames.",
         days=4, refs=["SS6.9"]),
    dict(title="Build the outpost on the hostile moon",
         detail="The environment-driven composition proof: an airless, storm-scoured site "
                "with sealed, anchored, shielded buildings and visible life support.",
         acceptance="The same archetypes as the city produce visibly different buildings, "
                    "from environment data alone.",
         days=4, refs=["LW SS7.2"]),
    dict(title="Three ships to production standard",
         detail="A courier, a hauler and a fighter: full component graphs, interiors, "
                "damage states, instruments and audio.",
         acceptance="All three are flyable, walkable, damageable and repairable, with "
                    "handling differences that come from their configurations.",
         days=6, refs=["SS6.9"]),
    dict(title="Placeholder purge",
         detail="Every remaining grey box, untextured mesh, programmer material and "
                "temporary sound, found and replaced or removed.",
         acceptance="An automated audit reports zero placeholder assets in the slice.",
         days=4, refs=["SS14"]),
    dict(title="Traversal and interaction polish pass",
         detail="Every transition in the slice — door, ladder, seat, airlock, dock, frame "
                "change — reviewed and made to feel deliberate rather than tolerated.",
         acceptance="A reviewer completes every transition in the slice and reports none as "
                    "rough.",
         days=4, refs=["SS6.9"]),
    dict(title="Stability soak",
         detail="Long unattended and attended sessions hunting crashes, leaks, drift and "
                "state corruption.",
         acceptance="Ten hours of mixed play with no crash and no state corruption.",
         days=4, refs=["SS13"]),
    dict(title="External playtest",
         detail="People who have not seen it, playing without instruction, watched. The "
                "first honest read on whether the technical model reads as a place.",
         acceptance="Five testers complete a free-play session and their observations are "
                    "recorded and triaged.",
         days=3, refs=["SS13"]),
    dict(title="Technical slice review and ADR",
         detail="What the model proved, what it did not, and what the simulation layer must "
                "now be built against. The decision point the whole technical plan exists "
                "to inform.",
         acceptance="An ADR recording what was proven and any changes it forces on the "
                    "simulation plan.",
         days=2, refs=["ARCH SS5"]),
    dict(title="Playable proof: an hour of free play",
         detail="Rule 6, and the milestone gate. This is the moment the project either has "
                "a technical foundation or does not.",
         acceptance="An hour of unscripted play with no loading screen, no crash, no frame "
                    "over 16 ms, and nothing that reads as placeholder.",
         days=3, refs=["ARCH Rule 6"]),
]
