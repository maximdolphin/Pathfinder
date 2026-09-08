# Design & Engineering Plan

> **Status:** Draft v1.1 — pre-POC
> **Changes in v1.1:** UE5 corrections (§6.8 Nanite/RVT, §6.9 bake timing, §9 stack, §13.2 Lumen); wire transport split from wire contract (§5, §9, §13.2); Phase 0 persistence trimmed to in-process (§9, §13.1); §8.2/§8.1 and §8.7 termination conflicts resolved; terrain spike carved out of the Phase 0 freeze (§13.1).
> **Codename:** LEDGER (placeholder; thematically apt — the game is about debts, obligations, and who knows what)
> **Audience:** Implementing engineers and agents. This document is normative. Deviations require an ADR.

---

## 0. How to read this document

Sections 1–3 are **design intent**. If an implementation detail conflicts with them, the design intent wins and the implementation is wrong.

Sections 4–9 are **system specifications**. They define contracts, not code. An implementer may choose different internals as long as the contracts and invariants hold.

Sections 10–14 are **engineering standards, structure, and schedule**. These are non-negotiable process constraints.

Section 15 lists **open decisions**. Do not silently resolve one. Raise it, decide it, write an ADR.

---

## 1. North Star

> **A living corporate-feudal galaxy where information is the scarcest commodity, and the only thing that makes you powerful is knowing something before anyone else does.**

Every system in this game exists to serve one sentence: **you learn something true about the world, and that knowledge is worth something.**

The test for any proposed feature: *does this create, transport, distort, protect, or monetise knowledge?* If no, it is probably not in this game.

### 1.1 What we are explicitly not building

We are not competing on ship fidelity, flight model realism, or graphical spectacle. Those markets are saturated and we would lose. Star Citizen has spent fourteen years and a billion dollars on exactly that axis and still has no release window. We compete on **simulation depth and consequence**.

Concretely, the following are **anti-goals**:

- Ships with fully modelled interiors as a headline feature
- 1:1 solar system scale
- Photoreal fidelity as a differentiator
- Hundreds of unique ship hulls
- Free-form conversational NPCs (see §6.4 for why)

### 1.2 The one-sentence pitch

*Dark Matter meets Star Wars, run as a market.* You are a contractor in a galaxy owned by corporations, and every job you take moves a number on someone's balance sheet.

---

## 2. Design Pillars

Four pillars. Each has a **falsifiable test** — a question you can answer yes or no by playing a build. If a pillar's test fails, the build has failed, regardless of what else works.

### P1 — The world does not wait for you

The simulation runs whether or not any player is online. Corporations trade, fail, consolidate, and retaliate on their own schedule.

> **Test:** Log out for a week. Log back in. Can you find at least three specific, causally-explicable changes to the world that had nothing to do with you?

### P2 — Consequence is legible in hindsight, uncertain in foresight

Nothing happens because a die rolled. Every major world change is the terminal step of a causal chain a sufficiently attentive player could have traced. Randomness seeds **timing and location**, never **outcome**.

> **Test:** Take any world-state change. Can a player reconstruct the four preceding causes from information that was available to them in-game before it happened?

### P3 — Knowledge is asymmetric, transportable, and perishable

There is no omniscient UI. What you know is a subset of what is true. What NPCs know is a different subset, arrives late, and is sometimes wrong. Information decays in value as it spreads.

> **Test:** Can two players in the same system hold contradictory, sincerely-held, non-buggy beliefs about the same fact?

### P4 — You are a participant, not a protagonist

No chosen one. No Force. You matter because of leverage you accumulated, and that leverage is losable.

> **Test:** Can a player permanently lose something they spent twenty hours building, through a chain of events that was their fault and was foreseeable?

### 2.1 The thing worth caring about

Corporate dystopia has a known failure mode: everyone is compromised, so nothing matters, so the player disengages. Our answer is **not** a moral cause. It is **crew**.

The player accumulates NPC crew and contacts who have their own obligations, debts, and fears. They are simulated entities with positions in the belief graph. They can be bought, frightened, killed, or turned. **They are the losable thing.** Player attachment comes from investment and jeopardy, not from writing.

This is a design constraint on every other system: crew must be persistently simulated, individually identifiable, and genuinely at risk.

---

## 3. Setting

**Premise.** Interstellar expansion was financed, not colonised. Every habitable world carries debt to the consortium that terraformed it. Sovereignty is a line item. "Government" is a service contract, renewable.

**Factions** are corporations, not nations. They hold:
- Territory (as collateral or operating concession)
- Lanes (routes, which are the real strategic asset)
- Ledgers (who owes them, what, and when it comes due)
- Reputation networks (who talks to them)

**The player** is an independent contractor — deliberately outside the ledger, which is the only source of freedom in the setting and also why they have no protection.

**Tone.** Grounded, procedural, bureaucratic menace. Violence is expensive and traceable. The scariest thing in the game is a compliance division, not a warship.

**Scale for MVP:** one star system, three to five planets/moons, four to six corporate factions, roughly 2,000 simulated named entities. This is deliberately small. Depth over breadth.

---

## 4. Core Loop

```
              ┌──────────────────────────────────────┐
              │                                      │
              ▼                                      │
    ACQUIRE KNOWLEDGE ──► CONVERT TO LEVERAGE ──► SPEND LEVERAGE
    (ask, bribe, tail,     (money, favours,        (access, protection,
     intercept, witness)    faction standing)       better knowledge)
              │                                      ▲
              │                                      │
              └──────── every action you take ───────┘
                        is itself an event others
                        can learn about
```

The closing edge is the design. **Acquiring information is not free and not silent.** Asking around about a target generates events; those events propagate; the target hears. The player is always racing their own information footprint.

