# Module map and layering rules

*How the code is arranged, and what is not allowed. Companion to
`docs/design/living-world.md`, which says what we are building and why.*

---

## 0. Why this document exists

The prototype works and the prototype's code does not scale. Three files carry
most of it:

| File | Lines | Doing |
|---|---|---|
| `src/world.rs` | 1430 | economy, obligations, investigations, missions, agents, the lot |
| `client/.../LedgerPlanet.cpp` | 1226 | quadtree, LOD, job pool, upload, cache, section pool, stats, console |
| `client/.../LedgerWorld.cpp` | 1002 | world building, site selection, camera scripting, capture, transects |

That is fine for a spike, whose job is to answer a question and be thrown away.
It is not fine for a system that has to grow for two years, and it is the reason
the plan below starts with architecture rather than features.

The failure mode is specific and worth naming: **a god-module has no seams, so
every new feature is added by editing the middle of it.** After a year that file
is the only thing that knows how anything works, and changing it is a research
project. Nothing about that is fixed by care or by comments.

---

## 1. The two halves

```
      ┌─────────────────────────────────────────┐
      │  SIMULATION  (Rust, deterministic)      │   authoritative
      │  fixed point, event sourced, headless   │   runs without a client
      └──────────────────┬──────────────────────┘
                         │  wire contract: commands up, snapshots down
      ┌──────────────────┴──────────────────────┐
      │  CLIENT  (UE 5.8, C++)                  │   a view and an input device
      │  render, embody, present                │   owns no gameplay state
      └─────────────────────────────────────────┘
```

**Rule 1. The simulation is authoritative; the client is a view.** No gameplay
state lives in Unreal. Not "mostly". A ship's position while you fly it is
client-side prediction of a sim entity, not a fact the client owns. The moment
the client owns a fact, that fact cannot be reasoned about, replayed, tested
headlessly, or shared with another player.

This rule is what makes everything in `living-world.md` affordable: the world
keeps running where the player is not, because the thing that runs it was never
Unreal.

---

## 2. Simulation crates

A cargo workspace. Each crate is a real compilation boundary, which means the
compiler enforces the layering rather than a convention doing it.

```
ledger-core        fixed-point arithmetic, PRNG, ids, tick/time. Zero domain knowledge.
ledger-event       event log, Reducer trait, snapshot, replay, determinism harness.
ledger-space       systems, bodies, orbits, sites, routes; distance and travel time.
ledger-belief      propositions, beliefs, provenance, propagation, decay, distortion.
ledger-agent       needs, goals, planning, scheduling, competence, loyalty.
ledger-org         membership, roles, holdings, treasury, doctrine, span of control.
ledger-econ        goods, markets, production, contracts, logistics.
ledger-site        environment profiles, claims, structures, utility networks, maintenance.
ledger-power       threat models, alarm, coalition formation, hostility, war state.
ledger-mission     tension detection, mission grammar, offers, stakes, resolution.
ledger-directive   standing orders, delegation, resolution against agents.
ledger-world       composition root. Owns State and reduce(). Wires the above.
ledger-wire        the client contract. Nothing depends on this but the client.
ledger-cli         the binary: dumps, replays, scenarios, the determinism harness.
```

### 2.1 The dependency rule

Dependencies flow **down only, never sideways**:

```
core
 └─ event
     ├─ space
     ├─ belief
     └─ agent          (may use space + belief)
         ├─ org        (may use agent)
         ├─ econ       (may use space + org)
         ├─ site       (may use space + econ + org)
         ├─ power      (may use org + belief)
         ├─ mission    (may use everything above it)
         └─ directive  (may use agent + org + site)
             └─ world  (may use all of them; nothing may use world)
                 ├─ wire
                 └─ cli
```

`power` may depend on `org`. `org` may **not** depend on `power`. When a rule
like that becomes inconvenient — and it will, the first time an organisation
needs to know whether it is at war — the answer is never to add the edge. The
answer is that the fact belongs one layer down (`org` gets a hostility field
that `power` writes) or one layer up (`world` composes the two).

Cargo will refuse to compile a cycle. That is the point of using crates rather
than modules: **the build system is the architecture police**, and it does not
get tired or make exceptions at 2am.

### 2.2 What stays true from the prototype

The existing sim got the hard parts right and they carry forward unchanged:

- Fixed-point `i64` arithmetic in all authoritative state. No floats, ever.
- Event sourcing: `State_n = fold(reduce, State_0, events[0..n])`.
- `SplitMix64` seeded from `(world_seed, region_id, tick)` — no shared RNG state.
- Typed enums, never magic strings. Exhaustive `match`.
- Disposition computed, never stored.

The work is not rewriting these. It is putting walls between the things that
currently share `world.rs`.

---

## 3. Client modules

Unreal modules, not folders. Same reason as crates: `.Build.cs` declares
dependencies and UBT enforces them.

