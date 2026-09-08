# -*- coding: utf-8 -*-
"""M05-M08: ships, flight, travel, and physics at scale.

The core of the technical model, and the block most likely to contain something
that turns out to be impossible. M08 in particular is engine-level work that
Chaos does not support: design SS6.9 flags nested reference frames as the reason
the flight model was hand-integrated in the first place.
"""

# ---------------------------------------------------------------------------
# M05 - ship framework: hulls, components, subsystems.
# ---------------------------------------------------------------------------

M05 = [
    dict(title="Ship definition format",
         detail="A ship is data: hull geometry, hardpoints, component slots, mass "
                "distribution, and the topology that connects them. Adding a ship must "
                "never require a compiler.",
         acceptance="A new ship is one data file plus art, and it flies.",
         days=3, refs=["SS6.9", "ARCH Rule 7"]),
    dict(title="Component graph with typed ports",
         detail="Components connect through typed ports — power, coolant, fuel, data — and "
                "the graph is what damage, heat and failure propagate through. This is the "
                "spine of the whole milestone.",
         acceptance="A component's inputs and outputs are discoverable at runtime and an "
                    "unsatisfiable connection is a load error, not a crash.",
         days=4, refs=["SS6.9"]),
    dict(title="Power generation and distribution",
         detail="Plants, buses, priorities and brownouts. Demand exceeding supply sheds "
                "load in priority order rather than stopping the ship.",
         acceptance="Overload a bus and the lowest-priority consumers drop first, in the "
                    "order the configuration states.",
         days=3, refs=["SS6.9"]),
    dict(title="Thermal model",
         detail="Components generate heat, coolant loops move it, radiators reject it, and "
                "an overheated component throttles or fails. Heat is also a signature.",
         acceptance="Run at full power with radiators retracted and the ship overheats on "
                    "the schedule the thermal model predicts.",
         days=4, refs=["SS6.9"]),
    dict(title="Fuel: types, tanks, transfer, consumption",
         detail="Propellant and reactor fuel as separate resources, consumed at rates the "
                "flight model produces rather than at a rate someone picked.",
         acceptance="Fuel burn integrated over a flight matches thrust integrated over the "
                    "same flight, and running dry stops the engines.",
         days=3, refs=["SS6.9"]),
    dict(title="Mass, centre of mass and inertia from contents",
         detail="Computed from hull, components, fuel and cargo, updating as fuel burns and "
                "cargo moves. An unevenly loaded ship handles unevenly.",
         acceptance="Load cargo on one side and the ship's handling changes measurably in "
                    "the direction predicted.",
         days=3, refs=["SS6.9"]),
    dict(title="Damage model over the component graph",
         detail="Hits damage components, damage propagates along ports, and the failure "
                "order is explained by the topology rather than by a table.",
         acceptance="Destroying a power coupling kills exactly the components downstream of "
                    "it and nothing else.",
         days=4, refs=["SS6.9"]),
    dict(title="Hull damage: penetration, deformation, breach",
         detail="Localised hull state, with pressure loss where the hull is breached and "
                "structural failure where enough of it is gone.",
         acceptance="A breach vents the compartment behind it and not the rest of the ship.",
         days=4, refs=["SS6.9"]),
    dict(title="Component wear and condition",
         detail="Components degrade with use and with abuse, changing performance before "
                "they fail. The maintenance loop in M19 reads from this.",
         acceptance="A neglected thruster loses measurable output over a hundred hours of "
                    "operation and warns before it fails.",
         days=2, refs=["LW SS7.6"]),
    dict(title="Repair and component replacement",
         detail="Field repair with limited effect, workshop repair with full effect, and "
                "component swapping with the parts economy in mind.",
         acceptance="A damaged ship is repaired to a stated condition and the parts are "
                    "consumed from somewhere.",
         days=3, refs=["SS6.9"]),
    dict(title="Life support as a component",
         detail="Atmosphere, pressure and temperature inside the hull, with consumption, "
                "reserves and failure. On a breathable world it is a convenience; "
                "elsewhere it is the thing keeping you alive.",
         acceptance="Cut life support in vacuum and the internal atmosphere depletes on the "
                    "modelled schedule, with warnings.",
         days=3, refs=["LW SS7.2"]),
    dict(title="Shields",
         detail="Directional shields with capacity, recharge, and a power draw that competes "
                "with everything else on the bus.",
         acceptance="Raising shields measurably reduces power available to thrust, and the "
                    "trade-off is visible on the instruments.",
         days=3, refs=["SS6.9"]),
    dict(title="Signature model: thermal, electromagnetic, cross-section",
         detail="What a ship looks like to a sensor, derived from what it is doing. Running "
                "cold and quiet must be a real option, because information is the design's "
                "scarce commodity.",
         acceptance="Shutting down non-essential systems measurably reduces detection range, "
                    "and the numbers come from the thermal and power models.",
         days=3, refs=["SS4", "SS6.9"]),
    dict(title="Hardpoints and mount framework",
         detail="The general mechanism for attaching things to a hull — weapons, mining "
                "gear, sensors, cargo pods — with size classes and power and cooling draw. "
                "Not the weapons themselves.",
         acceptance="A mount accepts any component of its class, refuses others, and its "
                    "power and heat appear in the ship's totals.",
         days=3, refs=["SS6.9"]),
    dict(title="Cargo hold and volumetric stowage",
         detail="Cargo occupies space and has mass and a position. A full hold is visibly "
                "full and an unbalanced one is felt.",
         acceptance="Loading to capacity is limited by volume or mass, whichever binds "
                    "first, and the contents are visible in the hold.",
         days=3, refs=["SS7"]),
    dict(title="Animated ship parts as components",
         detail="Gear, ramps, doors, radiators and canopies are components with state, "
                "power draw and failure modes, not animations.",
         acceptance="Cutting power mid-cycle leaves the gear half-deployed and it stays "
                    "there until power returns.",
         days=3, refs=["SS6.9"]),
    dict(title="Power-on and shutdown sequence",
         detail="A ship starts cold and comes up in an order the component graph implies. "
                "This is where a ship stops being a vehicle and becomes a machine.",
         acceptance="Cold start brings systems up in dependency order, and skipping a step "
                    "prevents the next one.",
         days=2, refs=["SS6.9"]),
    dict(title="Instrumentation framework",
         detail="Multi-function displays driven by real component state through one data "
                "path. An instrument that lies is a bug in the instrument, not in the "
                "display.",
         acceptance="Every gauge traces to a component reading, and a failed sensor makes "
                    "its gauge unavailable rather than wrong.",
         days=4, refs=["SS6.9"]),
    dict(title="Ship audio from component state",
         detail="Engine note from throttle and load, coolant pumps under thermal stress, "
                "alarms from the systems that raised them.",
         acceptance="A listener can identify which subsystem is in trouble from audio alone.",
         days=3, refs=["SS6.9"]),
    dict(title="Damage visuals",
         detail="Scorching, deformation, venting, sparks and debris tied to the damage "
                "state rather than played as effects.",
         acceptance="A ship's exterior condition is readable at a glance and matches its "
                    "component state.",
         days=3, refs=["SS6.9"]),
    dict(title="Ship configuration and outfitting",
         detail="Swap components within slot and power constraints, with the consequences "
                "computed rather than listed.",
         acceptance="Fitting an oversized power plant changes mass, heat and handling, and "
                    "the configuration screen shows all three before it is fitted.",
         days=3, refs=["SS6.9"]),
    dict(title="Ship state persistence",
         detail="Condition, fuel, cargo, damage and configuration survive a save, a reload "
                "and eventually a handover to the simulation.",
         acceptance="A damaged, half-fuelled, oddly configured ship reloads exactly as it "
                    "was.",
         days=2, refs=["ARCH Rule 1"]),
    dict(title="Ship diagnostics and test harness",
         detail="A headless rig that builds a ship, applies loads and damage, and asserts "
                "the component graph behaves. Ship bugs are systemic and need systemic "
                "tests.",
         acceptance="Every failure mode in this milestone has a test that reproduces it "
                    "without flying anything.",
         days=3, refs=["SS13"]),
    dict(title="Playable proof: shoot out a power coupling",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Thrusters on that bus go dead, heat climbs, systems degrade in graph "
                    "order, and repair brings the ship back.",
         days=2, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M06 - flight model in vacuum and in air.
# ---------------------------------------------------------------------------

M06 = [
    dict(title="Rigid-body integration with a real inertia tensor",
         detail="Replace the prototype's hand-integrated point mass with a proper rigid "
                "body using the mass distribution M05 computes. Still hand-integrated "
                "rather than handed to Chaos, for the reasons in SS6.9.",
         acceptance="Angular response matches the inertia tensor, and an off-axis thrust "
                    "produces the torque the geometry predicts.",
         days=4, refs=["SS6.9"]),
    dict(title="Thruster allocation solver",
         detail="Given thruster positions, orientations and limits, solve for the "
                "combination producing the commanded force and torque. This is what makes "
                "a ship's handling a consequence of its thruster layout rather than of a "
                "handling number.",
         acceptance="Disable a thruster and the solver compensates, with the loss of "
                    "authority in the axis the geometry predicts.",
         days=5, refs=["SS6.9"]),
    dict(title="Flight assist and control modes",
         detail="Assist on, assist off, coupled and decoupled, each a different controller "
                "over the same allocator rather than a different flight model.",
         acceptance="Switching modes changes only the controller; the physics underneath "
                    "is identical and provably so.",
         days=3, refs=["SS6.9"]),
    dict(title="Aerodynamics: lift, drag and control surfaces",
         detail="Real aerodynamic forces from angle of attack, airspeed and the atmospheric "
                "density M04 provides, with control surfaces that only work in air.",
         acceptance="A winged ship glides unpowered in atmosphere and does not in vacuum.",
         days=4, refs=["SS6.9"]),
    dict(title="Stall, spin and recovery",
         detail="An aerodynamic envelope with edges. Exceeding them has consequences that "
                "can be recovered from if the pilot knows how.",
         acceptance="A deliberate stall departs controlled flight and standard recovery "
                    "inputs recover it.",
         days=3, refs=["SS6.9"]),
    dict(title="Vacuum-to-atmosphere transition",
         detail="Aerodynamic authority fades in as density rises and thruster authority "
                "does not, so the handover is continuous rather than a mode change.",
         acceptance="A re-entry from vacuum to sea level shows no discontinuity in forces "
                    "at any altitude.",
         days=3, refs=["SS6.9"]),
    dict(title="VTOL and hover",
         detail="Vertical thrust with ground effect, and the fuel cost that makes hovering "
                "a decision rather than a default.",
         acceptance="Hovering consumes fuel at the rate thrust-to-weight implies, and "
                    "ground effect is measurable near the surface.",
         days=3, refs=["SS6.9"]),
    dict(title="Landing gear and ground handling",
         detail="Suspension, contact, friction, braking and steering on the ground. Landing "
                "is a physical event, not a state change.",
         acceptance="A hard landing compresses the gear and can damage it; a shallow one "
                    "does not; the ship stays put on a slope up to the friction limit.",
         days=4, refs=["SS6.9"]),
    dict(title="G-force model and pilot effects",
         detail="Sustained and transient acceleration on the pilot, with the visual and "
                "physiological limits that make manoeuvres have a cost.",
         acceptance="A sustained high-g turn greys the view on the modelled schedule and "
                    "recovery follows the same model.",
         days=2, refs=["SS6.9"]),
    dict(title="Flight computer limits and envelope protection",
         detail="Configurable limits on the allocator — g, angular rate, thermal — that a "
                "pilot can relax at their own risk.",
         acceptance="Limits are enforced by the controller, and disabling them lets the "
                    "airframe be damaged.",
         days=2, refs=["SS6.9"]),
    dict(title="Autopilot framework",
         detail="Hold attitude, hold altitude, hold station, approach and auto-land, as "
                "controllers over the same allocator. Directives in M21 will drive ships "
                "through this.",
         acceptance="Auto-land puts a ship on a pad in wind, repeatably, from any approach.",
         days=4, refs=["LW SS4.1"]),
    dict(title="Docking",
         detail="Approach corridors, alignment, capture and release against a station or "
                "another ship, with the moving-frame handover M08 will formalise.",
         acceptance="Dock with a station in orbit and the ship's frame becomes the "
                    "station's with no jolt.",
         days=4, refs=["SS6.9"]),
    dict(title="Ground vehicle framework",
         detail="Wheeled and tracked vehicles over the same rigid body and the same terrain "
                "collision, because a settlement needs things that are not ships.",
         acceptance="A ground vehicle drives across terrain with suspension responding to "
                    "the surface the renderer draws.",
         days=4, refs=["SS6.9"]),
    dict(title="Input framework: HOTAS, gamepad, mouse and keyboard",
         detail="One binding layer over one command set, with curves, deadzones and "
                "per-device profiles. Not three control schemes.",
         acceptance="All three devices produce identical commanded forces for equivalent "
                    "input, and rebinding requires no code.",
         days=3, refs=["SS6.9"]),
    dict(title="HUD and flight instruments",
         detail="Velocity vector, attitude, target reticle, energy, and the instruments a "
                "pilot actually flies on — driven through M05's instrumentation path.",
         acceptance="A full flight is completable on instruments alone with the external "
                    "view disabled.",
         days=4, refs=["SS6.9"]),
    dict(title="Collision response for vehicles",
         detail="Impacts transfer momentum and damage components through M05's graph, "
                "rather than triggering a generic effect.",
         acceptance="A glancing impact damages the components at the point of contact and "
                    "imparts the momentum the collision implies.",
         days=3, refs=["SS6.9"]),
    dict(title="Ship-to-ship physical interaction",
         detail="Ships push each other, land on each other, and carry each other. The "
                "prerequisite for M08's nested frames.",
         acceptance="A small ship lands on a large one and is carried with it.",
         days=3, refs=["SS6.9"]),
    dict(title="Flight recorder",
         detail="Record every input, force and state to a file that can be replayed exactly. "
                "Flight bugs are transient and unreproducible without this.",
         acceptance="A recorded flight replays to an identical trajectory.",
         days=2, refs=["SS13"]),
    dict(title="Flight model tuning tools",
         detail="In-editor visualisation of thruster authority, aerodynamic forces, envelope "
                "and control response, so tuning is measured rather than felt.",
         acceptance="A ship's envelope can be read off a chart the tool generates.",
         days=3, refs=["SS13"]),
    dict(title="Flight regression suite",
         detail="Recorded flights replayed in CI with trajectory tolerances, so a physics "
                "change that alters handling is caught immediately.",
         acceptance="A deliberate one-percent thrust change fails the suite.",
         days=2, refs=["SS13"]),
    dict(title="Playable proof: continent, orbit, pad",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Pad to another continent aerodynamically, out to vacuum, manoeuvre, "
                    "re-enter with heating, land on the pad you left — cockpit only.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M07 - seamless travel across a system.
# ---------------------------------------------------------------------------

M07 = [
    dict(title="Origin rebasing",
         detail="Move the world under the camera so float precision never has to express "
                "an astronomical unit. The prototype avoids this by keeping vertices "
                "patch-relative; everything else in the game will not have that luxury.",
         acceptance="At one AU from origin, a one-millimetre movement is still one "
                    "millimetre, for every actor and every particle system.",
         days=5, refs=["SS6.8"]),
    dict(title="Depth precision across twelve orders of magnitude",
         detail="A cockpit instrument at 40 cm and a planet at 400,000 km in one frame. "
                "Reversed-Z with cascaded depth ranges, or a logarithmic buffer, chosen by "
                "measurement rather than by preference.",
         acceptance="No z-fighting anywhere from a hand-held object to a planet on the same "
                    "screen.",
         days=4, refs=["SS6.8"]),
    dict(title="Scale-aware camera and rendering",
         detail="Near and far rendering passes composited, so distant bodies are drawn at a "
                "scale the depth buffer can hold.",
         acceptance="A planet, its moon and a ship's own hull are all correctly occluded "
                    "against each other.",
         days=4, refs=["SS6.8"]),
    dict(title="Entity relevance and streaming by distance",
         detail="What exists, what is simulated, what is rendered, and what is a dot — with "
                "explicit bands and explicit transitions between them.",
         acceptance="A ship a thousand kilometres away is a dot; approach it and it becomes "
                    "a ship with no pop and no state loss.",
         days=4, refs=["ARCH SS3"]),
    dict(title="High-speed travel mode",
         detail="The thing that makes a system crossable in minutes rather than weeks: "
                "spool time, alignment, a speed that scales with distance, and a real cost.",
         acceptance="Cross the system in a few minutes, with every phase interruptible and "
                    "the ship in a legal physical state at every instant.",
         days=4, refs=["LW SS8"]),
    dict(title="Interdiction and interruption",
         detail="High-speed travel can be pulled out of, by mass, by another ship, or by "
                "failure — otherwise the entire strategic layer has no chokepoints.",
         acceptance="A ship in transit is pulled out at the position and velocity the model "
                    "predicts, and the pursuer arrives with it.",
         days=3, refs=["LW SS3"]),
    dict(title="Route planning and navigation",
         detail="Plot a route between any two sites, with time, fuel and hazard estimates "
                "that match what flying it actually costs.",
         acceptance="Estimated and actual travel time agree within five percent over twenty "
                    "sampled routes.",
         days=3, refs=["LW SS4"]),
    dict(title="Interstellar travel",
         detail="Between systems: whatever mechanism the fiction settles on, with a duration "
                "that keeps information genuinely slow. Design SS8: instant travel means "
                "instant information and there is no game left.",
         acceptance="An interstellar transit takes a stated time, is interruptible, and "
                    "leaves the destination system correctly streamed on arrival.",
         days=4, refs=["LW SS8"]),
    dict(title="Arrival and approach handling",
         detail="Deceleration, traffic separation, approach clearance and the transition "
                "into a body's frame — the part where seamlessness usually breaks.",
         acceptance="Arrive at a planet from high speed and descend to a pad without a "
                    "single frame over budget.",
         days=3, refs=["SS6.8"]),
    dict(title="Cross-scale physics stability",
         detail="Physics at 200 m/s near a rotating planet and at high speed between them, "
                "without tunnelling, jitter or integration blow-up.",
         acceptance="A twenty-minute transit ends with position error inside tolerance and "
                    "no jitter at any point.",
         days=4, refs=["SS6.9"]),
    dict(title="Bookmarks, waypoints and the navigation UI",
         detail="Somewhere to point the ship at, including places the player named and "
                "places they were told about.",
         acceptance="A waypoint set from the map is flyable to and arrives where it said.",
         days=2, refs=["LW SS4"]),
    dict(title="Traffic: other ships going about their business",
         detail="Ships on routes, arriving and departing, at a density the streaming budget "
                "supports. Placeholder behaviour now; the simulation drives it from M16.",
         acceptance="Twenty ships operate in one system without a frame cost that reads on "
                    "a graph.",
         days=3, refs=["LW SS6"]),
    dict(title="Precision and seam regression suite",
         detail="Automated traversals at many scales asserting no precision artefact, no "
                "hitch over a threshold, and no discontinuity in position or velocity.",
         acceptance="A deliberately reintroduced precision bug is caught by CI.",
         days=3, refs=["SS13"]),
    dict(title="Hitch elimination pass",
         detail="Every remaining stall over 4 ms during traversal, found and removed. "
                "Seamless is a property that is destroyed by one hitch.",
         acceptance="A full system traversal shows no frame over 16 ms and no allocation "
                    "spike over the budget.",
         days=4, refs=["SS14"]),
    dict(title="Playable proof: surface to a moon of another planet",
         detail="Rule 6. The gate, run and recorded in one continuous shot.",
         acceptance="No loading screen, no cut, no precision artefact anywhere on the path.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M08 - physics at scale and local grids.
#
# The riskiest milestone in the plan. Design SS6.9 identifies nested reference
# frames as engine-level surgery, and it is the reason the prototype's flight
# model integrates itself rather than using Chaos.
# ---------------------------------------------------------------------------

M08 = [
    dict(title="Spike: three approaches to nested reference frames",
         detail="Chaos with a moving kinematic parent, a second physics scene per grid with "
                "transform bridging, or a custom solver for grid-local bodies. Build enough "
                "of each to measure, then decide in an ADR. Choosing wrong here is a year.",
         acceptance="Three prototypes, measured on stability, cost and integration risk, and "
                    "an ADR that picks one and says why.",
         days=8, refs=["SS6.9", "ARCH SS5"]),
    dict(title="Local physics grid framework",
         detail="A grid is a moving frame with its own physics content. Bodies inside it "
                "are simulated in grid-local coordinates, so a ship at 3,000 m/s is a room "
                "at rest.",
         acceptance="An object at rest inside a manoeuvring ship stays at rest relative to "
                    "the ship, at any speed the ship can reach.",
         days=6, refs=["SS6.9"]),
    dict(title="Frame entry and exit",
         detail="Walking onto a ship, stepping off onto a pad, a ship entering another "
                "ship's hold — velocity and angular velocity transferred correctly at every "
                "boundary.",
         acceptance="Step from a landing pad onto a hovering ship and back with no jolt, "
                    "no launch, and no lost velocity.",
         days=5, refs=["SS6.9"]),
    dict(title="Character controller inside a moving frame",
         detail="Walking, standing and falling relative to a frame that is itself "
                "accelerating and rotating, with the apparent forces that implies.",
         acceptance="Walk the length of a ship while it accelerates and rolls, without "
                    "sliding and without the controller fighting the frame.",
         days=5, refs=["SS6.9"]),
    dict(title="Apparent forces: acceleration, rotation, Coriolis",
         detail="Inertia felt inside an accelerating ship, and centrifugal gravity inside a "
                "rotating station with the Coriolis effect that comes with it.",
         acceptance="A dropped object inside a spinning station lands where the rotating "
                    "frame predicts, not directly below.",
         days=4, refs=["SS6.9"]),
    dict(title="Nested grids",
         detail="A vehicle inside a ship inside a station. Depth-limited, but the limit must "
                "be a decision rather than an accident.",
         acceptance="Two levels of nesting are stable and the limit is enforced with a "
                    "clear error rather than a failure.",
         days=4, refs=["SS6.9"]),
    dict(title="Gravity generation inside grids",
         detail="Artificial gravity as a per-grid field with orientation, plus what happens "
                "when it fails and everything inside becomes free-floating.",
         acceptance="Cutting grid gravity leaves loose objects and characters floating with "
                    "the velocity they had.",
         days=3, refs=["SS6.9"]),
    dict(title="Continuous collision detection at high speed",
         detail="Nothing may pass through a hull because it moved far in one step. Sweeps "
                "for fast bodies, with a budget.",
         acceptance="A projectile at any modelled speed always hits the surface it should, "
                    "over ten thousand trials.",
         days=4, refs=["SS6.9"]),
    dict(title="Collision geometry for large structures",
         detail="Ships and stations need collision at a fidelity that supports walking "
                "inside them, streamed, without cooking a battleship every time one appears.",
         acceptance="A large ship's interior collision streams in ahead of the character and "
                    "never blocks a frame.",
         days=4, refs=["SS6.9"]),
    dict(title="Constraint and joint framework",
         detail="Doors, ramps, turrets, elevators, landing gear and cargo clamps as "
                "constraints driven by M05's components rather than as animations.",
         acceptance="A ramp lowered under load moves at the rate the actuator provides and "
                    "stops when the power does.",
         days=4, refs=["SS6.9"]),
    dict(title="Object handling: pick up, carry, place, throw",
         detail="Loose objects with mass that behave inside moving frames, because a cup on "
                "a table in a manoeuvring ship is the whole promise of this milestone.",
         acceptance="Set a cup on a table, fly a barrel roll, and the cup behaves the way "
                    "the frame and the friction say it should.",
         days=4, refs=["SS6.9"]),
    dict(title="Ragdoll and character physics",
         detail="Bodies that fall, are thrown and collide, correctly inside a moving frame.",
         acceptance="A ragdoll inside an accelerating ship slides toward the stern at the "
                    "rate the acceleration implies.",
         days=3, refs=["SS6.9"]),
    dict(title="Destruction framework",
         detail="Structural damage to ships, buildings and terrain features, with debris "
                "that is physical and then is not, on a budget.",
         acceptance="A destroyed structure produces debris that settles and is cleaned up "
                    "without a frame cost that reads on a graph.",
         days=5, refs=["SS6.9"]),
    dict(title="Physics determinism and stability harness",
         detail="Fixed-step physics with recorded scenarios replayed in CI. Frame-rate "
                "dependent physics is a bug that only shows on someone else's machine.",
         acceptance="A recorded scenario replays identically at 30, 60 and 144 fps.",
         days=3, refs=["ARCH Rule 5"]),
    dict(title="Physics performance budget",
         detail="Grid count, body count and substep cost under one budget, with degradation "
                "that is a decision rather than a stutter.",
         acceptance="Ten active grids with two hundred bodies each stays inside the physics "
                    "budget.",
         days=3, refs=["SS14"]),
    dict(title="Playable proof: walk to the cockpit at speed",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Walk from the stern to the cockpit while the ship accelerates, rolls "
                    "and flies through atmosphere; a cup set down stays on the table; step "
                    "out onto a pad on a rotating planet and inherit the right frame.",
         days=3, refs=["ARCH Rule 6"]),
]