### 4.1 Session shape

A session is 45–90 minutes and should contain: one contract accepted, three to six information-gathering interactions, one traversal, one resolution attempt, and at least one consequence that will outlive the session.

### 4.2 Worked example — the bounty (reference implementation of the loop)

This is the canonical mission archetype. Build this one first and build it properly; the others are variations on the same machinery.

1. **Contract board** surfaces a bounty. It exists because a corp holds an unresolved obligation against a person. It was not generated from a template.
2. **Target has a true location** (`Vec3` + region). The player does not have it. The player has a *search volume*, initially the whole region.
3. **N NPCs hold beliefs** about that location, at varying confidence, some stale, some wrong.
4. **Disclosure** is a function of `(disposition, perceived_risk, incentive_offered, relationship_to_target)`. Incentives include money, favour redemption, threat, and traded information.
5. Each disclosure **narrows or widens** the search volume. Contradictions widen it. This is a set operation, not a quest-marker update.
6. **Every interaction emits events.** The bartender you bribed reports to the target's associate. The target begins moving. Search volume degrades over time.
7. **Resolution** is a physical encounter. Alive is worth more than dead. Alive requires approach and preparation; dead is fast and loud and generates worse events.
8. **Delivery choice.** The posting corp is not the only buyer. A rival may pay more for the target alive because the target knows something. Delivering to the rival is a betrayal event with propagation consequences.

Nothing in that sequence is special-cased. Steps 2–6 are one algorithm (§6.3) reused by every investigation mission.

---

## 5. Architecture Overview

Three processes. The boundary between them is the most important architectural decision in the project.

```
┌─────────────────────────────────────────────────────────────┐
│  SIM CORE (Rust, headless, deterministic, authoritative)    │
│                                                             │
│  event log ──fold──► world state                            │
│  ├─ entities, factions, ledger                              │
│  ├─ belief graph + propagation                              │
│  ├─ economy + market clearing                               │
│  ├─ mission generation (queries over state)                 │
│  └─ simulation LOD / reification                            │
│                                                             │
│  NO rendering. NO engine dependency. Runs in CI.            │
└──────────────────────┬──────────────────────────────────────┘
                       │ protobuf (the only contract; transport per §9)
        ┌──────────────┴───────────────┐
        ▼                              ▼
┌──────────────────┐         ┌─────────────────────┐
│ UE5 CLIENT       │         │ NARRATION SERVICE   │
│ presentation,    │         │ local LLM, batched, │
│ traversal,       │         │ off critical path,  │
│ combat, input    │         │ renders state→prose │
└──────────────────┘         └─────────────────────┘
```

### 5.1 Why the sim is not in Unreal

Non-negotiable, and the single highest-value decision here:

- **It runs in CI.** A million-tick fuzz run against economy invariants, in a GitHub Action, with no engine, no GPU, no editor. This is what makes a living world testable instead of hopeful.
- **It survives engine churn.** UE major version upgrades cannot break the economy.
- **It forces a real contract.** Everything crossing the boundary is a protobuf message — typed, versioned, and structurally incapable of carrying a magic string.
- **It fails fast.** The POC (§13.1) proves or kills the concept with zero rendering work.

### 5.2 Why Rust for the sim

The sim's requirements map exactly onto what Rust enforces at compile time: exhaustive `match` over sum types means an unhandled state is a build failure, not a runtime edge case. Ownership makes the "state is a fold over an event log" model natural rather than aspirational. This is the "algorithms resilient by construction, not by patched edge cases" standard, enforced by the toolchain rather than by review.

**Determinism requirement:** the sim uses **no floating point in authoritative state**. Fixed-point (`i64`, scaled) for all economic and positional simulation values. Floats are permitted only in the client's presentation layer. This is what makes replay bisection work.

### 5.3 Event sourcing

World state is a fold over an append-only event log.

```
State_n = fold(reduce, State_0, events[0..n])
```

**Requirements:**
- Every state mutation is an event. There is no other write path.
- PRNG is seeded per region per tick, derived from `(world_seed, region_id, tick)`. Never `thread_rng`.
- Replay from any snapshot to any tick must be bit-identical. This is a test.
- Snapshots are an optimisation, never a source of truth.

**Why:** in a world that evolves for months, a bug surfaces a thousand ticks after its cause. You will never reproduce it from a save. With event sourcing you replay to tick 47,000, bisect, and find the exact event. Without it, you debug a living world by guessing — and that is how these projects die quietly.

---

## 6. System Specifications

### 6.1 Event / Belief / Knowledge

The foundation. Everything else reads from this.

**Three distinct concepts. Never collapse them.**

| Concept | Definition |
|---|---|
| `Fact` | Something true in the world. Objective. |
| `Belief` | An entity's held proposition about a fact, with confidence and provenance. May be false. |
| `Disposition` | Computed from beliefs. **Never stored.** |

That last line is the load-bearing one. There is no `reputation: f32` field anywhere in this codebase. Disposition is a pure function of an entity's belief set. This is what gives us, for free: locality (an act in a backwater stays there), witness elimination working, reputation preceding you along fast trade lanes, two factions holding opposite views of the same act, and rumour as a first-class object distinct from truth.

```rust
struct Belief {
    subject: EntityId,        // who/what this is about
    proposition: Proposition, // typed enum — never a string
    confidence: Confidence,   // fixed-point 0..1
    acquired_tick: Tick,
    provenance: Provenance,   // Witnessed | Told(EntityId) | Inferred | Fabricated(EntityId)
}
```

