# -*- coding: utf-8 -*-
"""M09-M11: embodiment and animation, interiors, rendering fidelity.

Where the game stops being a camera with a flight model and becomes a place
with a person in it.
"""

# ---------------------------------------------------------------------------
# M09 - embodiment and animation.
# ---------------------------------------------------------------------------

M09 = [
    dict(title="Source character and animation libraries, and audit their licences",
         detail="ADR-0005 rules out buying assets and rules out generating people: a "
                "rigged human with a face is not something a rule produces, and a "
                "convincing walk cycle is captured or authored. So this is a sourcing "
                "task, and it comes first because every task after it assumes a skeleton "
                "and a set of clips already exist. Free, licensed libraries only, with "
                "the terms read rather than remembered -- the surface work asserted the "
                "Megascans licence from memory and was wrong about it. One skeleton "
                "chosen and committed to, because retargeting everything later is worse "
                "than choosing badly now. Manifest and validator, same discipline as "
                "surfaces: a clip on disk with no recorded source and licence fails the "
                "build.",
         acceptance="A rigged character and a locomotion set are in the project, every "
                    "one of them has a manifest entry naming its source and its licence, "
                    "and an unaccounted asset fails CI naming the file.",
         days=3, refs=["SS6.9", "SS14"]),
    dict(title="Character controller with momentum",
         detail="Acceleration, deceleration, turning radius and footing rather than a "
                "capsule that changes velocity instantly. A body has mass and the "
                "controller has to admit it.",
         acceptance="Stopping from a run takes distance, turning at speed carries momentum, "
                    "and neither is a curve someone drew.",
         days=4, refs=["SS6.9"]),
    dict(title="One skeleton for first and third person",
         detail="Two skeletons is two sets of bugs and a first person view whose body is a "
                "lie. One rig, camera attached to the head, IK correcting what the camera "
                "can see.",
         acceptance="Look down in first person and see the same body a third-person camera "
                    "sees, doing the same thing.",
         days=4, refs=["SS6.9"]),
    dict(title="Full-body IK and foot placement",
         detail="Feet land on the ground that is actually there, at the angle it is at, on "
                "stairs, slopes and debris.",
         acceptance="Walk across broken terrain and a stairway with no foot sliding and no "
                    "foot intersecting geometry.",
         days=4, refs=["SS6.9"]),
    dict(title="Locomotion state machine and blend framework",
         detail="Idle, walk, run, sprint, crouch, prone, strafe, turn-in-place, with "
                "distance-matched transitions rather than fixed blends.",
         acceptance="No locomotion transition slides the feet, at any speed or direction "
                    "change.",
         days=5, refs=["SS6.9"]),
    dict(title="Locomotion in variable gravity",
         detail="Walking on a moon, in a station's spin gravity, and in a ship under thrust "
                "— stride, jump arc and fall speed all derived from local gravity.",
         acceptance="The same controller produces correct movement at 0.16 g, 1 g and 1.5 g "
                    "with no special-cased animation.",
         days=4, refs=["SS6.9"]),
    dict(title="Zero-gravity movement and EVA",
         detail="Push off, drift, arrest with a thruster pack, grab handholds. A different "
                "control problem, not a different character.",
         acceptance="Cross a compartment in zero gravity by pushing off and catching a "
                    "handhold, with momentum conserved throughout.",
         days=4, refs=["SS6.9"]),
    dict(title="Interaction framework",
         detail="One mechanism for everything a person can touch: buttons, levers, doors, "
                "seats, terminals, containers. Highlighting, reach, occlusion and the "
                "animation that sells it.",
         acceptance="A new interactable is one data declaration and inherits highlighting, "
                    "reach and animation with no bespoke code.",
         days=4, refs=["SS6.9"]),
    dict(title="Seats, harnesses and vehicle entry",
         detail="Getting into and out of a seat as a physical transition inside a moving "
                "frame, with the controller handing off to the vehicle.",
         acceptance="Board a hovering ship, take the seat and fly, with no teleport and no "
                    "cut, and get out again while it is still moving.",
         days=4, refs=["SS6.9"]),
    dict(title="Ladders, climbing and mantling",
         detail="Vertical movement that respects the geometry rather than snapping to it.",
         acceptance="Climb a ladder inside an accelerating ship and mantle onto a ledge, "
                    "both with correct hand placement.",
         days=3, refs=["SS6.9"]),
    dict(title="Carrying and manipulation animation",
         detail="Two-handed carry, one-handed carry, and the effect of mass on gait. Tied to "
                "M08's object handling.",
         acceptance="Carrying a heavy crate visibly changes stance and speed, driven by the "
                    "object's mass.",
         days=3, refs=["SS6.9"]),
    dict(title="Suits, helmets and life support on the body",
         detail="A person outside a breathable atmosphere needs a suit, and the suit is a "
                "component set with oxygen, temperature and pressure like any other.",
         acceptance="Step into vacuum without a sealed suit and die on the modelled "
                    "schedule, with warnings that come from the suit's components.",
         days=3, refs=["LW SS7.2"]),
    dict(title="Damage, injury and incapacitation",
         detail="Localised injury with effects on movement and capability, and a recovery "
                "path that is not a health bar refilling.",
         acceptance="A leg injury changes gait and speed until treated, and treatment "
                    "consumes something.",
         days=3, refs=["SS6.9"]),
    dict(title="Facial and upper-body animation framework",
         detail="Look-at, gesture, and expression driven by state rather than by canned "
                "clips, because every NPC in the living world will need it.",
         acceptance="A character tracks a moving object with head and eyes while walking, "
                    "with no clip authored for it.",
         days=4, refs=["LW SS6"]),
    dict(title="Character definition and variation",
         detail="Body proportions, faces, clothing and equipment as data, so a crowd is not "
                "twelve copies of one person.",
         acceptance="A hundred generated characters are visually distinct and all animate "
                    "correctly on the shared rig.",
         days=4, refs=["ARCH Rule 7"]),
    dict(title="Animation LOD and crowd cost",
         detail="Full evaluation up close, reduced further out, and something very cheap at "
                "distance, with transitions nobody sees.",
         acceptance="Two hundred visible characters stay inside the animation budget with "
                    "no visible LOD switch.",
         days=3, refs=["SS14"]),
    dict(title="Footstep, foley and movement audio",
         detail="Surface-aware footsteps, cloth, equipment, and breathing that responds to "
                "exertion and to whether there is air outside.",
         acceptance="Surface material is identifiable from footstep audio, and audio changes "
                    "correctly in a suit and in vacuum.",
         days=3, refs=["SS6.9"]),
    dict(title="Camera framework",
         detail="First person, third person, and the transitions, with collision, "
                "stabilisation and the shake budget that stops it becoming nauseating.",
         acceptance="Switching view is continuous, the camera never enters geometry, and "
                    "motion sickness testing passes with three people.",
         days=3, refs=["SS6.9"]),
    dict(title="Animation regression suite",
         detail="Recorded traversals with foot-sliding, penetration and pop metrics computed "
                "automatically, because animation regressions are invisible in a diff.",
         acceptance="A deliberately broken blend is caught by CI with a named metric.",
         days=3, refs=["SS13"]),
    dict(title="Playable proof: room to cockpit and back",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Out of a room, up a ladder, across a pad in wind, into a ship, into a "
                    "seat, fly, land, get out — one shot, no cut, nothing sliding.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M10 - interiors and modular architecture.
# ---------------------------------------------------------------------------

M10 = [
    dict(title="Modular kit framework",
         detail="Rooms, corridors, junctions and shafts as a connectable kit with sockets, "
                "so interiors are assembled rather than modelled. The prerequisite for "
                "generated ships, stations and buildings all at once.",
         acceptance="An interior assembled from the kit has no gap, no z-fight at any seam, "
                    "and correct collision throughout.",
         days=5, refs=["LW SS7"]),
    dict(title="Interior generation from a layout grammar",
         detail="A generator that lays out an interior from a purpose and a footprint — "
                "circulation first, then rooms, then detail — rather than stamping "
                "prefabs.",
         acceptance="Twenty generated interiors of the same purpose are all navigable, all "
                    "different, and none has an unreachable room.",
         days=5, refs=["LW SS7"]),
    dict(title="Ship interiors on the modular kit",
         detail="Ship internals assembled from the same kit, constrained by the hull, with "
                "the component graph from M05 physically present in them.",
         acceptance="Every component in a ship's graph has a physical location inside it "
                    "that can be walked to and worked on.",
         days=4, refs=["SS6.9"]),
    dict(title="Cockpit generated from the component graph",
         detail="The strongest case for generating rather than authoring, and it comes "
                "out of a promise already made: docs/quality-bar.md says every gauge "
                "traces to a real component. A hand-authored cockpit can lie about that. "
                "One laid out from the ship's own graph cannot -- a readout exists "
                "because a component exists, a breaker exists because a power path does, "
                "and a ship fitted differently gets a different panel without anyone "
                "redrawing it. Layout from the seat outward: reach envelope from the "
                "seated eye point, consoles on ergonomic arcs, sightlines to the canopy "
                "preserved, labels beside every control. Cable and pipe runs routed "
                "between components rather than drawn.",
         acceptance="Two ships with different component fits produce different cockpits "
                    "with no hand editing; every readout in both traces to a component in "
                    "the graph; and every control is reachable from the seated position "
                    "without the sightline to the horizon being blocked.",
         days=5, refs=["SS6.9", "LW SS7"]),
    dict(title="Station interiors",
         detail="Larger, with spin gravity, public and private volumes, docking connections "
                "and the traffic that implies.",
         acceptance="Walk from a docked ship into a station, across it, and into another "
                    "docked ship, all in the correct frames.",
         days=4, refs=["SS6.9"]),
    dict(title="Building interiors for settlements",
         detail="Habitation, industry and civic interiors, generated for the settlement "
                "layer, with the environment-driven composition from LW SS7.2 visible "
                "inside as well as outside.",
         acceptance="A habitat on an airless world has an airlock, a pressurised interior "
                    "and visible life support; the same archetype on a temperate world does "
                    "not.",
         days=4, refs=["LW SS7.2"]),
    dict(title="Portals and interior streaming",
         detail="Interiors stream and cull through portals, so a city block of walkable "
                "buildings costs what is visible rather than what exists.",
         acceptance="A hundred walkable interiors in view cost no more than the handful "
                    "actually visible through their openings.",
         days=5, refs=["SS14"]),
    dict(title="Doors, airlocks and pressure boundaries",
         detail="Doors as constraints with power, pressure differential and failure. An "
                "airlock is a state machine that can kill you.",
         acceptance="Cycling an airlock takes time, equalises pressure, and opening the "
                    "wrong door vents the compartment.",
         days=3, refs=["LW SS7.3"]),
    dict(title="Interior lighting framework",
         detail="Lights as powered fixtures. When the district loses power, the lights go "
                "out — which is the first-person tell LW SS7.1 requires.",
         acceptance="Cutting power to a building darkens its interior and emergency lighting "
                    "comes up on its own reserve.",
         days=3, refs=["LW SS7.1"]),
    dict(title="Interior navigation mesh generation",
         detail="Navigation built from the assembled kit at runtime, across frames, so NPCs "
                "can move through interiors that were generated a moment ago and are "
                "themselves moving.",
         acceptance="A character navigates from any point in a generated interior to any "
                    "other, including inside a moving ship.",
         days=4, refs=["LW SS6"]),
    dict(title="Interior audio: reverb, occlusion, propagation",
         detail="Rooms sound like rooms, doors muffle, and a corridor carries sound the way "
                "a corridor does.",
         acceptance="Reverb and occlusion change correctly walking between three connected "
                    "spaces without hand-placed volumes.",
         days=3, refs=["SS6.9"]),
    dict(title="Set dressing and clutter framework",
         detail="Rule-driven placement of the objects that make a room look inhabited, "
                "responding to the room's purpose and its owner's circumstances.",
         acceptance="A workshop, a bunk and an office generated from the same kit are "
                    "immediately distinguishable, and a wealthy one differs from a poor one.",
         days=4, refs=["LW SS7.1"]),
    dict(title="Interior collision and traversal quality pass",
         detail="Every surface a person can reach behaves: no invisible walls, no geometry "
                "you can walk through, no ledge you catch on.",
         acceptance="An automated traversal agent covers every reachable point of ten "
                    "generated interiors without getting stuck.",
         days=3, refs=["SS13"]),
    dict(title="Playable proof: street to cockpit through a building",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Street, into a building, up through it, onto a roof pad, into a docked "
                    "ship, to its cockpit — no load, no fade, no frame over budget.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M11 - rendering fidelity.
# ---------------------------------------------------------------------------

M11 = [
    dict(title="Material standard and shading model audit",
         detail="One documented PBR standard: what maps, what ranges, what conventions. "
                "Half the reason a game looks amateur is materials that disagree about "
                "what roughness means.",
         acceptance="Every material in the project conforms, and a validator flags any that "
                    "does not.",
         days=3, refs=["SS6.8"]),
    dict(title="Master material framework with layering",
         detail="Layered materials with blend masks, so a hull is metal plus paint plus wear "
                "plus dirt rather than one baked texture.",
         acceptance="A single hull material produces a clean ship and a filthy one from "
                    "parameters alone.",
         days=4, refs=["SS6.8"]),
    dict(title="Wear, dirt and weathering framework",
         detail="Procedural edge wear, dirt accumulation by cavity and by exposure, "
                "streaking below seams, heat discolouration near thrusters, driven by an "
                "object's actual history rather than authored in. Under ADR-0005 this is "
                "not a finishing touch, it is the single largest quality lever available: "
                "the gap between generated geometry and shipped-game geometry is mostly "
                "this layer, all of it curvature and occlusion arithmetic, and none of it "
                "needing an artist.",
         acceptance="A ship that has flown through dust looks like it, and cleaning it "
                    "removes exactly that. The same asset with the wear layer disabled and "
                    "enabled, at identical framing and exposure, is the before-and-after "
                    "this task is judged on.",
         days=5, refs=["SS6.8"]),
    dict(title="Decal framework",
         detail="Projected detail on hulls, terrain and interiors — markings, damage, "
                "leaks, signage — with correct normals and a budget.",
         acceptance="A thousand decals in view cost inside budget and none swims or "
                    "z-fights.",
         days=3, refs=["SS6.8"]),
    dict(title="Lighting: Lumen configuration and fallback",
         detail="Global illumination that works from a lit cockpit at night to a sunlit "
                "dune, with a measured decision about where it costs more than it gives. "
                "Design v1.1 turned it off for the slice; this is where that is revisited "
                "with numbers.",
         acceptance="Three reference scenes are correctly lit within budget, and the "
                    "decision is recorded with measurements.",
         days=5, refs=["SS6.8", "SS14"]),
    dict(title="Shadow quality across scales",
         detail="Contact shadows in a cockpit, cascades across a landscape, and shadows from "
                "a moon on a planet — three problems that must share one setup.",
         acceptance="No shadow acne, no peter-panning and no cascade seam at any of the "
                    "three scales.",
         days=4, refs=["SS6.8"]),
    dict(title="Reflection framework",
         detail="Screen-space, captures and ray-traced where it pays, with a policy for what "
                "gets which. Water, canopies and polished hulls each want a different "
                "answer.",
         acceptance="A reflective hull, a canopy and the sea are all correct from a viewpoint "
                    "that shows all three.",
         days=4, refs=["SS6.8"]),
    dict(title="Exposure, tone mapping and colour pipeline",
         detail="One colour pipeline from lighting to display, handling a range from "
                "starlight to a sunlit ice field without clipping or hunting.",
         acceptance="Flying from a dark hangar into daylight adapts smoothly with no "
                    "overshoot, and no scene clips.",
         days=3, refs=["SS6.8"]),
    dict(title="Anti-aliasing and upscaling",
         detail="Temporal AA tuned against the specific problems here — thin cockpit "
                "geometry, high-frequency terrain, particles — plus an upscaler evaluated "
                "rather than assumed.",
         acceptance="No ghosting on cockpit instruments during rapid motion and no shimmer "
                    "on terrain at any distance.",
         days=4, refs=["SS6.8"]),
    dict(title="Particle and VFX framework",
         detail="Thrusters, damage, weather, dust and debris on one budgeted system, with "
                "LOD and culling and no orphaned emitters.",
         acceptance="Worst-case effects load stays inside budget and a two-hour session "
                    "leaks no emitters.",
         days=4, refs=["SS6.8"]),
    dict(title="Volumetric lighting and atmospherics in interiors",
         detail="Light shafts, dust in the air, smoke that fills a compartment — the "
                "difference between a lit room and a rendered one.",
         acceptance="A single window lights a dusty interior convincingly at three times of "
                    "day, inside budget.",
         days=3, refs=["SS6.8"]),
    dict(title="LOD and impostor chain for everything",
         detail="One policy covering ships, buildings, characters and props, with automatic "
                "generation and no visible switch.",
         acceptance="No LOD transition is visible at any distance for any asset class.",
         days=4, refs=["SS14"]),
    dict(title="Character rendering: skin, cloth, hair, eyes",
         detail="The hardest thing to make not look like a mannequin, and the thing a player "
                "looks at closest.",
         acceptance="A character at conversation distance holds up in three lighting "
                    "conditions.",
         days=5, refs=["SS6.8"]),
    dict(title="Cockpit rendering quality pass",
         detail="Where the player spends most of their time: glass, displays, backlighting, "
                "reflections on the canopy, and the sun through it.",
         acceptance="A cockpit at dawn, at noon and at night each hold up, with legible "
                    "instruments in all three.",
         days=4, refs=["SS6.9"]),
    dict(title="Post-processing chain",
         detail="Bloom, motion blur, depth of field, aberration and grain, each with a "
                "reason and a budget, and each defensible against being turned off.",
         acceptance="Every post effect has a measured cost and a documented reason, and the "
                    "chain costs no more than its budget.",
         days=3, refs=["SS6.8"]),
    dict(title="Reference comparison harness",
         detail="Fixed shots against reference footage at set distances and lighting "
                "conditions, reviewed each time the renderer changes.",
         acceptance="The comparison set is captured automatically and archived per build.",
         days=3, refs=["SS13"]),
    dict(title="Playable proof: the reference comparison",
         detail="Rule 6. The gate: side by side at three distances and three lighting "
                "conditions.",
         acceptance="A stranger cannot immediately sort ours from the reference on material "
                    "and lighting alone.",
         days=3, refs=["ARCH Rule 6"]),
]
