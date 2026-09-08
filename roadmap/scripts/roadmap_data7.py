# -*- coding: utf-8 -*-
"""M19-M24: sites and city life, power and coalitions, missions, Command,
integration, and the vertical slice.

The last block. By here the technical model is proven and the simulation
frameworks exist; what remains is the systems that make it a game about power,
and then everything running at once.
"""

# ---------------------------------------------------------------------------
# M19 - sites, construction and city life.
# ---------------------------------------------------------------------------

M19 = [
    dict(title="Site and environment profile",
         detail="Atmosphere, pressure, temperature, weather, gravity and local resources as "
                "the data a site is defined by — read from the physical fields M04 already "
                "computes rather than authored separately.",
         acceptance="A site's environment matches what the renderer and the weather model "
                    "say at that location, always.",
         days=3, refs=["LW SS7.2"]),
    dict(title="Claims and registration",
         detail="Claiming land is registering with an authority, which creates a "
                "relationship. Claims far from authority are cheap and unprotected.",
         acceptance="A claim registered with an authority changes that authority's "
                    "disposition; an unregistered one is defensible only by force.",
         days=3, refs=["LW SS7.5"]),
    dict(title="Structure archetypes and module derivation",
         detail="A building is an archetype plus the modules its environment demands. The "
                "rule from LW SS7.2 as code: sealed envelope, anchoring, shielding, thermal "
                "plant, each derived rather than chosen.",
         acceptance="One archetype on four environments produces four different structures "
                    "with different capital cost, power load and maintenance rate.",
         days=5, refs=["LW SS7.2"]),
    dict(title="Utility network model: power",
         detail="Generation, distribution and consumption as a graph with capacity and "
                "throughput. The network everything else depends on.",
         acceptance="Demand exceeding supply sheds load in priority order, and the shortfall "
                    "is attributable to a node or an edge.",
         days=4, refs=["LW SS7.3"]),
    dict(title="Utility networks: life support, water, transit",
         detail="The other three, on the same graph machinery. Life support exists only "
                "where the environment demands it, which is why some cities are fragile.",
         acceptance="All four networks run on one implementation, and a city on a breathable "
                    "world has three of them.",
         days=4, refs=["LW SS7.3"]),
    dict(title="Cascade failure",
         detail="Lose power, lose life support, lose the district. One directed cascade, and "
                "it is enough for all of city crisis modelling.",
         acceptance="Cutting a power node causes exactly the downstream failures the graph "
                    "predicts, in the order it predicts.",
         days=3, refs=["LW SS7.3"]),
    dict(title="Construction projects as directives",
         detail="A project has a site, a design, materials that must physically arrive, "
                "labour that must be present, and a schedule that weather and people can "
                "disrupt.",
         acceptance="A project on a remote world is delayed by a late convoy and by a "
                    "storm, and both delays are traceable.",
         days=5, refs=["LW SS7.4"]),
    dict(title="Construction as a visible process",
         detail="Foundations, scaffolding, partial structures and crews. A building under "
                "construction is a place, not a progress bar.",
         acceptance="A project is identifiable by sight at four stages of completion.",
         days=4, refs=["LW SS7.1"]),
    dict(title="Structure condition and degradation",
         detail="Everything wears, faster in hostile environments, and the wear is visible "
                "before it is fatal.",
         acceptance="Degradation rate tracks the environment, and a neglected structure is "
                    "visibly neglected before it fails.",
         days=3, refs=["LW SS7.6"]),
    dict(title="Work orders and maintenance crews",
         detail="Degradation produces work orders; crews are agents with the maintain goal, "
                "their own competence, and their own beliefs about which conduit is broken.",
         acceptance="A crew fed a false belief about a fault location goes to the wrong "
                    "district, and the right one stays dark.",
         days=4, refs=["LW SS7.6"]),
    dict(title="Unhandled work becomes contracts",
         detail="What an organisation cannot staff goes on the open board. This is where a "
                "new player finds their first job, long before they know there is a "
                "strategic layer generating it.",
         acceptance="An understaffed organisation's backlog appears as public contracts, "
                    "and taking one clears the work order.",
         days=3, refs=["LW SS7.6", "LW SS5"]),
    dict(title="District state and population",
         detail="Who lives and works where, what they need, and what happens when the "
                "district cannot provide it — including leaving.",
         acceptance="A district that loses life support evacuates, and the population "
                    "appears somewhere else rather than vanishing.",
         days=4, refs=["LW SS7"]),
    dict(title="City growth and decline",
         detail="Settlements grow where there is reason and shrink where there is not, "
                "driven by the economy rather than by a curve.",
         acceptance="A city grows a district in response to sustained economic demand and "
                    "abandons one when the demand goes.",
         days=4, refs=["LW SS7"]),
    dict(title="The six first-person tells",
         detail="LW SS7.1, implemented and verified: dark districts, masks indoors, shutters "
                "in a storm, deferred maintenance, contested markers, scaffolding. A "
                "mechanic without a tell does not ship.",
         acceptance="All six are observable by walking through a running city, and each is "
                    "driven by the simulation rather than triggered for display.",
         days=5, refs=["LW SS7.1"]),
    dict(title="Utilities as military targets",
         detail="A holding whose power fails is a holding that dies, which makes utilities "
                "worth attacking and worth defending.",
         acceptance="Destroying a generator has the strategic effect the cascade model "
                    "implies, and the owner responds.",
         days=3, refs=["LW SS7.2"]),
    dict(title="Settlement inspection tools",
         detail="Networks, loads, conditions, work orders, population and projects, "
                "inspectable for any settlement.",
         acceptance="Any settlement failure is diagnosable from the tools without reading "
                    "code.",
         days=3, refs=["SS13"]),
    dict(title="Playable proof: cut the power to a district",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Life support fails, crews respond, people put masks on, and the "
                    "district evacuates — all visible from the street.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M20 - power, threat and coalitions.
# ---------------------------------------------------------------------------

M20 = [
    dict(title="Believed power estimation",
         detail="What an organisation thinks another is worth fearing, assembled from "
                "beliefs about fleet, holdings, income and allies — every one of them "
                "capable of being wrong.",
         acceptance="Two organisations hold different estimates of a third, and each "
                    "estimate is explained by what that observer has actually heard.",
         days=4, refs=["LW SS3.1"]),
    dict(title="Threat model and alarm",
         detail="Alarm from believed power, proximity, dependency, rivalry history and the "
                "observer's own strength. The formula in LW SS3.1, and it reads belief "
                "rather than truth.",
         acceptance="A test proves the threat model cannot reach ground truth, and alarm "
                    "responds correctly to each of its five inputs in isolation.",
         days=4, refs=["LW SS3.1"]),
    dict(title="Dependency: you do not attack your supplier",
         detail="Economic dependency suppresses alarm, which makes being useful a genuine "
                "alternative to being feared — and a different way to play.",
         acceptance="Establishing a supply relationship measurably lowers the customer's "
                    "alarm, and severing it raises it again.",
         days=3, refs=["LW SS3.1"]),
    dict(title="Coalition formation",
         detail="When several organisations independently cross alarm about the same subject "
                "and are not blocked by mutual hostility, they discover each other. The pact "
                "is a coincidence of alarm, not a script.",
         acceptance="A coalition forms in a scenario with no code that decides a coalition "
                    "should form, and its members can each state why they joined.",
         days=5, refs=["LW SS3.1"]),
    dict(title="Coalition instability and dissolution",
         detail="Every member runs the threat model on every other member. A coalition "
                "dissolves when the target falls below threshold or a partner rises above "
                "one.",
         acceptance="A coalition dissolves for both reasons in separate scenarios.",
         days=4, refs=["LW SS3.1"]),
    dict(title="Counterplay: hiding, bribing, fracturing, being useful",
         detail="The four strategies in LW SS3.1, each of which must actually work: suppress "
                "information, redirect a member's alarm, feed one member a belief about "
                "another, or become indispensable.",
         acceptance="Each of the four demonstrably breaks or prevents a coalition in a "
                    "scenario.",
         days=5, refs=["LW SS3.1"]),
    dict(title="Visibility: the informational ceiling",
         detail="Assets generate observable events. The larger an organisation, the more "
                "surface it presents and the harder believed power is to suppress.",
         acceptance="Observable event rate scales with organisation size, and a large "
                    "organisation cannot reach the concealment a small one can.",
         days=3, refs=["LW SS3.2"]),
    dict(title="Legitimacy: the economic ceiling",
         detail="A holding taken by force needs a garrison forever; one taken by contract "
                "needs an accountant. Two cost curves that cross.",
         acceptance="Coercive and commercial acquisition of the same holding have costs "
                    "whose curves cross at a computable point.",
         days=3, refs=["LW SS3.3"]),
    dict(title="Hostility, war and peace states",
         detail="Relationships between organisations as states with entry and exit "
                "conditions, driven by alarm and by events rather than declared.",
         acceptance="A war starts from a traceable sequence of events and ends when the "
                    "conditions that sustained it stop holding.",
         days=4, refs=["LW SS3"]),
    dict(title="Intelligence operations",
         detail="Deliberately acquiring and denying information: agents placed, sources "
                "cultivated, feeds poisoned. The belief system's offensive use.",
         acceptance="An intelligence operation measurably changes a rival's threat model, "
                    "and can be detected and countered.",
         days=4, refs=["SS4"]),
    dict(title="Power inspection tools",
         detail="Every organisation's threat model, alarm levels, coalitions and their "
                "reasons, inspectable and explicable.",
         acceptance="Any coalition's formation is explained from the tools without reading "
                    "code.",
         days=3, refs=["SS13"]),
    dict(title="Power balance testing",
         detail="Long runs looking for runaway victory and for stagnation. The ceiling is "
                "supposed to be a plateau, not a wall and not a cliff.",
         acceptance="Twenty one-year runs produce neither a runaway hegemon nor a frozen "
                    "balance.",
         days=4, refs=["LW SS3"]),
    dict(title="Playable proof: provoke a coalition, then fracture it",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Rivals independently cross alarm and coordinate; a fabricated belief "
                    "fed to one member then breaks the pact.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M21 - missions and directives.
# ---------------------------------------------------------------------------

M21 = [
    dict(title="Tension detection",
         detail="Scan the world for someone who believes something, wants something, and "
                "lacks the means. Everything a mission can be starts here.",
         acceptance="Tension detection returns results in a running world, and each names "
                    "the parties, the belief and the shortfall.",
         days=4, refs=["LW SS5"]),
    dict(title="Mission grammar",
         detail="Turning a tension into something a person could be paid to do: goal, "
                "constraints, stake, deadline, payer. A grammar, not a template list.",
         acceptance="Every generated mission traces to a specific tension, and removing the "
                    "tension removes the mission.",
         days=5, refs=["LW SS5"]),
    dict(title="Stakes: nothing is generated that does not matter",
         detail="Every mission changes the world on success or failure. A mission whose "
                "stake is only money should not have been emitted.",
         acceptance="A test rejects any mission whose only stake is a payment, and every "
                    "generated mission's stake is a state change.",
         days=3, refs=["LW SS5.2"]),
    dict(title="Offer selection: who would offer this to you",
         detail="A mission is offered based on what the offering party believes about the "
                "recipient. Nobody hires an unknown for a decapitation.",
         acceptance="The same tension produces different offers to agents of different "
                    "standing, and no offer exceeds what the offerer believes the recipient "
                    "capable of.",
         days=4, refs=["LW SS5.1"]),
    dict(title="Mission resolution and consequence",
         detail="Success, failure and partial outcomes feeding back as events, beliefs and "
                "obligations. The consequence is the point.",
         acceptance="Completing a mission changes the world in the way its stake stated, and "
                    "the change propagates as belief rather than as fact.",
         days=4, refs=["LW SS5.2"]),
    dict(title="Directive representation",
         detail="Intent, assets, constraints and a delegate. A standing order the simulation "
                "resolves whether or not anyone is watching.",
         acceptance="A directive is issued, persists across a save, and continues resolving.",
         days=3, refs=["LW SS4.1"]),
    dict(title="Directive resolution through agents",
         detail="A directive is executed by the delegate's own planner, on the delegate's "
                "own beliefs, with their competence and their interests. This is where drift "
                "comes from and it is not a separate system.",
         acceptance="A directive given to an incompetent or disloyal delegate produces a "
                    "different outcome than the same directive to a good one, with the "
                    "difference explained by the agent.",
         days=5, refs=["LW SS4.1", "LW SS2.3"]),
    dict(title="The presence dividend",
         detail="Doing something yourself is better in kind, not by a percentage: current "
                "information, the ability to change your mind, nobody skimming, and morale.",
         acceptance="The same task executed personally and by directive differ in all four "
                    "stated ways, each measurable.",
         days=4, refs=["LW SS4.2"]),
    dict(title="Player directives become other people's missions",
         detail="At the top you generate the tension. A directive that moves a fleet creates "
                "the shortfall a courier is hired against, through the same generator.",
         acceptance="A player directive produces a mission on the open board for an NPC, "
                    "with no special code path.",
         days=3, refs=["LW SS5.1"]),
    dict(title="Contract board and mission presentation",
         detail="How offers reach a player: boards, brokers, direct approach, and rumour. "
                "What is visible depends on where you are and who trusts you.",
         acceptance="Two players in different places with different standing see different "
                    "boards, and the difference is explained by belief.",
         days=4, refs=["LW SS5.1"]),
    dict(title="Mission inspection tools",
         detail="Every generated mission with its tension, parties, stake and reasoning, "
                "plus what happened to the ones nobody took.",
         acceptance="Any mission on any board is explicable from the tools.",
         days=3, refs=["SS13"]),
    dict(title="Mission quality and variety testing",
         detail="Long runs checking that generated missions are varied, that stakes matter, "
                "and that no degenerate pattern dominates.",
         acceptance="A one-year run produces missions across all tension classes with no "
                    "single pattern exceeding a stated share.",
         days=4, refs=["SS12"]),
    dict(title="Playable proof: a directive left alone for a week",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="It resolves through subordinates, correctly or otherwise, and the "
                    "outcome is explained by the delegate's beliefs and competence.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M22 - presence and Command.
# ---------------------------------------------------------------------------

M22 = [
    dict(title="Command lens framework",
         detail="A lens over the simulation, not a mode. It does not pause the world and it "
                "does not move your body — which is still standing wherever you left it.",
         acceptance="Opening Command mid-flight leaves the ship flying and the world "
                    "running, and closing it returns to an unchanged cockpit.",
         days=4, refs=["LW SS4"]),
    dict(title="The map is built from reports, not ground truth",
         detail="Everything displayed is something somebody reported. A Command lens showing "
                "truth would destroy the entire information design.",
         acceptance="A test proves the Command data path cannot reach world state, and an "
                    "unobserved fact does not appear on the map.",
         days=4, refs=["LW SS4.3"]),
    dict(title="Staleness and confidence in the interface",
         detail="A three-day-old report looks three days old. Confidence, provenance and "
                "hop count are visible rather than flattened into a number.",
         acceptance="A stale report is visibly stale and its source is inspectable.",
         days=3, refs=["LW SS4.3"]),
    dict(title="Issuing directives from Command",
         detail="Select assets, choose intent, set constraints, assign a delegate. The UI "
                "over M21's directive system.",
         acceptance="Every directive type is issuable from Command and resolves identically "
                    "to one issued programmatically.",
         days=4, refs=["LW SS4.1"]),
    dict(title="Reports and the intelligence view",
         detail="What your people have told you, when, and how much you should believe it — "
                "including reports that contradict each other.",
         acceptance="Two contradictory reports are both shown with their sources rather than "
                    "one silently winning.",
         days=3, refs=["SS4"]),
    dict(title="Organisation management screens",
         detail="Members, roles, finances, holdings, doctrine and the oversight deficit — "
                "the last of which is where a player learns what drift is.",
         acceptance="An over-extended organisation shows the deficit and the player can act "
                    "on it before drift becomes damage.",
         days=4, refs=["LW SS2.3"]),
    dict(title="Scale transitions in the interface",
         detail="From a district to a system to the region, without a load and without "
                "losing context.",
         acceptance="Zooming from a street to a region is continuous and never loses the "
                    "selected entity.",
         days=3, refs=["LW SS4"]),
    dict(title="Your body while you are in Command",
         detail="You are still standing somewhere, and someone might be looking for you. "
                "The tension that stops Command being a safe menu.",
         acceptance="A hostile agent can find and act on the player's body while Command is "
                    "open.",
         days=3, refs=["LW SS4"]),
    dict(title="Notifications and interruption",
         detail="What is worth telling the player, when, and how not to bury it. An "
                "organisation with four hundred problems generates four hundred events.",
         acceptance="A hundred simultaneous events produce a legible summary rather than a "
                    "hundred notifications.",
         days=3, refs=["LW SS4"]),
    dict(title="Playable proof: Command mid-flight",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="The world does not pause, the ship does not stop, a stale report is "
                    "visibly stale, and an unobserved fact is absent.",
         days=2, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M23 - the living world, integrated.
# ---------------------------------------------------------------------------

M23 = [
    dict(title="Combat resolution shared across scales",
         detail="Personal, ship and asset-scale violence through one model, so a raid "
                "resolves the same way whether the player fights it or reads about it.",
         acceptance="The same engagement fought in person and resolved in Command produces "
                    "outcomes from one distribution.",
         days=6, refs=["LW SS1.1"]),
    dict(title="Bodyguards, escorts and security",
         detail="The things that happen to people who become worth attacking, expressed as "
                "agents with the protect goal rather than as a stat.",
         acceptance="A bodyguard interposes because its planner decided to, and can fail, "
                    "be bribed, or be elsewhere.",
         days=4, refs=["LW SS1.1"]),
    dict(title="Raids and invasions",
         detail="Organisation-scale operations against holdings, resolved through directives "
                "and agents, with the utility-network consequences from M19.",
         acceptance="A raid on a holding takes it, damages it, or fails, and the outcome "
                    "propagates through ownership, belief and the economy.",
         days=5, refs=["LW SS3"]),
    dict(title="Cross-system integration pass",
         detail="Every framework wired to every other one it should touch, and the seams "
                "found by running rather than by reading.",
         acceptance="A one-month run exercises every framework and every interaction between "
                    "them without an error.",
         days=6, refs=["ARCH SS2"]),
    dict(title="Long-run stability: one simulated year",
         detail="The test the whole simulation plan is aimed at. Runaways, deadlocks, drift "
                "to zero, and agents stuck in loops all show up here and nowhere earlier.",
         acceptance="A simulated year completes unattended with no crash, no deadlock and no "
                    "unbounded quantity.",
         days=6, refs=["SS12"]),
    dict(title="Emergence review and tuning",
         detail="Read a year of world history and ask whether it is interesting. Tune the "
                "mechanisms rather than adding new ones — that discipline is what stops the "
                "system becoming unexplainable.",
         acceptance="A year's history is readable as a story with causes, and every "
                    "adjustment made is to a parameter with a stated meaning.",
         days=5, refs=["SS12"]),
    dict(title="Simulation performance at scale",
         detail="Fifty thousand agents across thirty-two systems inside the tick budget, "
                "with the level-of-detail system from M16 carrying the load.",
         acceptance="The tick budget holds at target scale, with cost attributable per "
                    "crate.",
         days=5, refs=["SS14"]),
    dict(title="Save, load and persistence at scale",
         detail="A world with a year of history saves and loads without losing state and "
                "without taking minutes.",
         acceptance="A one-year world round-trips in under thirty seconds with a matching "
                    "hash.",
         days=4, refs=["SS3"]),
    dict(title="Failure and recovery",
         detail="What happens when the simulation stalls, diverges or crashes, with the "
                "client degrading honestly rather than inventing state.",
         acceptance="Killing the simulation mid-session degrades the client visibly and "
                    "recovers on restart with no invented facts.",
         days=3, refs=["ARCH Rule 1"]),
    dict(title="World dump at full scale",
         detail="The SS12 dump, still legible with a year of history and thirty-two systems "
                "in it. If it is not legible, the world is not understandable.",
         acceptance="A collapse anywhere in a year-old world is traceable back four causes "
                    "from the dump.",
         days=3, refs=["SS12"]),
    dict(title="Playable proof: a year, unattended",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="No runaway monopoly, no dead economy, no organisation with a thousand "
                    "ships, no agent stuck in a loop, and a dump that still explains itself.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M24 - vertical slice: nothing to coalition.
# ---------------------------------------------------------------------------

M24 = [
    dict(title="Arrival: the opening",
         detail="A player with nothing, in a place, with something to do within a minute. No "
                "cutscene, no tutorial, no character creation wall.",
         acceptance="A new player has a reason to move within sixty seconds of gaining "
                    "control.",
         days=4, refs=["LW SS1.1"]),
    dict(title="The early loop: work, information, a little money",
         detail="Cargo, messages, and what you learn on the way — the design's premise made "
                "playable at the smallest scale.",
         acceptance="A player can earn a living from generated work alone for three sessions "
                    "without repetition becoming visible.",
         days=5, refs=["LW SS1.1", "LW SS5"]),
    dict(title="Building a crew",
         detail="Hiring the first people, who are agents with their own goals and can leave, "
                "and learning what delegation costs.",
         acceptance="A hired agent executes work independently, and does it badly if hired "
                    "badly.",
         days=4, refs=["LW SS2.3"]),
    dict(title="Founding the organisation",
         detail="Capital, charter and registration, and the relationship with the chartering "
                "authority that comes with it.",
         acceptance="A player founds an organisation and the chartering authority's "
                    "disposition changes measurably as a result.",
         days=4, refs=["LW SS2.2"]),
    dict(title="First holding: claim, build, maintain",
         detail="Claim ground, build on it, and discover that it needs power, people and "
                "attention.",
         acceptance="A player builds a holding, has it degrade through neglect, and repairs "
                    "it, all through the M19 systems.",
         days=5, refs=["LW SS7"]),
    dict(title="Being noticed",
         detail="The point at which rivals form beliefs about you and act on them — the "
                "first time the world pushes back.",
         acceptance="A growing player organisation becomes the subject of a rival's threat "
                    "model, and the player can see evidence of it before it acts.",
         days=4, refs=["LW SS3.1"]),
    dict(title="The Command transition",
         detail="Not a mode unlock: the Command lens goes from empty to necessary because "
                "the player owns too much to be everywhere.",
         acceptance="Playtesters reach the point of using Command without being told to, "
                    "because presence alone stopped being sufficient.",
         days=4, refs=["LW SS4"]),
    dict(title="The coalition",
         detail="The end of the arc: several organisations independently conclude the player "
                "is the problem and discover each other.",
         acceptance="A coalition forms against a player organisation in ordinary play, and "
                    "the player can see why from Command.",
         days=4, refs=["LW SS3.1"]),
    dict(title="Counterplay made discoverable",
         detail="Hide, bribe, fracture, be useful — all four available and all four findable "
                "without a tutorial explaining them.",
         acceptance="Playtesters find at least two of the four unprompted.",
         days=4, refs=["LW SS3.1"]),
    dict(title="Onboarding without a tutorial",
         detail="The systems teach themselves through consequence and through the interface, "
                "because a tutorial for this design would be an hour long.",
         acceptance="A new player reaches the organisation stage with no explicit tutorial "
                    "and can state the rules they learned.",
         days=5, refs=["LW SS1"]),
    dict(title="Full playtest cycle",
         detail="External testers through the whole arc, watched, with findings triaged and "
                "acted on.",
         acceptance="Ten testers complete the arc; their findings are recorded, triaged and "
                    "either fixed or explicitly deferred.",
         days=5, refs=["SS13"]),
    dict(title="Balance pass across the whole arc",
         detail="Pacing from nothing to coalition: too fast is meaningless, too slow is a "
                "second job.",
         acceptance="Median time to the coalition falls inside the intended band across ten "
                    "playtests.",
         days=5, refs=["LW SS3"]),
    dict(title="Final stability and polish",
         detail="Crashes, corruption, soft locks, and every rough edge testers named.",
         acceptance="Twenty hours of mixed play with no crash, no corruption and no soft "
                    "lock.",
         days=5, refs=["SS13"]),
    dict(title="Playable proof: nothing to coalition, one save",
         detail="Rule 6, and the end of the roadmap as written. Everything since M00 exists "
                "to make this sentence true.",
         acceptance="A player who has never seen the game goes from nothing to a chartered "
                    "organisation to a coalition forming against them, in one save, with no "
                    "tutorial explaining any of the systems.",
         days=4, refs=["ARCH Rule 6"]),
]