**Propagation** is a graph traversal over social edges. Each hop applies:
- **Latency** — proportional to social and physical distance
- **Decay** — confidence falls per hop and per elapsed tick
- **Distortion** — propositions can mutate on transmission, deterministically, seeded by `(source, target, tick)`

**Fabrication** is the same pipeline with `Provenance::Fabricated`. Injecting a false belief is mechanically identical to spreading a true one. This is the setting's thesis expressed as a code path, and it costs nothing extra to support.

**Invariants (assert in tests):**
- A belief's confidence never increases without a new corroborating event
- Propagation terminates (no infinite loops in a cyclic social graph)
- `Provenance::Witnessed` requires the witness to have been within perception range at the event tick

### 6.2 Obligation Ledger (favours)

A directed multigraph. **Not a stat.**

```rust
struct Obligation {
    debtor: EntityId,
    creditor: EntityId,
    class: ObligationClass,  // typed enum
    weight: Weight,
    expiry: Option<Tick>,
    public: bool,            // was it witnessed?
}
```

`ObligationClass` gates what a favour can buy. A dockworker yields a manifest; a port authority yields a clean landing; an executive yields a name unredacted. Faction standing gates *which classes* are available to you at all — this is how faction standing becomes meaningful without being a number on a bar.

**Rules:**
- Favours are **consumed** on redemption. There is no renewable relationship currency.
- Redemption is an **event** and propagates. Using Faction A's favour against Faction B is visible if witnessed.
- NPCs call in favours **on the player**. This is a primary mission source and it is not optional content.
- Public obligations are visible to anyone who holds the belief. Private ones are leverage.

### 6.3 Investigation (the hidden-variable extraction algorithm)

One algorithm, reused by every investigation-class mission. Build it once, correctly.

**Formally:** the player holds a search volume `V` over possible values of a hidden variable `x`. Each disclosure is an observation that constrains `V`. The player wins by reducing `|V|` below a resolution threshold before an adversarial process invalidates `x`.

```
V ← full domain
loop:
  select source S from available contacts
  cost ← disclosure_cost(S, incentive)
  belief ← S.belief_about(x)          // may be stale, may be wrong
  V ← V ∩ constraint(belief)          // contradictions widen V
  emit Event::InquiryMade(S, x)       // ← the closing edge of the core loop
  propagate(event)                     // target may learn and move
  if resolution(V) < threshold: break
```

**Why this generalises:** "find a person" is the same algorithm as "find who is skimming from a manifest", "find where a rival's shipment routes", "find who put the bounty on *you*". One implementation, many mission types. Do not write a second one.

**Mandatory properties:**
- Contradictory disclosures must widen `V`, never silently pick a winner
- No quest markers. `V` is rendered as a search region, not a pin
- Inquiry events must propagate before the next player action resolves
- Must be fully testable headless with a scripted agent

### 6.4 Narration (the LLM layer)

**The model renders. It never decides.** This is an architectural constraint, not a guideline.

The narration service does exactly two jobs:

1. **Select** an action from a typed, closed action space, given structured world state — via constrained decoding or grammar. Output is an enum variant, never free text that touches state.
2. **Render** structured state into prose — news articles, dialogue lines, corporate filings.

Everything that matters — ledgers, positions, beliefs, prices — lives in the deterministic sim. The sim must produce **identical state with narration entirely stubbed**. This is a CI test.

**Why so restrictive:** every LLM-NPC demo to date hits the same wall. The NPC will talk about anything forever and it means nothing, because talking without consequence is not gameplay. Players discover this in about ten minutes and stop talking. Constrained selection over a real simulation is the version that works.

**Hardware budget.** Local inference competes with the renderer for VRAM. On a 12 GB card the game wants 8–10, leaving 2–4 — a 3–8B model at 4-bit quantisation, sharing compute with a 60 fps render loop. Therefore:
- Inference is **off the render thread**, always
- Batched, never per-frame
- Used for **slow** decisions: faction moves on tick-of-minutes, mission text at board refresh, dialogue at conversation open
- Dialogue budget: 300 ms, with an **authored fallback line** on timeout
- Design the pacing so latency is invisible. Do not fight it.

### 6.5 Economy & Market

**Causality direction is fixed and non-negotiable:**

```
simulation event → supply/demand model → price movement → news article
```

**Not** `LLM generates news → price moves`. That inversion produces a random number generator wearing a language model as a hat: unlearnable, unpredictable, unrewarding. Players would correctly conclude the news feed is noise within hours, killing the best system in the game.

Run correctly, the news is a **lossy, biased, sometimes-wrong rendering of true underlying state** — which is exactly the setting. Corporate releases spin. Some outlets are captured. Articles lag price moves, so players who watched the convoy die traded before the news broke.

**Randomness policy.** RNG seeds *when* and *where*. Never *what happens next*.

> A shipment is late (random timing) → contract penalty triggers (deterministic) → subsidiary cash reserve goes negative (deterministic) → it sells an asset to a rival (deterministic, rule-selected) → the rival now controls a lane (deterministic).

The collapse was **causal**. A player reading the ledger could have seen it three steps out and shorted it. That is the difference between a world that feels alive and one that feels randomised: *alive means legible in hindsight, uncertain in foresight.* Pure RNG is uncertain in both directions and therefore inert.

**Conservation laws.** This is the failure mode nobody plans for. If corporations can be destroyed and lanes permanently lost, the world monotonically degrades, and a player joining in year two arrives in a husk. Persistent worlds with irreversible state drift toward one of two attractors: total collapse, or one dominant power and total ossification.

Therefore quantities are **conserved, not destroyed**:
- Market share **moves between** corporations; it never evaporates
- Bankruptcy **transfers assets to creditors**; it never deletes them
- Territory changes hands
- **Mean-reversion pressure:** the larger a faction grows, the higher its overhead, the more it is targeted, the more rivals coalesce against it