```
LedgerCore        math, LWC helpers, the async-job pattern, logging. No gameplay.
LedgerTerrain     cube-sphere quadtree, patch generation, LOD, streaming, cache.
LedgerSky         atmosphere, clouds, star field, lighting, time of day.
LedgerMaterial    generated materials and textures.
LedgerProcGen     mesh building; settlement, building and prop generators.
LedgerSettlement  district views, utility state, construction sites, environment effects.
LedgerBridge      the only module that talks to the sim. Commands out, snapshots in.
LedgerEntity      entity-view framework: sim entity -> actor, spawn and despawn by relevance.
LedgerPawn        embodiment: on foot, in a seat, in a ship. Shared locomotion contract.
LedgerFlight      6-DOF flight model, gravity, atmospheric drag.
LedgerCommand     the Command lens: map, directives, reports.
LedgerUI          HUD, menus, organisation screens.
LedgerGame        composition root: game mode, subsystems, wiring. Deliberately thin.
```

**Rule 2. `LedgerGame` is the only module permitted to know about many others.**
Everything else declares a short, explicit dependency list. If `LedgerTerrain`
needs to know about ships, the design is wrong, not the rule.

**Rule 3. `LedgerBridge` is the only module that touches the wire format.** One
place to change when the contract changes; one place to test against a recorded
snapshot stream.

### 3.1 What happens to the prototype code

Split, then hardened. Specifically:

- `LedgerPlanet.cpp` → `LedgerTerrain`, as five files: quadtree, LOD policy,
  patch job, section pool, cache. The console commands and stats move to a
  diagnostics file.
- `LedgerSurface.cpp` → `LedgerMaterial`, one file per material.
- `LedgerWorld.cpp` → the scripted flight is a *test fixture*, not production
  code. It moves to a `LedgerHarness` module compiled only in development
  builds, and the world building it does moves to `LedgerGame`.
- `LedgerShip.cpp` → `LedgerFlight` (the model) plus `LedgerPawn` (the pawn).

None of it is thrown away. All of it is currently in the wrong place.

---

## 4. Rules that apply everywhere

**Rule 4. No `if (is_player)` below the UI layer.** A player organisation is an
organisation. A player agent is an agent. The only place the player is special
is where input arrives and where pixels leave. This is the rule that makes the
world alive rather than reactive, and it is the one that will be under the most
pressure — every shortcut in the next two years will present itself as a
harmless special case here.

**Rule 5. Determinism is a test, not an aspiration.** Every sim crate ships a
replay test: same seed and same events produce a byte-identical snapshot. The
harness runs in CI. A crate that cannot be replayed is a crate that cannot be
debugged, and there will be bugs that only appear four hours into a simulation.

**Rule 6. Framework milestones end with a thin playable proof.** Not just green
tests — something you can run and look at. The risk of spending a year on
frameworks is building a year of scaffolding that turns out not to fit the game,
and the only defence is exercising each framework through the actual game loop
as it lands, however crudely.

*This is worth stating plainly as a risk: a year of framework-first work can
produce beautiful abstractions that nobody can build a game out of. The
mitigation is Rule 6 and nothing else. If a framework milestone cannot produce a
playable proof, that is information about the framework, not about the schedule.*

**Rule 7. Content is data; mechanism is code.** A doctrine is data. The threat
model that reads it is code. A mission template is data. The grammar that
generates from tension is code. When something is unclear, the test is whether a
designer changing it should require a compiler.

**Rule 8. Every settlement mechanic has a first-person tell.** From
`living-world.md` §7.1, promoted here because it is an architectural constraint
and not a presentation one: it is what keeps the city model small enough to
build. A mechanic that cannot be perceived by someone standing in the street
does not get simulated.

**Rule 9. Every module owns its own tests.** No central test directory that
imports everything, because that is a god-module with a different extension.

---

## 5. Order of work

The split happens first and takes four weeks. Everything after it is built
inside the structure rather than refactored into one later.

1. **The split itself (M01).** Crates and Unreal modules created, code moved,
   nothing rewritten. The god-files go, the layering tests land, and from that
   point the build enforces the rules. This is the cheapest it will ever be.

2. **The technical model (M02-M14).** Terrain, planetary bodies, atmosphere and
   weather, ships as component graphs, flight, seamless travel, physics at
   scale, embodiment, interiors, rendering, the asset pipeline, performance, and
   a vertical slice that proves it. Roughly two and a half years.

   This comes before the simulation because it is the part that can fail
   outright. Nested reference frames (M08) are engine-level surgery that Chaos
   does not support; seamless travel across twelve orders of magnitude (M07) is
   a precision problem with several ways to lose. A simulation built first would
   be rebuilt against whatever those turned out to allow.

3. **The simulation (M15-M23).** The crates above are created *here*, as the
   real simulation is written — not as a refactor of the Phase 0 spike. That
   spike is frozen (see `src/README.md`): splitting fifteen flat files into
   thirteen crates so that they can later be replaced is work for nobody, and
   the design it implements has already changed underneath it. What carries
   forward from it is a short list of decisions, not a structure.

   Event core and determinism, then agents —
   the longest single milestone, because everything in `living-world.md` reduces
   to agents — then organisations, economy, sites, power, missions and the
   Command lens, in that order, because each is written in the vocabulary of the
   last.

4. **The game (M24).** Nothing to coalition, in one save, with no tutorial.

The whole plan is about four years for one engineer. That is what this scope
costs, and a schedule that flatters itself is worse than no schedule.
