# -*- coding: utf-8 -*-
"""M15-M18: simulation core, agents, organisations, economy.

The living world goes on top of a proven technical model. Everything here is a
framework: an agent framework rather than an NPC, an organisation framework
rather than a faction, because content built before the mechanism is content
that gets rebuilt.
"""

# ---------------------------------------------------------------------------
# M15 - simulation core and determinism.
# ---------------------------------------------------------------------------

M15 = [
    dict(title="Cargo workspace and the crate layering test",
         detail="The crates from ARCH SS2, created as the real simulation is written "
                "rather than as a refactor of the frozen Phase 0 spike. Plus the test that "
                "parses the workspace manifests and fails the build on a dependency "
                "pointing upward — documentation nobody enforces is a wish.",
         acceptance="cargo tree shows exactly the intended edges, and adding one from "
                    "ledger-org to ledger-power fails CI with a message naming the rule.",
         days=2, refs=["ARCH SS2.1"]),
    dict(title="Port what the Phase 0 spike proved",
         detail="Fixed-point arithmetic, the seeded generator, the event fold with causes, "
                "and the belief graph with its tuned constants — moved into their crates "
                "and re-tested. src/README.md lists what is worth carrying; the structure "
                "is not on the list.",
         acceptance="The belief propagation tests from the spike pass against the new "
                    "crates, with the same constants and the same metric outcomes.",
         days=4, refs=["SS4", "SS3"]),
    dict(title="Event type registry with stable wire ids",
         detail="Events are the log format and the log outlives every refactor. Each event "
                "gets an id that never changes and a version.",
         acceptance="Renaming an event struct does not change its id, and a test proves it.",
         days=2, refs=["SS3"]),
    dict(title="Reducer contract and per-crate registration",
         detail="Each domain crate contributes reducers over its own slice of state; "
                "ledger-world composes them without knowing what they do.",
         acceptance="Adding a crate's reducer requires no edit to ledger-world beyond one "
                    "registration line.",
         days=3, refs=["ARCH SS2"]),
    dict(title="Snapshot format with content hashing",
         detail="A snapshot is bytes plus a hash of those bytes, so divergence is detected "
                "rather than described.",
         acceptance="Two runs from one seed produce identical hashes and a one-bit change is "
                    "detected.",
         days=3, refs=["SS3"]),
    dict(title="Replay from snapshot plus tail",
         detail="Load a snapshot, apply subsequent events, and arrive where a full replay "
                "from zero arrives.",
         acceptance="Snapshot-plus-tail and full replay agree byte for byte at 100,000 ticks.",
         days=3, refs=["SS3"]),
    dict(title="Determinism harness in CI",
         detail="Run the same scenario twice in separate processes and diff. Cross-platform "
                "where CI allows, because that is where float creep hides.",
         acceptance="CI fails on divergence and names the first differing tick.",
         days=3, refs=["ARCH Rule 5"]),
    dict(title="Static check: no floating point in authoritative state",
         detail="A test that walks the type graph of State and rejects f32 and f64.",
         acceptance="Introducing a float into any authoritative struct fails a test.",
         days=2, refs=["SS3"]),
    dict(title="Tick scheduler with fixed step and bounded catch-up",
         detail="Simulation time advances in fixed steps regardless of wall clock, with "
                "catch-up bounded so a slow frame cannot spiral.",
         acceptance="Stalling the process for ten seconds leaves the sim consistent rather "
                    "than ahead of itself.",
         days=3, refs=["SS3"]),
    dict(title="Command intake as the only entry point",
         detail="External intent becomes a validated command, which becomes events. No "
                "system mutates state directly.",
         acceptance="A test proves no reducer is reachable except through a command.",
         days=3, refs=["ARCH Rule 1"]),
    dict(title="Scenario definition format",
         detail="A seed, an initial state and a script of commands. Every acceptance test "
                "from here to the end of the roadmap is written as one.",
         acceptance="A scenario file runs headless and produces a stable dump.",
         days=3, refs=["SS13"]),
    dict(title="Event log persistence and compaction",
         detail="Write the log, read it back, and compact old regions behind snapshots so it "
                "does not grow without bound.",
         acceptance="A one-year log compacts and still replays to the same hash.",
         days=3, refs=["SS3"]),
    dict(title="Event schema migration",
         detail="Old logs must replay after the schema moves. Versioned events with upgrade "
                "functions.",
         acceptance="A log written against version 1 replays under version 2.",
         days=3, refs=["SS3"]),
    dict(title="Causal trace: why did this happen",
         detail="Every event carries its cause, so tracing a collapse back four causes "
                "becomes a property of the log rather than of one printer.",
         acceptance="Any event walks back to a root cause with no domain-specific code.",
         days=3, refs=["SS12"]),
    dict(title="Sim-to-client wire contract",
         detail="The snapshot and command formats the client's bridge module speaks, "
                "versioned, with a recorded stream to test against.",
         acceptance="A recorded stream replays into the client and produces the same world.",
         days=3, refs=["ARCH Rule 3"]),
    dict(title="Per-crate tick budget and profiling",
         detail="Where the tick goes, per crate, so the answer at M23 is measured rather "
                "than guessed.",
         acceptance="A run prints tick cost by crate and flags any crate over budget.",
         days=2, refs=["SS14"]),
    dict(title="Playable proof: the client renders a replayed world",
         detail="Rule 6. Feed the client a snapshot stream from a replayed log rather than a "
                "live sim and confirm the boundary is honest.",
         acceptance="The client renders identically from a live sim and from a replay of it.",
         days=2, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M16 - agent framework. The longest simulation milestone and the one every
# other system reduces to.
# ---------------------------------------------------------------------------

M16 = [
    dict(title="Agent identity, attributes and lifecycle",
         detail="Who someone is, what they are capable of, and how they enter and leave the "
                "world. Birth, hiring, death and disappearance are all lifecycle events.",
         acceptance="Ten thousand agents are created, tracked and retired over a simulated "
                    "year with no leak and no orphan.",
         days=3, refs=["LW SS6"]),
    dict(title="Needs and drives",
         detail="Income, safety, standing, belonging, grudge — the pressures that make an "
                "agent want anything. Needs decay and are satisfied; they are not a mood.",
         acceptance="An agent deprived of income changes behaviour in a way traceable to "
                    "the need, and satisfying it changes behaviour back.",
         days=4, refs=["LW SS6"]),
    dict(title="Goal representation and prioritisation",
         detail="A goal is a desired world state with a weight. Agents hold several and "
                "reprioritise as needs and beliefs change.",
         acceptance="An agent's goal set is inspectable and its ordering is explained by its "
                    "needs.",
         days=4, refs=["LW SS6"]),
    dict(title="Planner over world state",
         detail="From a goal and a set of available actions, produce a plan. Hierarchical "
                "and interruptible, because plans in this world get invalidated constantly.",
         acceptance="An agent plans a multi-step route to a goal, is interrupted, and "
                    "replans without restarting from nothing.",
         days=6, refs=["LW SS6"]),
    dict(title="Agents plan against beliefs, not world state",
         detail="The single most important property in the framework. A planner that reads "
                "world state is a planner that is omniscient, and an omniscient agent "
                "cannot be deceived, which destroys the entire information design.",
         acceptance="A test proves the planner cannot reach world state, and an agent with "
                    "a false belief plans as though it were true.",
         days=4, refs=["LW SS6", "SS4"]),
    dict(title="Perception and belief formation",
         detail="Agents observe what they can see, are told what others tell them, and form "
                "beliefs with provenance and confidence. The existing belief graph gets its "
                "producers.",
         acceptance="An agent that witnesses an event believes it firsthand; one told about "
                    "it believes it at a lower confidence with a named source.",
         days=5, refs=["SS4"]),
    dict(title="Action library and execution",
         detail="The vocabulary of what an agent can do — move, work, trade, talk, fight, "
                "wait — with preconditions, effects, duration and cost. Data, not a switch "
                "statement.",
         acceptance="Adding an action is one definition and the planner uses it with no "
                    "other change.",
         days=4, refs=["ARCH Rule 7"]),
    dict(title="Schedules and routine",
         detail="Agents have lives: work, rest, travel, obligations. Routine is what makes a "
                "world look inhabited rather than idle.",
         acceptance="A day in an agent's life is legible from a trace and repeats with "
                    "variation rather than exactly.",
         days=4, refs=["LW SS6"]),
    dict(title="Competence and failure",
         detail="Agents differ in skill and fail at things. A plan that assumed success and "
                "got failure must be recoverable from.",
         acceptance="Two agents with different competence attempting the same action "
                    "produce measurably different outcome distributions.",
         days=3, refs=["LW SS2.3"]),
    dict(title="Loyalty, relationships and social graph",
         detail="Who an agent trusts, owes, resents and reports to — the substrate for "
                "delegation, betrayal and bribery, which are all one system.",
         acceptance="An agent's willingness to act against another is computed from the "
                    "relationship graph, not stored as a flag.",
         days=4, refs=["LW SS2.3"]),
    dict(title="Communication: agents telling each other things",
         detail="Conversation as belief transfer with latency, selection and distortion. "
                "Agents choose what to share based on who they are talking to.",
         acceptance="A fact crosses a social network with the decay, hop count and "
                    "distortion the belief model specifies.",
         days=4, refs=["SS4"]),
    dict(title="Deception: agents that lie",
         detail="An agent can assert something it does not believe, with a motive derived "
                "from its goals. The receiver has no way to know except through provenance "
                "and corroboration.",
         acceptance="An agent with a motive to deceive does, and the false belief propagates "
                    "and is later contradicted by a firsthand observation.",
         days=4, refs=["SS4"]),
    dict(title="Agent scale: level of detail for simulation",
         detail="Fifty thousand agents cannot all plan every tick. Full simulation near the "
                "player, statistical further out, with promotion and demotion that does not "
                "lose state or produce a visible discontinuity.",
         acceptance="An agent demoted to statistical and later promoted has a history "
                    "consistent with what it would have done, and the transition is "
                    "invisible.",
         days=6, refs=["SS14"]),
    dict(title="Agent debugging and inspection tools",
         detail="Pick any agent and see its needs, goals, plan, beliefs, relationships and "
                "the reasoning behind its current action. Without this the framework is "
                "unmaintainable.",
         acceptance="Any agent's current action is explained in one screen with no code "
                    "reading.",
         days=4, refs=["SS13"]),
    dict(title="Agent behaviour regression suite",
         detail="Scenarios asserting agents respond correctly to needs, beliefs, deception "
                "and failure, so a change to the planner is caught rather than discovered.",
         acceptance="Each property in this milestone has a scenario that fails when broken.",
         days=4, refs=["SS13"]),
    dict(title="Client integration: agents as embodied characters",
         detail="Agents drive the characters M09 built, through the entity-view framework. "
                "A sim agent walking to work is a person walking down a street.",
         acceptance="Two hundred simulated agents are visible as characters going about "
                    "traceable business, inside the frame budget.",
         days=5, refs=["ARCH Rule 1"]),
    dict(title="Playable proof: 500 agents for 30 days",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Any agent's whole history is explained by its needs and beliefs with no "
                    "appeal to world state it never observed, and two agents with different "
                    "beliefs act differently.",
         days=3, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M17 - organisation framework.
# ---------------------------------------------------------------------------

M17 = [
    dict(title="Organisation entity and charter",
         detail="Charter, members, holdings, fleet, treasury, doctrine. One type, used by "
                "player and NPC organisations alike — ARCH Rule 4 is enforced here or "
                "nowhere.",
         acceptance="A test asserts no branch in the org crate reads a player flag.",
         days=3, refs=["LW SS2", "ARCH Rule 4"]),
    dict(title="Membership, roles and authority",
         detail="Who belongs, in what role, with what authority to commit the organisation's "
                "resources.",
         acceptance="An action exceeding a member's authority is refused, and the refusal is "
                    "visible to the member who attempted it.",
         days=3, refs=["LW SS2.3"]),
    dict(title="Doctrine as goal weighting",
         detail="Doctrine weights goal generation so a mercantile organisation answers a "
                "supply problem with a contract and a predatory one with a raid. Flavour "
                "text that does not change behaviour is not doctrine.",
         acceptance="Two organisations with identical assets and different doctrine produce "
                    "measurably different action distributions.",
         days=4, refs=["LW SS2.1"]),
    dict(title="Treasury, budgets and financial state",
         detail="Income, expenditure, reserves and the ability to commit to something that "
                "has not been paid for yet.",
         acceptance="An organisation cannot spend what it does not have, and insolvency has "
                    "consequences that propagate to its members.",
         days=3, refs=["SS7"]),
    dict(title="Holdings and asset ownership",
         detail="Stations, claims, facilities, ships — what an organisation owns, what it "
                "earns from them, and what it is exposed to by them.",
         acceptance="Transferring a holding moves income and vulnerability together, with "
                    "the change propagating through the belief graph rather than instantly.",
         days=3, refs=["LW SS2.1"]),
    dict(title="Span of control and oversight",
         detail="Oversight demand from assets against supply from members, with the deficit "
                "expressed as drift rather than as a number.",
         acceptance="Oversight demand and supply are computable and their ratio predicts the "
                    "drift rate observed in a scenario.",
         days=4, refs=["LW SS2.3"]),
    dict(title="Drift: subordinates acting on their own goals",
         detail="The consequence of the deficit, expressed entirely through the agent "
                "framework. Unauthorised routes, skimming, late execution, private agendas — "
                "all agents pursuing their own goals because nobody is watching.",
         acceptance="Drift emerges from agent goals with no code in the org layer that "
                    "produces it directly.",
         days=5, refs=["LW SS2.3"]),
    dict(title="Founding an organisation",
         detail="Capital, a public charter, and registration with an existing authority — "
                "which means an authority now has an opinion about you.",
         acceptance="A player organisation is founded through the same API an NPC one is, "
                    "and the chartering authority's disposition changes as a result.",
         days=3, refs=["LW SS2.2"]),
    dict(title="Organisation-level goals and decision making",
         detail="Organisations want things: growth, security, revenue, revenge. Goals "
                "generated from state and doctrine, decomposed into directives for members.",
         acceptance="An organisation's current goals are inspectable and each traces to a "
                    "state fact and a doctrine weight.",
         days=5, refs=["LW SS2.1"]),
    dict(title="Hiring, firing and defection",
         detail="Members join and leave for reasons in their own goal systems. An "
                "underpaid, unwatched or resentful subordinate is a recruitable one.",
         acceptance="An agent defects to a rival when the rival's offer beats their current "
                    "position on their own terms, and the org learns of it late.",
         days=4, refs=["LW SS2.3"]),
    dict(title="Standing as belief, not as a number",
         detail="Organisations do not own reputations. Other organisations hold beliefs "
                "about them, and those beliefs disagree.",
         acceptance="A test asserts no reputation field exists, and two organisations "
                    "demonstrably hold different beliefs about a third.",
         days=3, refs=["LW SS2.4"]),
    dict(title="Organisation inspection tools",
         detail="Membership, holdings, finances, goals, drift and standing for any "
                "organisation, plus the reasoning behind its current decisions.",
         acceptance="Any organisation's current behaviour is explicable from one screen.",
         days=3, refs=["SS13"]),
    dict(title="Organisation regression suite",
         detail="Scenarios for drift, defection, insolvency, doctrine divergence and "
                "founding.",
         acceptance="Each property has a scenario that fails when broken.",
         days=3, refs=["SS13"]),
    dict(title="Playable proof: over-extend an organisation",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="Drift appears as subordinates pursuing their own goals, with no code "
                    "path in the org layer asking whether the owner is a player.",
         days=2, refs=["ARCH Rule 6"]),
]


# ---------------------------------------------------------------------------
# M18 - economy and logistics.
# ---------------------------------------------------------------------------

M18 = [
    dict(title="Goods, commodities and material properties",
         detail="What exists, what it weighs, what it takes up, what it is worth and what it "
                "is made from. The vocabulary production and construction are written in.",
         acceptance="The goods table is data, and adding a commodity requires no code.",
         days=2, refs=["SS7"]),
    dict(title="Production chains",
         detail="Facilities that consume inputs and produce outputs at a rate, with the "
                "labour and power they need. Ore becomes metal becomes components becomes "
                "ships.",
         acceptance="A chain three deep runs for a simulated year with conservation holding "
                    "at every stage.",
         days=4, refs=["SS7"]),
    dict(title="Local markets with real price formation",
         detail="Prices from local supply, local demand and what traders believe about "
                "elsewhere — not a global price table with noise.",
         acceptance="Two markets in one system hold different prices for the same good, and "
                    "the difference is explained by supply and by transport cost.",
         days=5, refs=["SS7"]),
    dict(title="Conservation invariants",
         detail="Goods and money are conserved except at explicit mint and sink events. The "
                "prototype proved this at small scale; it must hold at large scale.",
         acceptance="A simulated year across thirty-two systems conserves both to the unit.",
         days=3, refs=["SS7"]),
    dict(title="Contracts and obligations",
         detail="Agreements to deliver, to pay, to protect, with terms, deadlines and "
                "penalties, resting on the existing obligation ledger.",
         acceptance="A breached contract has consequences that propagate as belief and as "
                    "obligation, not as a number changing.",
         days=4, refs=["SS4", "SS7"]),
    dict(title="Transport and convoys",
         detail="Goods move physically, take time, and can be intercepted. This is what makes "
                "distance strategic and a convoy a target.",
         acceptance="A shipment's arrival time matches the route's travel time, and "
                    "destroying it removes the goods from the destination's supply.",
         days=4, refs=["LW SS7.4"]),
    dict(title="Supply shock propagation",
         detail="A disruption in one place moves through markets at the speed of transport "
                "and information, not instantly.",
         acceptance="A shock moves prices three systems away with a lag matching travel "
                    "time, and the lag is measurable.",
         days=3, refs=["SS7"]),
    dict(title="Labour markets",
         detail="Agents want work, organisations want workers, and wages come from that "
                "meeting. Underpaying is possible and has consequences.",
         acceptance="A labour shortage raises wages, and an organisation that will not pay "
                    "loses workers to one that will.",
         days=3, refs=["LW SS2.3"]),
    dict(title="Currency, credit and debt",
         detail="Money that is issued somewhere, credit that can be extended and defaulted "
                "on, and debt as a relationship rather than a number.",
         acceptance="A default damages the creditor's position and the debtor's standing, "
                    "both through existing systems.",
         days=3, refs=["SS7"]),
    dict(title="Economic inspection and dashboards",
         detail="Prices, flows, shortages, production and trade routes, inspectable across "
                "the whole simulation.",
         acceptance="A shortage anywhere is visible in the dashboard and traceable to its "
                    "cause.",
         days=3, refs=["SS12"]),
    dict(title="Economic balance and anti-degeneracy",
         detail="Long runs looking for the failure modes: hyperinflation, dead markets, "
                "monopoly, infinite loops. Each found and given a mechanism that resists it.",
         acceptance="Ten one-year runs from different seeds end with functioning economies "
                    "and no runaway.",
         days=5, refs=["SS12"]),
    dict(title="Economy regression suite",
         detail="Conservation, price formation, shock propagation and contract enforcement "
                "as scenarios.",
         acceptance="Each property has a scenario that fails when broken.",
         days=3, refs=["SS13"]),
    dict(title="Playable proof: the supply shock",
         detail="Rule 6. The gate, run and recorded.",
         acceptance="A shock moves prices three systems away with the right lag; ninety days "
                    "with no negative inventory and no money created outside a mint.",
         days=2, refs=["ARCH Rule 6"]),
]