The state space becomes a cycle rather than a line, and can run for years without a reset.

**Testable invariants (fuzz these in CI over 10⁶ ticks):**
- Total shares outstanding per instrument is constant
- Total territory tiles is constant
- No faction exceeds 40% dominance for more than N consecutive ticks
- No entity holds negative inventory
- Money supply changes only through defined faucets and sinks, and they are logged

**Anti-bot friction.** A market with legible signals is a market a script trades better than a human. Friction must be cheap for a person and expensive for a bot: knowledge that only exists inside physical conversations, positions requiring presence to establish. Our information-first design helps here more than a conventional order book would — but solve it deliberately, do not discover it.

> **Prior art warning:** economies are where ambitious multiplayer games actually fail. EVE Online is the only one that really works, and CCP has employed professional economists to monitor it. Faucets outrunning sinks, cornering, and bot arbitrage do not yield to good code alone.

### 6.6 Mission Generation (query, not template)

**The failure mode to design against** has a name: Kate Compton's *10,000 bowls of oatmeal problem*. A generator can produce mathematically distinct outputs that are perceptually identical. Every bowl unique; every bowl oatmeal. This is why Star Citizen, Starfield, and Ubisoft procedural content feel samey despite enormous combinatorial variety.

**Missions are queries against existing simulation tension.** Not templates with blanks filled from a pool.

```sql
find (A, B) where
    A holds unresolved obligation against B
    and B has an exploitable vulnerability
    and player has viable access to at least one of them
    and the tension predates the player's awareness of it
```

The mission is "resolve this tension". The goal is generated from state that **already existed for its own reasons**. It feels authored because it is *consequent* — it references debts and grudges that predate the player.

**Direct consequence:** the simulation must generate tension continuously whether or not a player is nearby. That is the headless tick, and it is the expensive part. Budget for it.

**Mission archetypes for MVP** (all built on §6.3 plus one resolution verb):

| Archetype | Hidden variable | Resolution verb |
|---|---|---|
| Bounty | Target location | Capture / kill |
| Audit | Who is skimming | Expose / blackmail |
| Interception | Route + timing | Board / destroy / observe |
| Extraction | Holding location | Retrieve person or data |
| Fabrication | (none — injection) | Plant a false belief |
| Collection | Debtor whereabouts + assets | Coerce / negotiate |

Six archetypes × a live world = non-repeating missions. Six archetypes × a static world = six missions. The variety is in the simulation, never in the generator.

### 6.7 Simulation LOD & Reification

You cannot per-entity tick a galaxy.

- **Full fidelity:** regions with players present. Per-entity simulation.
- **Statistical:** distant regions. Aggregate flows, no individual agents.

**Reification is the hard part.** When a player arrives, approximate state must instantiate into concrete agents *consistently*. When they leave, concrete state must fold back into aggregates *without losing information*. Get the boundary wrong and players find the seam immediately.

Specify it as a **bidirectional, lossy-but-consistent transform** from day one. It is not retrofittable.

**Invariant:** `aggregate(reify(A)) == A` for all aggregate states `A`. This is a property test, and it is the single most important test in the LOD system.

### 6.8 Traversal (planet ↔ space, no loading screens)

The client-side technical centrepiece. Scoped deliberately small.

**What UE5 gives us free.** Large World Coordinates removes the float-precision problem CIG forked an engine to solve. A 6,000 km planet is 6×10⁸ cm — comfortably inside LWC's envelope with room for a moon and orbital stations. We only hit the wall at full 1:1 system scale, which is an explicit anti-goal (§1.1). Camera-relative rendering is on by default. Sky Atmosphere is a Bruneton-style physically-based model parameterised by planet radius and atmosphere height, explicitly built for ground *and* orbital viewing — the most visually convincing part of reentry is a component we configure, not build.

**Nanite is not on that list.** Nanite clusters are built offline. A runtime-generated, streamed cube-sphere quadtree cannot use it, which is exactly why the terrain below is specified as a conventional screen-space-error LOD mesh. Nanite earns its place on ship hulls, POI props, and station interiors — assets baked at cook time. Do not plan terrain work around it, and do not let §9's feature list imply otherwise.

**What must be built:** a cube-sphere quadtree terrain with continuous LOD.

- Six root faces, recursively subdivided against a screen-space error metric relative to camera altitude
- Heightfield from a GPU compute pass over layered noise, or sampled from baked source
- Crack prevention via **edge-index stitching**, not skirts — cleaner, no wasted fill rate
- Runtime Virtual Texture for surface material blending by slope and altitude. **This is a subsystem, not a checkbox:** an RVT volume is a box projecting along one axis, and a cube-sphere presents six faces with six normals. Expect six RVT volumes, explicit seam handling at face edges, and projection distortion that worsens with altitude. Budget it as work, not configuration.
- Collision cooked **only** within a few hundred metres of the player, async, off the game thread

**The schedule risk lives in that last bullet.** Chaos heightfield collision cooking is the hitch source in every implementation of this. It must be a job-queued, budgeted, **predictive** system — cook ahead along the velocity vector, never on demand. Correct architecture up front means the rest is bookkeeping. Wrong architecture means patching stutters forever.

World Partition does not map onto a sphere; bypass it for terrain and drive our own streaming. Retain it for discrete POIs (landing pads, station interiors).

