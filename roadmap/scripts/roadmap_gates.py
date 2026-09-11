# -*- coding: utf-8 -*-
"""Explicit gate checks, one list per milestone.

A gate written as a paragraph is a gate somebody argues with. Each entry below
is a single condition that is either met or not, plus **how it is decided** —
because "no popping that reads as a seam" has no judge, and a criterion with no
judge is a criterion that passes whenever the person checking is tired.

`how` is one of:

  auto      a script or automated test decides, in CI. No human judgement.
  measured  a number the harness produces, against a stated threshold.
  capture   a recorded image or video, compared against a stated criterion.
  observed  a person follows a written protocol. Used sparingly; where it
            appears, the protocol is in the text and includes how many people
            and what counts as agreement.

Every check traces to docs/quality-bar.md. Where a milestone's checks do not
cover a promise made in that document, one of the two is wrong.
"""

GATE_CHECKS = {

"M00": [
    ("A dump of the running world traces a corporate collapse back four causes.", "observed"),
    ("Two NPCs hold different beliefs about the same fact, for reasons the dump explains.", "observed"),
    ("The client flies orbit to ground to orbit with no loading screen.", "capture"),
    ("Water, weather and a settlement are present and rendered.", "capture"),
],

"M01": [
    ("No source file exceeds 500 lines.", "auto"),
    ("Every module declares three or fewer project dependencies, except the composition root.", "auto"),
    ("A dependency pointing up the layering fails the build, naming the rule.", "auto"),
    ("Captures match the committed reference within the perceptual tolerance.", "auto"),
    ("The flight model's tests pass with no world, actor or frame involved.", "auto"),
],

"M5P": [
    ("The offscreen playtest flies its session with no divergence: spin under 180 deg/s, speed inside the envelope, nothing NaN.", "auto"),
    ("The playtest's log carries no ensure and no crash, and no hitch over 250 ms after its first minute.", "auto"),
    ("Frames during the playtest hold M02's criterion: 99.9% under 16.7 ms and none over 33 ms.", "measured"),
    ("Playtest captures are crisp -- edge contrast within 10% of a static control frame -- and space is black.", "capture"),
    ("At full throttle most of the engine voice's energy is below 400 Hz, with no stepped gain changes.", "measured"),
    ("The owner flies the build for ten minutes and signs it off.", "observed"),
],
"M2S": [
    ("Twelve side-by-side pairs against real photographs at 2 m, 20 m and 200 m, each with a written verdict.", "capture"),
    ("At least three of those verdicts name a specific remaining deficiency.", "observed"),
    ("Standing on flat ground, no terrain quad exceeds 40 px at 1920x1080 measured at 20 m.", "measured"),
    ("A 50 m profile of the drawn mesh deviates from a fitted line by at least 15 cm RMS.", "measured"),
    ("Adjacent LOD depths agree to within 25 cm, so the ground does not re-form as the rings sweep.", "auto"),
    ("An overhead capture shows no autocorrelation peak at the tiling period above the noise floor.", "auto"),
    ("Each of albedo, normal, roughness, AO and height is shown, isolated and at pinned exposure, to be the map the manifest names.", "capture"),
    ("A capture at 3 m shows stones whose silhouettes break the horizon line behind them.", "capture"),
    ("Four biomes at eye height look like four different places, and the closest pair differs by more than 15 of 255.", "measured"),
    ("The frame time cost of the whole milestone is attributed to geometry, material and scatter separately.", "measured"),
    ("The scripted flight reports zero unfilled patches at the resolution recorded in its own report.", "measured"),
],

"M02": [
    ("A 200 km transect at 300 m altitude reports zero unfilled patches.", "measured"),
    ("The same transect at 900 m/s reports zero unfilled patches.", "measured"),
    ("A downward trace hits terrain on every frame of that transect.", "auto"),
    ("No frame exceeds 16.7 ms during the transect.", "measured"),
    ("Walking a ridge at 2 m/s across four LOD boundaries shows no vertex movement.", "capture"),
    ("Three biomes meet on one slope with no visible blend band.", "capture"),
    ("No texture repetition is identifiable at any distance in the biome capture.", "observed"),
    ("Every river reaches the sea or a basin; none flows uphill.", "auto"),
    ("A cave is entered, traversed and exited with collision and lighting correct throughout.", "capture"),
    ("One command turns a generated asset into a contact sheet, at fixed exposure, with the camera provably outside the geometry.", "auto"),
    ("Two consecutive runs of the turntable agree within the stated tolerance, worst single channel.", "auto"),
    ("Ten thousand scatter instances render inside the frame budget, placed identically across runs.", "measured"),
],

"M2W": [
    ("A packaged build with no editor present flies the scripted flight to completion.", "auto"),
    ("Its captures match the committed references within the perceptual tolerance.", "auto"),
    ("Its frame-time report is no worse than the editor build's.", "measured"),
    ("No material in the project is constructed at runtime; every one is a saved asset.", "auto"),
    ("The project opens on its own map, not on an engine map.", "auto"),
    ("One command regenerates every generated asset from its definition.", "auto"),
    ("A second run leaves version-controlled generated assets byte-identical; derived ones are checked by settings and render, because Unreal packages embed GUIDs.", "auto"),
    ("A generated hull is a static mesh with Nanite, a full LOD chain, matching collision and a distance field.", "auto"),
    ("Draw count for a settlement's scattered props is independent of the instance count.", "measured"),
    ("A generated asset with no definition behind it is reported by a validator, naming the file.", "auto"),
    ("The terrain component comparison is written down with measured generation, upload and draw costs for each candidate.", "measured"),
    ("A packaged build shows no shader compilation stall on first sight of any material.", "measured"),
],

"M03": [
    ("A moon rises, transits and sets within one arcminute of the ephemeris prediction.", "auto"),
    ("Sky and ephemeris agree to the arcminute at 100 sampled times and locations.", "auto"),
    ("Positions computed forward from zero and directly at time t agree to sub-metre over a simulated century.", "auto"),
    ("A moon's phase matches the sun-moon-observer geometry from any body in the system.", "auto"),
    ("An eclipse predicted by the ephemeris is observable from the surface at the predicted time.", "capture"),
    ("Illuminance at each planet matches the inverse-square prediction.", "auto"),
    ("Landing on a moon uses the same terrain code path, with correct low gravity and no atmosphere.", "capture"),
    ("A system regenerates identically from its seed in a second process.", "auto"),
],

"M04": [
    ("Flying into a storm front changes visibility, wind loading and audio together.", "capture"),
    ("The same storm is visible from orbit at the position the weather model gives it.", "capture"),
    ("A storm tracked over a simulated week follows a coherent path and reproduces from its seed.", "auto"),
    ("Three cloud decks resolve at the altitudes the atmosphere profile predicts.", "capture"),
    ("Rain falls below the freezing level and snow above it, at the boundary the lapse rate puts it.", "capture"),
    ("Fog fills a valley at dawn, is absent on the ridge above, and is invisible from orbit.", "capture"),
    ("A steep re-entry heats and a shallow one does not, from the density-velocity integral.", "measured"),
    ("Worst-case weather costs no more than 4 ms of GPU time.", "measured"),
    ("Wind speed changes and all five consumers respond within one second.", "auto"),
],

"M05": [
    ("Destroying a power coupling kills exactly the components downstream of it and nothing else.", "auto"),
    ("Heat climbs and affected systems degrade in the order the component graph explains.", "auto"),
    ("Repairing the coupling restores the ship to its stated condition.", "auto"),
    ("Overloading a bus sheds load in the configured priority order.", "auto"),
    ("Loading cargo on one side changes handling measurably in the predicted direction.", "measured"),
    ("Fuel burned over a flight matches thrust integrated over the same flight.", "auto"),
    ("A cold start brings systems up in dependency order; skipping a step prevents the next.", "auto"),
    ("A hull breach vents the compartment behind it and not the rest of the ship.", "auto"),
    ("Every instrument traces to a component reading; a failed sensor makes its gauge unavailable, not wrong.", "auto"),
    ("Shutting down non-essential systems measurably reduces detection range.", "measured"),
    ("A new ship is one data file plus art, requiring no recompile.", "auto"),
],

"M06": [
    ("Disabling one thruster makes the allocator compensate, losing authority in the axis the geometry predicts.", "auto"),
    ("Angular response matches the inertia tensor; off-axis thrust produces the torque the geometry gives.", "auto"),
    ("A winged ship glides unpowered in atmosphere and does not in vacuum.", "auto"),
    ("A deliberate stall departs controlled flight and standard recovery inputs recover it.", "capture"),
    ("A re-entry from vacuum to sea level shows no discontinuity in forces at any altitude.", "measured"),
    ("A hard landing compresses the gear and can damage it; a shallow one does not.", "auto"),
    ("Switching flight-assist mode changes only the controller; the physics underneath is provably identical.", "auto"),
    ("Auto-land places a ship on a pad in wind, repeatably, from any approach.", "auto"),
    ("A full flight is completable on instruments alone with the external view disabled.", "capture"),
    ("Pad to another continent, to vacuum, through re-entry, back to the same pad — cockpit view only.", "capture"),
    ("All three input devices produce identical commanded forces for equivalent input.", "auto"),
    ("A recorded flight replays to an identical trajectory.", "auto"),
],

"M07": [
    ("At one AU from origin, a one-millimetre movement is still one millimetre, for every actor and particle system.", "auto"),
    ("No z-fighting between a hand-held object and a planet in the same frame.", "capture"),
    ("Surface of one planet to a moon of another in one continuous shot, no loading screen and no cut.", "capture"),
    ("No frame exceeds 16.7 ms anywhere along that path.", "measured"),
    ("No allocation spike over budget anywhere along that path.", "measured"),
    ("A ship a thousand kilometres away is a dot; approaching it loses no state and shows no pop.", "capture"),
    ("A ship in transit is pulled out at the position and velocity the interdiction model predicts.", "auto"),
    ("Estimated and actual travel time agree within five percent over twenty sampled routes.", "auto"),
    ("A twenty-minute transit ends with position error inside tolerance and no jitter at any point.", "auto"),
],

"M08": [
    ("Walking stern to cockpit while the ship accelerates, rolls and crosses an atmosphere works without sliding.", "capture"),
    ("An object set on a table inside a manoeuvring ship stays on the table.", "auto"),
    ("Stepping from a pad onto a hovering ship and back shows no jolt, no launch and no lost velocity.", "capture"),
    ("An object at rest inside a manoeuvring ship stays at rest relative to the ship, at any reachable speed.", "auto"),
    ("A dropped object inside a spinning station lands where the rotating frame predicts, not directly below.", "auto"),
    ("Stepping onto a landing pad on a rotating planet inherits the correct frame.", "auto"),
    ("A projectile at any modelled speed always hits the surface it should, over ten thousand trials.", "auto"),
    ("Two levels of grid nesting are stable; the limit is enforced with a clear error.", "auto"),
    ("A recorded physics scenario replays identically at 30, 60 and 144 frames a second.", "auto"),
    ("Ten active grids with two hundred bodies each stays inside the physics budget.", "measured"),
],

"M09": [
    ("Room, ladder, pad in wind, ship, seat, flight, and out again — one continuous shot with no cut or teleport.", "capture"),
    ("No locomotion transition slides the feet, at any speed or direction change.", "auto"),
    ("Feet land on the ground that is there, at its angle, on stairs, slopes and debris.", "auto"),
    ("The same controller produces correct movement at 0.16 g, 1 g and 1.5 g with no special-cased animation.", "auto"),
    ("Crossing a compartment in zero gravity conserves momentum throughout.", "auto"),
    ("First person shows the same body doing the same thing a third-person camera shows.", "capture"),
    ("Boarding a hovering ship and taking the seat involves no teleport and no cut.", "capture"),
    ("A new interactable is one data declaration and inherits highlighting, reach and animation.", "auto"),
    ("Stepping into vacuum without a sealed suit kills on the modelled schedule, with warnings from the suit's components.", "auto"),
    ("Every character mesh and animation clip has a manifest entry naming its source and licence.", "auto"),
    ("An animation asset on disk with no manifest entry fails the build, naming the file.", "auto"),
    ("Two hundred visible characters stay inside the animation budget with no visible LOD switch.", "measured"),
],

"M10": [
    ("Street, building, up through it, roof pad, docked ship, cockpit — no load, no fade.", "capture"),
    ("No frame during that traversal exceeds 16.7 ms.", "measured"),
    ("An interior assembled from the kit has no gap, no z-fight at any seam, and correct collision.", "auto"),
    ("Twenty generated interiors of one purpose are all navigable with no unreachable room.", "auto"),
    ("Every component in a ship's graph has a physical location inside it that can be walked to.", "auto"),
    ("A habitat on an airless world has an airlock and visible life support; the same archetype on a temperate world does not.", "capture"),
    ("A hundred walkable interiors in view cost no more than the handful visible through their openings.", "measured"),
    ("Cutting power to a building darkens its interior and brings up emergency lighting on its own reserve.", "capture"),
    ("Two ships with different component fits produce different cockpits with no hand editing.", "auto"),
    ("Every readout in a generated cockpit traces to a component in that ship's graph; an orphan readout fails the build.", "auto"),
    ("Every control is reachable from the seated position without blocking the sightline to the horizon.", "auto"),
    ("An automated traversal agent covers every reachable point of ten generated interiors without getting stuck.", "auto"),
],

"M11": [
    ("Shown twelve pairs of stills — ours and reference footage, at three distances and three lighting conditions, "
     "in random order — five people who have not seen the project identify ours correctly no more than 60% of the time.", "observed"),
    ("Every material in the project conforms to the documented PBR standard.", "auto"),
    ("A single hull material produces a clean ship and a filthy one from parameters alone.", "capture"),
    ("No shadow acne, peter-panning or cascade seam at cockpit, landscape or planetary scale.", "capture"),
    ("Flying from a dark hangar into daylight adapts smoothly with no overshoot and no clipped scene.", "capture"),
    ("No ghosting on cockpit instruments during rapid motion; no shimmer on terrain at any distance.", "capture"),
    ("No LOD transition is visible at any distance for any asset class.", "capture"),
    ("A character at conversation distance holds up in three lighting conditions.", "capture"),
    ("A cockpit at dawn, noon and night each hold up, with legible instruments in all three.", "capture"),
    ("The same asset with the wear layer disabled and enabled, at identical framing and exposure, is captured as one pair.", "capture"),
    ("Every post effect has a measured cost and a documented reason; the chain is inside its budget.", "measured"),
],

"M12": [
    ("A new ship goes from mesh to flyable with interiors, through the pipeline, with no manual step outside the tool.", "auto"),
    ("A new biome is added the same way and appears correctly at every LOD.", "capture"),
    ("A deliberately broken asset is rejected at import with a message naming the problem.", "auto"),
    ("An imported asset arrives with a full LOD chain and collision matching its silhouette, unattended.", "auto"),
    ("A generated building's exterior openings correspond exactly to its interior layout.", "auto"),
    ("One archetype placed on four environments produces four structurally different buildings from one definition.", "capture"),
    ("A complete star system is authored end to end without editing source.", "observed"),
    ("Memory stays inside budget through a two-hour session visiting every content type.", "measured"),
    ("A packaged build runs the full technical slice with no editor present.", "auto"),
    ("Three ships of visibly different design language come from three data files with no mesh editing.", "capture"),
    ("No hull plate spans a hard curvature break, and no marking lies across a seam.", "auto"),
    ("Greeble density correlates with component placement rather than being uniform noise.", "measured"),
    ("A measured repetition score over a generated street and a generated interior falls below the stated threshold.", "measured"),
    ("No asset anywhere in the project was purchased; every one is generated or carries a free-licence manifest entry.", "auto"),
    ("Every texture in the project has a recorded source and licence.", "auto"),
],

"M13": [
    ("Sixty frames a second with a full city, a docked ship interior and traffic in view.", "measured"),
    ("No frame exceeds 16.7 ms in any of the five fixed scenarios.", "measured"),
    ("A two-hour session ends with the memory profile it started with.", "measured"),
    ("Draw calls in the densest scene are below budget with no visual change.", "measured"),
    ("Visible triangle count in a city falls by an order of magnitude under culling, with no visual change.", "measured"),
    ("Every render pass is inside its budget in the worst scenario.", "measured"),
    ("A full traversal of the slice shows no frame over 16.7 ms.", "measured"),
    ("Three quality levels hit their target frame rate on three hardware profiles.", "measured"),
    ("A deliberate performance regression fails CI.", "auto"),
],

"M14": [
    ("An hour of unscripted free play with no crash.", "observed"),
    ("No loading screen at any point during that hour.", "observed"),
    ("No frame over 16.7 ms during that hour.", "measured"),
    ("An automated audit reports zero placeholder assets in the slice.", "auto"),
    ("Ten hours of mixed play with no crash and no state corruption.", "auto"),
    ("Five external testers complete a free-play session; observations recorded and triaged.", "observed"),
    ("Every transition in the slice — door, ladder, seat, airlock, dock, frame change — is reviewed and none reported as rough.", "observed"),
    ("An ADR records what the technical model proved, what it did not, and what it forces on the simulation plan.", "observed"),
],

"M15": [
    ("A 100,000-tick log replayed twice from one seed produces byte-identical snapshots.", "auto"),
    ("CI fails on divergence and names the first differing tick.", "auto"),
    ("Snapshot-plus-tail and full replay agree byte for byte at 100,000 ticks.", "auto"),
    ("Introducing a float into any authoritative struct fails a test.", "auto"),
    ("Renaming an event struct does not change its wire id.", "auto"),
    ("A log written against schema version 1 replays under version 2.", "auto"),
    ("A one-year log compacts and still replays to the same hash.", "auto"),
    ("No reducer is reachable except through a validated command.", "auto"),
    ("Any event walks back to a root cause with no domain-specific code.", "auto"),
    ("The client renders identically from a live sim and from a replay of it.", "capture"),
],

"M16": [
    ("500 agents run 30 simulated days without error.", "auto"),
    ("Any agent's full history is explained by its needs and beliefs, with no appeal to unobserved world state.", "auto"),
    ("A test proves the planner cannot reach world state.", "auto"),
    ("Two agents holding different beliefs about one fact demonstrably act differently.", "auto"),
    ("An agent with a false belief plans as though it were true.", "auto"),
    ("An agent with a motive to deceive does so, and the false belief propagates and is later contradicted.", "auto"),
    ("Two agents of different competence attempting one action produce measurably different outcome distributions.", "auto"),
    ("An agent demoted to statistical detail and promoted back has a consistent history and an invisible transition.", "auto"),
    ("Ten thousand agents are created, tracked and retired over a simulated year with no leak or orphan.", "auto"),
    ("Any agent's current action is explained in one screen with no code reading.", "observed"),
    ("Two hundred simulated agents are visible as characters going about traceable business, inside budget.", "measured"),
],

"M17": [
    ("An over-extended organisation exhibits drift as subordinates pursuing their own goals.", "auto"),
    ("Drift emerges from agent goals, with no code in the org layer producing it directly.", "auto"),
    ("No branch anywhere in the org crate reads a player flag.", "auto"),
    ("A player organisation is founded through the same API an NPC one is.", "auto"),
    ("The chartering authority's disposition changes measurably as a result of chartering.", "auto"),
    ("Two organisations with identical assets and different doctrine produce measurably different action distributions.", "auto"),
    ("No reputation field exists; two organisations demonstrably hold different beliefs about a third.", "auto"),
    ("An agent defects to a rival when the offer beats their position on their own terms, and the org learns of it late.", "auto"),
    ("Oversight demand and supply are computable, and their ratio predicts the observed drift rate.", "auto"),
],

"M18": [
    ("A supply shock moves prices three systems away with a lag matching travel time.", "auto"),
    ("Ninety simulated days with no negative inventory.", "auto"),
    ("No money created or destroyed outside an explicit mint or sink event.", "auto"),
    ("A three-deep production chain runs for a simulated year with conservation holding at every stage.", "auto"),
    ("Two markets in one system hold different prices for one good, explained by supply and transport cost.", "auto"),
    ("A shipment's arrival matches the route's travel time; destroying it removes the goods from the destination's supply.", "auto"),
    ("A breached contract has consequences propagating as belief and obligation, not as a number changing.", "auto"),
    ("Ten one-year runs from different seeds end with functioning economies and no runaway.", "auto"),
],

"M19": [
    ("One habitat archetype on a temperate world and an airless storm moon produces different composition, cost and maintenance rate.", "auto"),
    ("Cutting a power node causes exactly the downstream failures the graph predicts, in the predicted order.", "auto"),
    ("A district that loses life support evacuates, and its population appears elsewhere rather than vanishing.", "auto"),
    ("All six first-person tells are observable by walking through a running city.", "capture"),
    ("Each tell is driven by simulation state rather than triggered for display.", "auto"),
    ("A crew fed a false belief about a fault location goes to the wrong district, and the right one stays dark.", "auto"),
    ("An understaffed organisation's backlog appears as public contracts; taking one clears the work order.", "auto"),
    ("A construction project is identifiable by sight at four stages of completion.", "capture"),
    ("A project on a remote world is delayed by a late convoy and by a storm, both traceable.", "auto"),
    ("A site's environment matches what the renderer and weather model report at that location.", "auto"),
],

"M20": [
    ("A coalition forms in a scenario with no code that decides a coalition should form.", "auto"),
    ("Each member can state why it joined, from its own threat model.", "auto"),
    ("A fabricated belief fed to one member fractures the coalition.", "auto"),
    ("A coalition dissolves both because its target fell below threshold and because a partner rose above one.", "auto"),
    ("A test proves the threat model cannot reach ground truth.", "auto"),
    ("Alarm responds correctly to each of its five inputs in isolation.", "auto"),
    ("Each of the four counterplays — hide, bribe, fracture, be useful — demonstrably breaks or prevents a coalition.", "auto"),
    ("Establishing a supply relationship measurably lowers the customer's alarm; severing it raises it.", "auto"),
    ("Observable event rate scales with organisation size; a large one cannot reach a small one's concealment.", "auto"),
    ("Twenty one-year runs produce neither a runaway hegemon nor a frozen balance.", "auto"),
],

"M21": [
    ("Every generated mission traces to a named tension between real parties.", "auto"),
    ("No generated mission has a stake that is only a payment.", "auto"),
    ("Removing a tension removes the missions generated from it.", "auto"),
    ("A directive left alone for a simulated week resolves through subordinates.", "auto"),
    ("A directive given to an incompetent or disloyal delegate produces a different outcome, explained by the agent.", "auto"),
    ("The same task done personally and by directive differ in all four stated ways, each measurable.", "auto"),
    ("A player directive produces a mission on the open board for an NPC, with no special code path.", "auto"),
    ("The same tension produces different offers to agents of different standing.", "auto"),
    ("A one-year run produces missions across all tension classes, none exceeding a stated share.", "auto"),
],

"M22": [
    ("Opening Command mid-flight does not pause the world or stop the ship.", "capture"),
    ("A test proves the Command data path cannot reach world state.", "auto"),
    ("A fact nobody has observed does not appear on the map.", "auto"),
    ("A report three days stale is visibly stale and its source is inspectable.", "capture"),
    ("Two contradictory reports are both shown with their sources rather than one silently winning.", "capture"),
    ("Every directive type is issuable from Command and resolves identically to one issued programmatically.", "auto"),
    ("A hostile agent can find and act on the player's body while Command is open.", "auto"),
    ("A hundred simultaneous events produce a legible summary rather than a hundred notifications.", "observed"),
],

"M23": [
    ("One simulated year runs unattended with no crash and no deadlock.", "auto"),
    ("No unbounded quantity anywhere in that year.", "auto"),
    ("No runaway monopoly and no dead economy at the end of it.", "auto"),
    ("No organisation exceeds a stated ceiling on fleet size.", "auto"),
    ("No agent is stuck in a loop.", "auto"),
    ("A collapse anywhere in the year-old world is traceable back four causes from the dump.", "auto"),
    ("The same engagement fought in person and resolved in Command produces outcomes from one distribution.", "auto"),
    ("The simulation tick stays inside budget with fifty thousand agents across thirty-two systems.", "measured"),
    ("A one-year world saves and reloads in under thirty seconds with a matching hash.", "auto"),
    ("Killing the simulation mid-session degrades the client visibly and recovers with no invented facts.", "auto"),
],

"M24": [
    ("A player who has never seen the game reaches a chartered organisation with no tutorial.", "observed"),
    ("A coalition forms against that player's organisation in ordinary play.", "observed"),
    ("The player can see from Command why it formed.", "observed"),
    ("Playtesters find at least two of the four counterplays unprompted.", "observed"),
    ("A new player has a reason to move within sixty seconds of gaining control.", "observed"),
    ("A player can earn a living from generated work alone for three sessions without repetition becoming visible.", "observed"),
    ("Playtesters reach the point of using Command without being told to.", "observed"),
    ("Median time from nothing to the coalition falls inside the intended band across ten playtests.", "measured"),
    ("Ten testers complete the arc; findings recorded, triaged, and either fixed or explicitly deferred.", "observed"),
    ("Twenty hours of mixed play with no crash, no corruption and no soft lock.", "auto"),
],

}