> **Build/buy gate — resolve before Phase 1.** Voxel Plugin and several planetary-terrain plugins cover this ground, and if one handles async collision competently we skip the highest-risk six weeks entirely. **Evaluate before assuming from-scratch.** This decision is worth more than any architecture that follows it.
>
> **Frame it honestly.** These plugins do not slot a quadtree layer in underneath our architecture — Voxel Plugin is SDF/voxel meshing, a *different* terrain architecture that replaces the one specified above. Adopting one is lock-in, not a shortcut. That may well be the right trade; it is not a free one.
>
> **The spike answers exactly one question:** fly a continuous descent from orbit to ground and measure collision cook time under motion. Everything else — material blending, greebling, streaming polish — is recoverable later. Cook hitching under motion is not. If it hitches there, it hitches forever.
>
> Timebox one week. Deliverable: an ADR resolving §15.1, with the measured cook-time trace attached.

### 6.9 Ships & Assets (deliberately minimal)

Ships are **transport and capability**, not the product. Anti-goals apply (§1.1).

**Approach: parametric modular kit.** ~40 components with typed socket interfaces, assembled procedurally via UE5 Geometry Script (`AppendBox`, `ApplyMeshBoolean`, `ApplyMeshExtrude`, `ApplySolidify`, XAtlas auto-UV). Not 200 bespoke hulls.

**Generation happens at editor time, baked to `StaticMesh` assets. Never at runtime.** Geometry Script booleans are slow enough to be a visible hitch, and runtime-generated geometry gets no Nanite (§6.8). The parametric kit is a *tooling* win — regenerate a hull in the editor, commit the asset — not a runtime feature. `Tools/ShipGen/` is an editor utility.

**Where this is strong:** parametric hulls (change `hull_length` 24 m → 18 m, regenerate in seconds — iteration on proportion is *faster* than hand-modelling); recursive panel subdivision for greebling; interior/exterior volume constraint solving so walkable space provably fits inside the shell.

**Where it is weak, and must be planned around:** Geometry Script plus booleans produces triangle soup — fine for Nanite static hulls, unusable for deforming meshes, and not hand-editable afterward. Silhouette quality needs art direction. Materials need trim-sheet workflows plus a human pass.

**MVP target:** 3 flyable hulls, cockpit-only interiors. Walkable ship interiors are **post-MVP**. Local physics grids (walking inside a moving ship) are engine-level C++ surgery — Chaos is a single simulation space with no nested reference frames — and are **explicitly out of scope through MVP**.

### 6.10 Multiplayer & PvPvE

**This is the least-resolved system in the document.** Deep simulated NPCs and persistent multiplayer are in genuine tension, and the resolutions below are proposals requiring an ADR before Phase 2.

The tensions, stated plainly:

| Tension | Proposed resolution |
|---|---|
| Bounty target is a simulated individual; another player kills them first. Your mission evaporates. | **Target persistence classes.** `Unique` (world-significant, shared, races are intentional), `Regional` (shared within region, respawn-equivalent role fills), `Personal` (instanced to contract holder). Contract board declares the class up front. |
| If targets instance per player, the world is not shared and the premise collapses. | Only `Personal` instances. Cap the proportion of contracts at that tier. |
| Belief propagation is a griefing surface — coordinated rumour injection to destroy standing. | Rate-limit fabrication per source entity; fabrications carry `Provenance::Fabricated` and are traceable; corroboration from independent sources required above a confidence ceiling. |
| Headless simulation cost scales with world size, not player count — a server bill with no revenue relationship. | Simulation LOD (§6.7) is the mitigation. Budget it explicitly; measure cost per region-tick from POC onward. |

**Scale target for MVP:** 16–32 concurrent players per shard, one shard. Not 400. Server meshing is an anti-goal — CIG spent roughly eight years on it, it has been called technically impossible and unnecessary even if possible, and it buys seamlessness, which is an aesthetic property we are not selling.

**Netcode posture through MVP:** sim is authoritative for all world state. Combat is client-predicted with server validation. Standard UE replication for presentation entities; gRPC for sim state. Do not attempt custom netcode before MVP.

---

## 7. Legal Constraint — READ BEFORE DESIGNING NPC BEHAVIOUR

Warner Bros. holds **US20160279522A1**, "Nemesis characters, nemesis forts, social vendettas and followers in computer games," **expiring August 2036**. Any game implementing gameplay involving showdowns, factions, and bitter NPC feelings toward a player must differentiate enough to avoid infringement, license it, or gamble on WB Games' legal attention. The claims cover shared power centres, character hierarchies, NPCs, and related quests, with **quest outcomes reflected across separate game instances over a network** — which reads onto persistent multiplayer worlds specifically.

**Our differentiator is structural, not cosmetic:** LEDGER is built on **information propagation**, not personal vendetta and rank promotion. NPCs do not develop grudges and climb a hierarchy to hunt you. They hold beliefs, which propagate through a social graph, and act on economic interest. This is a genuinely different mechanic and also the better one.

**Action required:** IP counsel reads the actual claims against §6.1 and §6.10 **before Phase 2**. Do not defer this until the systems are load-bearing.

**Do not implement:** NPC rank-promotion-on-defeating-player, persistent named nemeses that hunt across sessions, or faction-fort capture hierarchies.

---

## 8. Engineering Standards (non-negotiable)

These are process constraints. They apply to every commit, human or agent.

### 8.1 TDD, strictly

For any defect: **write the failing test first.** The test must be a faithful, maximally strict reproduction of the failing scenario — never a test shaped to pass. Only then write the fix. The red→green transition is the proof the fix works.

For new systems: write the invariant tests before the implementation.

### 8.2 No magic strings, ever

All identifiers, states, classes, and categories are **typed enums** (Rust) or `UENUM`/`FGameplayTag` (UE5). Protobuf enums across the wire boundary.

Encountering a magic string in a file you are working in obligates you to refactor it to a typed enum — but **not necessarily in the same commit.** §8.1 requires small red→green diffs, and an unbounded refactor duty inside a bugfix defeats that. The rule: fold it into the same commit when the change is mechanical and local; otherwise land the fix, then land the refactor as the immediately following commit. What is *not* permitted is leaving it and moving on.

### 8.3 SOLID, without exception

Every module. Interface segregation matters most here — the sim's subsystems (belief, economy, mission) must depend on narrow traits, not on each other's concrete types.

### 8.4 Idempotent, edge-case-resilient algorithms

**Do not patch individual edge cases.** If you find yourself adding a conditional for a specific input, the algorithm is wrong. Redesign it so the edge case is handled by the structure of the solution.

The test: could you explain the algorithm's correctness without enumerating special cases? If not, it is not done.

Rust's exhaustive `match` over sum types is the enforcement mechanism — an unhandled state should be a compile error, not a runtime surprise.

### 8.5 Architecture documentation

Any infrastructure or architectural change requires either an update to an existing architecture doc, a new architecture doc, or an ADR — whichever fits. ADRs live in `docs/adr/` in this repo, numbered sequentially, using the standard Context / Decision / Consequences format. Mirror the publications convention used elsewhere.

**Every item in §15 requires an ADR when resolved.**

### 8.6 Folder structure discipline

If you find yourself navigating between distant folders to follow a single pipeline, **refactor the structure as you go**, with tests. Structure is optimised for a human finding things, not for the build system.

### 8.7 Review loop

On completing any work item: spin up **fresh** review agents (fresh context each iteration, not the same agent re-prompted), have them review, and fix what they find. Reviewers check against §8.1–8.6 and the invariants in §6.

**Cap at two rounds.** A fresh reviewer will always find *something*, so "repeat until a fresh agent reports no issues" is a loop with no termination guarantee and an unbounded token bill. Round one catches real defects; round two catches what the fixes broke. Anything surviving round two is either a judgement call — record it and move on — or a design problem that a review loop was never going to solve.

---

## 9. Tech Stack

| Layer | Choice | Rationale |
|---|---|---|
| Sim core | **Rust** (stable, `no_std`-friendly where possible) | Exhaustive matching, determinism, CI-testable, no engine coupling |
| Sim numerics | **Fixed-point `i64`** | Bit-identical replay. No floats in authoritative state. |
| Event store | **Phase 0: in-process `Vec<Event>` + serde to file.** Phase 1+: **PostgreSQL**, partitioned append-only tables | 200 entities × 50k ticks is tens of MB — it fits in RAM. Postgres buys durability and queryability, neither of which the Phase 0 gate needs. Boring and durable once it does. |
| Hot state / pubsub | **Phase 2+: Redis** | Region tick coordination and presence are multi-process concerns. A single-process sim needs neither. |
| Wire **contract** | **protobuf** | Typed, versioned, structurally magic-string-proof. This is the non-negotiable half. |
| Wire **transport** | **Phase 1: protobuf over TCP.** Phase 2+: **gRPC** | A separate decision from the contract. See the integration-cost note below. |
| Client | **UE5** — pin one version at Phase 1 start; do not chase latest | LWC, Sky Atmosphere, Nanite (**assets only — not terrain, see §6.8**), Lumen (**off for the vertical slice, see §13.2**). Engine upgrades are scheduled work, never incidental. |
| Client gameplay code | **C++** primary, Blueprint for designer-tunable params only | Blueprint is not a place for logic |
| Terrain | **Evaluate plugin vs custom — see §6.8 gate** | Highest-risk client decision |
| Narration | **llama.cpp / Ollama**, 3–8B at Q4, constrained decoding via GBNF | Local, batched, off critical path |
| Sim tests | `proptest` (property tests), `cargo-fuzz` | Invariant fuzzing over 10⁶ ticks in CI |
| Client tests | UE Automation Testing + Gauntlet | Traversal and perf regression |
| CI | GitHub Actions | Sim suite runs on every commit; UE builds nightly |
| Observability | **Phase 0: metrics printed into the text dump.** Phase 1+: OpenTelemetry → Grafana | The §12 metrics must be *measured* from Phase 0. They do not need a dashboard to be measured. |

**gRPC-in-UE5 integration cost — budget this explicitly.** UE builds with exceptions and RTTI disabled, uses its own build system, and its `check`/`verify` macros collide with protobuf and abseil headers. Wiring grpc + absl + protobuf as a UE third-party module is a known multi-day slog with poor documentation, and earlier drafts of this plan budgeted nothing for it. Phase 1 needs a live contract board, not backpressure and bidirectional streaming — protobuf framed over a TCP socket gets there in an afternoon and keeps the wire *contract* byte-identical. Adopt gRPC in Phase 2 when multiplayer actually needs streaming, as its own scheduled work item with an ADR.

**Rejected alternatives** (record as ADR-0001):
- *Sim inside UE (C++)*: cannot run in CI without an engine; couples the crown jewel to engine churn. Rejected.
- *Sim in C#*: viable, but float determinism requires constant vigilance rather than compile-time enforcement. Rejected.
- *Server meshing / 400-player scale*: anti-goal. Rejected.

---

## 10. Repository Structure

Monorepo. Optimised for following one pipeline without cross-repo navigation.

```
ledger/
├── docs/
│   ├── adr/                      # numbered ADRs — every §15 decision lands here
│   ├── architecture/             # living architecture docs
│   └── design/                   # this document, pillar tests, tuning notes
│
├── sim/                          # Rust workspace — the authoritative simulation
│   ├── ledger-core/              # event log, fold, tick loop, PRNG, fixed-point
│   ├── ledger-entity/            # entities, factions, crew
│   ├── ledger-belief/            # §6.1 — beliefs, propagation, distortion
│   ├── ledger-obligation/        # §6.2 — the favour ledger
│   ├── ledger-economy/           # §6.5 — supply, demand, clearing, conservation
│   ├── ledger-mission/           # §6.3 §6.6 — investigation algorithm, generation queries
│   ├── ledger-lod/               # §6.7 — aggregation and reification
│   ├── ledger-proto/             # generated protobuf types (single source of truth)
│   ├── ledger-server/            # gRPC service, persistence, region scheduling
│   └── ledger-harness/           # headless driver, scripted agents, text world-dump
│
├── narration/                    # Python — LLM service
│   ├── grammars/                 # GBNF constrained-decoding grammars
│   ├── renderers/                # state → news / dialogue / filings
│   └── fallbacks/                # authored lines used on timeout
│
├── client/                       # UE5 project
│   ├── Source/
│   │   ├── LedgerCore/           # sim client, protobuf transport, state cache
│   │   ├── LedgerTraversal/      # §6.8 terrain, atmosphere, flight, transitions
│   │   ├── LedgerPresentation/   # UI, contract board, market screens, dialogue
│   │   └── LedgerCombat/         # encounters, capture, prediction
│   ├── Content/
│   └── Tools/
│       └── ShipGen/              # §6.9 Geometry Script parametric kit
│
├── proto/                        # .proto definitions — THE contract
├── tools/                        # world inspector, replay bisector, market dashboards
└── ops/                          # deployment, migrations, observability config
```

**Rules:**
- `proto/` is the only place the sim/client contract is defined. Both sides generate from it.
- `sim/` has **zero** dependency on anything under `client/`. Enforced in CI.
- One pipeline, one folder subtree. If tracing the bounty flow requires four distant directories, restructure.

---

## 11. Testing Strategy

The reason the sim is a separate headless Rust service is that it makes the following possible. Do not compromise it.

**Tier 1 — unit.** Per-module, TDD, fast. Every defect gets a red test first (§8.1).

**Tier 2 — invariants (property tests).** Every invariant listed in §6 becomes a `proptest`. Examples:
- `aggregate(reify(A)) == A` for arbitrary aggregate state
- Total shares outstanding constant across arbitrary event sequences
- Belief confidence monotonically non-increasing absent corroboration
- Propagation terminates on arbitrary cyclic social graphs

**Tier 3 — long-horizon fuzz.** Run the sim 10⁶ ticks with a scripted adversarial agent, asserting all invariants continuously. Runs nightly in CI. **This is the single most valuable test in the project** and the thing that separates a living world that ships from one that quietly breaks.

**Tier 4 — replay determinism.** Replay any log from any snapshot; assert bit-identical state. Guards the fixed-point discipline.

**Tier 5 — narration stub equivalence.** Full sim run with narration stubbed must produce byte-identical world state to one with narration live. Guards §6.4's central constraint.

**Tier 6 — client.** UE Automation for traversal correctness; Gauntlet for frame-time regression on the terrain system.

**Tooling:** build the **replay bisector** in Phase 0, not later. When a bug surfaces at tick 47,000 you need to bisect the log immediately, and retrofitting the tool costs more than building it early.

---

## 12. Metrics That Matter

Instrument from POC. These tell you whether the design is working, and none of them are frame rate.

| Metric | Target | Why |
|---|---|---|
| Per-region tick cost | < 5 ms full fidelity | Determines world size ceiling and server bill |
| Belief propagation depth (median) | 3–6 hops | Too low = no spread; too high = omniscience |
| % of beliefs held that are false | 10–25% | Below 10% the asymmetry pillar is dead |
| Mission archetype distribution | No archetype > 35% | Oatmeal detector |
| Contracts referencing pre-existing tension | > 90% | Proves §6.6 is working, not templating |
| Favour redemption rate | 40–70% of accrued | Too low = they feel worthless; too high = trivially farmed |
| Faction share Gini coefficient over time | Bounded, oscillating | Proves conservation laws hold; a rising trend means ossification |

---

## 13. Timeline

Assumes one senior engineer driving agents, part-time-to-full-time. **Multiply by team size with the usual skepticism.**

### 13.1 Phase 0 — POC: "Is the world interesting in a text file?" (8–10 weeks)

**Zero rendering. Zero Unreal. No art.**

Build `sim/` core: event log, fold, deterministic tick, fixed-point. Belief graph with propagation (§6.1). Obligation ledger (§6.2). Investigation algorithm (§6.3). A minimal economy with conservation laws (§6.5). One faction set, ~200 entities, one region.

Then `ledger-harness/`: a scripted agent that pokes the world, and a **text world-dump** — a readable log of what happened and why.

Also build in Phase 0: the replay bisector, Tier 2–4 tests, and the §12 metrics **printed into the text dump** — not a dashboard (§9).

**Phase 0 runs as a single process with no external dependencies.** No Postgres, no Redis, no gRPC, no narration service, no OpenTelemetry. The event log is a `Vec<Event>` serialised to a file. The crate layout in §10 is the Phase 1 target; Phase 0 may start as one crate and split when a module genuinely needs its own dependency set. Every piece of infrastructure deferred here is infrastructure for a world that has not yet proved it is worth persisting — and if the gate fails, all of it was waste.

> **GO/NO-GO GATE.** Run the sim 50,000 ticks with a scripted agent. Read the text dump.
>
> **Ship criterion: is this a story you would want to read?** Can you trace a corporate collapse back four causes? Do two NPCs disagree about the same fact for legible reasons? Did anything surprise you?
>
> **If the text dump is boring, the game is boring.** No amount of Unreal fixes that. Kill or redesign here — three weeks of loss instead of a year.

This gate is the highest-value item in the entire plan. Do not skip it, do not soften it, and do not start building the game in Unreal before passing it.

**One carve-out: the §6.8 terrain spike may run in parallel.** It is the project's largest schedule risk, it resolves §15.1, and it is entirely independent of whether the sim is interesting. Answering a one-week build/buy question is not building the game — but it is the *only* Unreal work permitted before the gate, and if the gate fails its result is discarded along with everything else.

### 13.2 Phase 1 — Vertical Slice (4–6 months after Phase 0)

Resolve the §6.8 terrain build/buy gate **first**. Then:

- Cube-sphere terrain, one planet, surface→orbit→surface with no loading screen, 60 fps on 4070-class hardware — **with Lumen off.** A 4070 is 12 GB; §6.4 already spends 2–4 GB of it on the narration model, and Lumen plus a custom terrain in the remainder leaves no headroom. This is a traversal demo, not a lighting demo. Revisit Lumen once the narration hosting question (§15.6) is settled.
- 6-DOF flight, one flyable hull
- Sim↔client bridge over **protobuf-framed TCP** (§9); contract board reading live sim state
- **The bounty loop end to end** (§4.2): accept, investigate via NPC disclosure, narrow search volume, physically locate, resolve, choose delivery
- Basic dialogue with narration service and authored fallbacks
- Single player against the live sim. No multiplayer yet.

> **Gate:** does the bounty feel like investigation rather than a quest marker? Can a player describe how they found the target?

### 13.3 Phase 2 — MVP (8–12 months after Phase 1)

- 16–32 player shard, sim-authoritative
- All six mission archetypes (§6.6)
- Market and news feed live (§6.5), with the causality direction correct
- NPC crew: hire, lose, and grieve
- 3 hulls, 3–5 planets/moons, 4–6 factions, ~2,000 entities
- Simulation LOD and reification in production (§6.7)
- PvPvE persistence classes resolved and shipped (§6.10)
- IP counsel review complete (§7)

> **Gate:** P1–P4 pillar tests (§2) all pass on a live build with real players.

### 13.4 Phase 3 — Full Product (3–5 years, requires a team)

Additional systems, content volume, live ops, economist-level economy monitoring, walkable ship interiors, local physics grids. **Not achievable solo.** Phase 2 exists to be the thing you raise money or recruit on.

### 13.5 Honest schedule assessment

Phases 0 and 1 are realistically achievable by one strong engineer with agent leverage. Phase 2 is at the edge. Phase 3 is not — not because of the code, but because content volume, live ops, and organisational scale are where this class of project actually fails. That is the lesson of the billion dollars and fourteen years next door.

---

## 14. Risk Register

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | Sim is boring in text; core premise fails | **Critical** | Phase 0 gate exists precisely for this. Kill early. |
| R2 | Oatmeal problem — procedural missions feel identical | **Critical** | §6.6 query-not-template; §12 archetype distribution metric |
| R3 | Terrain collision cooking hitches | High | Predictive async cooking architected up front; plugin evaluation gate |
| R4 | Economy exploited or collapses | High | Conservation invariants fuzzed in CI; anti-bot friction designed, not discovered |
| R5 | Nemesis patent exposure | High | Structural differentiation (information vs vendetta); counsel before Phase 2 |
| R6 | LLM latency/VRAM ruins pacing | Medium | Off critical path, batched, authored fallbacks, slow-decision-only |
| R7 | Simulation server cost scales with world, not revenue | Medium | LOD from Phase 0; per-region-tick cost measured continuously |
| R8 | Scope creep toward ship fidelity | Medium | §1.1 anti-goals are binding; reject on sight |
| R9 | Belief propagation griefable in multiplayer | Medium | Rate limits, traceable fabrication provenance, corroboration ceilings |
| R10 | Solo-dev capacity exhausted before Phase 2 | High | Phases 0–1 designed to be independently demonstrable and fundable |

---

## 15. Open Decisions (each requires an ADR before resolution)

1. **Terrain: plugin vs custom.** Blocks Phase 1. Highest-value unresolved decision. (§6.8)
2. **PvPvE target persistence classes.** Proposal in §6.10 is unvalidated. Blocks Phase 2.
3. **Reification determinism under concurrent player arrival.** Two players entering a statistical region simultaneously — who triggers the transform, and is it deterministic? (§6.7)
4. **Crew death permanence.** Pillar P4 demands real loss. How permanent, and is there a mitigation the player can buy? Affects retention directly.
5. **Market instrument design.** Order book vs continuous double auction vs corp-share-only. Determines the anti-bot surface. (§6.5)
6. **Narration model size and hosting.** Local-only, or optional cloud for players without VRAM? Affects §6.4 latency budget.
7. **Shard topology at MVP.** One shard, or several with separate world state? Affects whether "the world lives without you" is one world or many.
8. **Fabrication cost model.** What stops rumour injection from being the dominant strategy? (§6.1, §6.10)

---

## 16. First Week

For the agent picking this up:

1. Read §1–2 and §8. Internalise the north star and the standards.
2. Scaffold `sim/` as a Rust workspace with the crate layout in §10. CI green on an empty test suite.
3. Write ADR-0001 recording the stack decision in §9 and its rejected alternatives.
4. TDD `ledger-core`: event log, deterministic fold, seeded PRNG, fixed-point arithmetic. Tier 4 replay-determinism test **before** any domain logic.
5. TDD `ledger-belief`: the `Belief` struct, propagation with latency/decay/distortion, and every invariant in §6.1 as a property test.
6. Build the text world-dump early. You need to be able to *read* the world from week two, not week ten.

Do not touch Unreal until the Phase 0 gate passes.
