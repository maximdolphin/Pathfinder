# The Living World

*Companion to `ledger-design.md`. That document describes a galaxy where
information is the scarce commodity. This one describes what the player does in
it: start with nothing, build an organisation, and discover that the galaxy has
opinions about people who get large.*

Status: design, v1. Supersedes the "bounty loop" framing as the game's spine —
that loop remains, as one of many tensions the mission generator can express.

---

## 1. The shape of the game

Three games, one simulation, no mode switch:

| Scale | You are | Reference |
|---|---|---|
| **Body** | one person: on foot, in a ship, in a room | Cyberpunk, Star Citizen |
| **Crew** | a handful of people who follow you | GTA |
| **Corporation** | an organisation with assets across systems | Hearts of Iron |

The progression is not a reward track. It is what happens when your problems
outgrow your body. At the start you have one problem and one pair of hands. By
the end you have four hundred problems, still one pair of hands, and the only
way through is other people — who have their own beliefs, their own interests,
and their own reasons to lie to you.

**The scarce resource is attention, not money.** Money is a means. Time and
presence are the constraint the whole design is built around, and every system
below exists to make delegation both necessary and dangerous.

### 1.1 What you actually do

- **Early**: you are nobody. Move cargo, carry messages, sell what you learn.
  Someone else's convoy, someone else's war. You have no assets to lose, which
  is the only freedom you will ever have.
- **Middle**: you have a crew, a ship or three, a warehouse. You take contracts
  and you also *issue* them. Rivals notice you. You start needing bodyguards
  because someone has decided you are worth killing.
- **Late**: you have holdings across systems, convoys you never see, subordinates
  you have to trust, and an intelligence apparatus. You issue orders and read
  reports. Some of the reports are wrong.
- **End**: you are large enough that other organisations coordinate against you.
  Not scripted — they each independently concluded you are the problem, and then
  discovered each other.

### 1.2 What we are not building

- Not a mission list with tiers. Missions are generated from real tension in the
  world (§5).
- Not a faction reputation bar. Standing is a consequence of belief, and belief
  is already a system (`ledger-design.md` §4).
- Not a mode toggle between "action game" and "strategy game". One world, two
  lenses (§4).
- Not an ending. There is a power ceiling (§3) and it is a plateau, not a wall.

---

## 2. Organisations

**The single most important rule in this document: a player organisation and an
NPC organisation are the same type.** Not "the same interface". The same struct,
the same reducer, the same code paths. Every mechanism below applies to both, in
both directions, without exception.

The moment there is an `if (is_player)` in the organisation layer, the world
stops being alive and becomes a set that reacts to the player. Every game that
promises a living world and fails to deliver one fails at exactly this line.

### 2.1 What an organisation is

```
Org {
    charter:     what it exists to do          -> shapes goal generation
    members:     who belongs, in what role     -> span of control
    holdings:    stations, claims, facilities  -> income and vulnerability
    fleet:       ships and crews               -> reach and threat
    treasury:    liquid capital                -> what it can act on now
    doctrine:    how it prefers to solve things -> coercion vs commerce vs law
    standing:    what others believe about it  -> NOT a number it owns (§2.4)
}
```

`doctrine` is the personality. It is not flavour text: it weights goal
generation, so a mercantile org answers a supply problem with a contract and a
predatory one answers it with a raid. Two orgs with identical assets and
different doctrine play differently, which is what stops the galaxy reading as
one AI wearing hats.

### 2.2 Founding one

Founding an organisation is not a menu unlock. It requires:

- **Capital** — enough to survive its first quarter without income.
- **A charter** — a declared purpose, which is public, and which constrains what
  it can plausibly do without cost. Charters can be violated; violation is
  information, and information propagates.
- **A registration** — with some existing authority, which means an existing
  authority now has a relationship with you and an opinion about you.

That third requirement is the important one. You cannot found an organisation
without entering somebody's world. There is no neutral ground, and the choice of
who charters you is your first real decision.

### 2.3 Span of control

An organisation's assets need oversight. Oversight comes from members with
authority, and authority is delegated from the top.

```
oversight_demand = sum over holdings and fleets of their complexity
oversight_supply = sum over members of (competence * authority * attention)
```

When demand exceeds supply, the deficit does not appear as a red number. It
appears as **drift**:

- Convoys take routes nobody authorised, because a captain had a better idea.
- Facilities under-report output, because somebody is skimming and nobody checks.
- Subordinates pursue their own goals with your assets.
- Orders are executed late, wrong, or not at all.

Drift is not a penalty applied to the player. It is subordinate agents (§6)
acting on their own goals because nobody is watching. It is the same mechanism
that makes NPC organisations rot from the inside, and the same one the player
can exploit against them: an over-extended rival is a rival with employees who
can be bought.

**This is the primary logistical ceiling on power.** You can always acquire more
than you can hold.

### 2.4 Standing is not owned

An organisation does not have a reputation value. Every *other* organisation has
a belief about it, and those beliefs disagree.

This is already the core system (`ledger-design.md` §4) and organisations plug
straight into it. `Proposition::OrgStrength(org, tier)`, `Proposition::OrgHostile(a, b)`,
`Proposition::HoldingOwned(holding, org)` — each held with confidence,
provenance and hop count, decaying, distorting, occasionally fabricated.

Consequences that fall out for free rather than being designed:

- You can be feared in one system and unknown in the next.
- You can *appear* weak by suppressing information, and appearing weak is
  materially different from being weak.
- A rival can fabricate your strength to trigger a coalition against you (§3),
  and you may not find out who did it.
- Your own subordinates' reports are beliefs with provenance. They can be wrong.
  They can be bought.

---

## 3. The ceiling: why you cannot simply win

Four independent ceilings. None of them is a cap, a soft cap, or a diminishing
returns curve. Each is a mechanism that makes the next increment of power cost
more than the last, for reasons the player can see and attack.

### 3.1 Coalition pressure — the social ceiling

Every organisation maintains a threat model over every organisation it has heard
of. Threat is computed from **belief, not truth**:

```
alarm(observer, subject) =
      believed_power(subject)          # from the belief graph, so wrong sometimes
    * proximity(observer, subject)     # threat is local before it is general
    * (1 - dependency(observer, subject))   # you do not attack your supplier
    * rivalry_history(observer, subject)
    / own_strength(observer)           # the weak are alarmed sooner
```

When several organisations independently cross an alarm threshold about the same
subject, and are not themselves blocked by mutual hostility, a **coalition**
forms. Not scripted: the pact is a discovered coincidence of alarm.

Coalitions are unstable by construction, because every member also runs the
threat model on every other member. A coalition dissolves when the target drops
below threshold — or when a member becomes more afraid of a partner.

**Counterplay, all four of which are real strategies:**

| Move | Mechanism |
|---|---|
| Hide | Suppress the information that feeds `believed_power`. |
| Bribe | Raise a member's alarm about somebody else. |
| Fracture | Feed one member a belief about another. |
| Be useful | Raise `dependency`. Nobody kills their only supplier. |

The fourth is the interesting one, because it is a genuine alternative victory
posture: an organisation everyone needs is safer than an organisation everyone
fears, and it is reached by entirely different play.

### 3.2 Visibility — the informational ceiling

You cannot hide a fleet. Assets generate observable events: traffic, purchases,
hiring, cargo, the simple fact of a station changing hands. The larger the
organisation, the more surface it presents, and the harder `believed_power` is
to suppress.

Small organisations can operate on deniability. Large ones cannot, and must
manage perception instead — which costs money and attention, and can be attacked.

### 3.3 Legitimacy — the economic ceiling

Coercion is cheaper than consent in the short run and more expensive in the
long. A holding taken by force needs a garrison forever. A holding taken by
contract needs an accountant. Doctrine is the choice between these and both are
viable, but the coercive path's cost is a *rate* and the commercial path's is a
*one-off*, so the two curves cross.

### 3.4 Span of control — the logistical ceiling

§2.3. Growth outruns oversight, and the deficit is paid in drift.

---

## 4. Presence and Command: the two lenses

**There is no RTS mode.** There is a simulation, and two ways of looking at it.

**Presence** is your body. Where you physically are. On foot, in a cockpit, in a
room, at a table. Everything at this scale is real-time and first-person, and
this is where the game's texture lives.

**Command** is a lens over what your organisation is doing: a map, orders,
reports, standing directives. Opening it does not pause the world and does not
move you. Your body is still standing wherever you left it — which matters,
because someone might be looking for it.

The transition the design brief asks for is not a transition. It is the Command
lens going from empty (you own nothing) to overwhelming (you own too much). The
game becomes an RTS the same way a person becomes a manager: gradually, and
without ever being issued a different body.

### 4.1 Directives

Everything the organisation does is a **directive**: a standing intent attached
to assets, resolved by the simulation whether or not you are watching.

```
Directive {
    intent:      move | hold | escort | raid | acquire | produce | scout | ...
    assets:      which of yours
    constraints: budget, deadline, rules of engagement
    delegate:    which member owns it
}
```

A directive is resolved by the assigned member's own agent logic (§6), acting on
that member's own beliefs, with their own competence and their own interests.
This is where §2.3's drift comes from and it is not a separate system.

### 4.2 The presence dividend

**Doing a thing yourself is meaningfully better than delegating it.** Not a
percentage bonus — a difference in kind:

- You act on what you can see, not on a report that is three hops and two days old.
- You can change your mind mid-action. A directive cannot.
- Nobody skims from a job you did yourself.
- Your presence is worth morale to the people beside you.

And you can only be in one place. That is the entire game: a constant, unwinnable
allocation of the one thing that cannot be bought.

### 4.3 What the RTS layer is not

It is not a second game with its own resources and its own units. Every unit in
the Command lens is an entity you could walk up to. Every number is a belief
somebody holds. If a convoy shows on the map, it is because someone reported it,
and the report may be stale — a Command lens that displays ground truth would
destroy the entire information design.

---

## 5. Missions as grammar

There is no mission list. There is a **generator** that reads the world for
tension and expresses it as something a person could be paid to do.

```
tension = (someone believes something) + (wants something) + (lacks the means)
```

The generator queries the running simulation:

- An org believes a route is threatened and lacks escorts → escort contract.
- An org wants a competitor's production data and lacks access → infiltration.
- An org believes a rival is weak and wants their holding → raid, needing crew.
- A coalition (§3.1) is forming and a member is wavering → someone will pay to
  keep them in, and someone else will pay to pull them out.

The last one is the point. **Missions arise from the strategic layer**, so the
strategic layer is legible from the ground even before the player has any part
in it. A courier run that pays double because a war is coming is a war being
visible to someone who does not know there is a war.

### 5.1 Why missions change as you rise

Not because they are gated. Because:

- **Different tensions are addressable by different capabilities.** You cannot
  take an invasion contract with one ship.
- **Different tensions are visible to different roles.** The mission generator
  offers what the offering party would plausibly offer *you*, based on what they
  believe about you. Nobody hires an unknown for a decapitation.
- **At the top, you generate the tension.** Your directives become other people's
  missions. Somewhere an NPC courier is taking a contract that exists because you
  moved a fleet, and that is the same generator running.

### 5.2 Stakes

Every generated mission carries a stake: what actually changes in the world if it
succeeds or fails. Nothing is generated that does not matter, because everything
is generated *from* something that already mattered. A mission whose stake is
"the player receives money" is a mission the generator should not have emitted.

---

## 6. Agents

The living world is not a schedule of animations. It is agents pursuing goals
under beliefs, and everything above is downstream of that.

Every NPC — a dock worker, a convoy captain, a lieutenant, a CEO — is the same
kind of thing at different scope:

```
Agent {
    needs:       what pressures them (income, safety, standing, loyalty, grudge)
    goals:       a prioritised plan over world state
    beliefs:     their own subgraph. Not the truth. Theirs.
    competence:  how well they execute
    loyalty:     to whom, and how contingent
}
```

**Agents act on their beliefs, not on world state.** A guard who believes the
route is safe takes the route. A lieutenant who believes you are finished starts
looking for a buyer. This is the single mechanism that makes information warfare
matter at every scale, from lying to one guard to fabricating a coalition.

### 6.1 Why this must be a framework first

Every system above is expressed in terms of agents:

- Drift (§2.3) is subordinates pursuing their own goals.
- Coalitions (§3.1) are agents at organisational scope doing threat assessment.
- Directives (§4.1) are resolved by agent planning.
- Missions (§5) are generated from agent goals that lack means.
- Defection, betrayal, bribery, loyalty are all one system: goals and beliefs.

Build the agent framework wrong and every one of these becomes a special case.
This is why the roadmap spends a year on frameworks before it spends a day on
content, and why the agent framework is the longest single milestone in it.

---

## 7. Settlements, construction and habitability

Organisations need something to own that is not a number, and agents need
somewhere to be. Settlements are both, and they are the physical substrate the
strategic layer fights over.

Nothing here is a new paradigm. **A city is holdings (§2) plus a utility graph
plus agents who maintain it (§6).** That it decomposes into systems already in
the plan is the reason it is affordable at all.

### 7.1 The rule that keeps this from becoming a spreadsheet

**Every city mechanic must have a first-person tell.** If a mechanic cannot be
perceived by a person standing in the street, it does not ship.

| Mechanic | Tell |
|---|---|
| Power deficit | Lights out in a district; shops dark; a generator running loud |
| Life-support failure | People wearing masks indoors; sealed doors; frost |
| Storm season | Shutters closing; streets emptying; grit in the air |
| Deferred maintenance | Rust, patched conduits, a crew arguing about parts |
| Contested claim | Two sets of markers; guards who watch each other |
| Growth | Scaffolding, a construction crew, a district that was empty last month |

This is not a presentation requirement bolted on afterwards. It is the design
constraint that keeps the city model small: a mechanic with no tell is a
mechanic nobody asked for.

### 7.2 Habitability drives building composition

Every site carries an environment profile:

```
Environment {
    atmosphere:  breathable | thin | none | toxic | corrosive
    pressure, temperature band
    weather:     calm | storms | dust | radiation bursts   (periodic, forecastable)
    gravity
    resources:   what is in the ground here
}
```

A building is an **archetype plus the modules its environment demands**:

| Environment fact | Demanded module | Cost |
|---|---|---|
| Atmosphere not breathable | Sealed envelope + airlock | Life-support load |
| Storms | Anchoring, shutters, buried conduit | Capital, maintenance rate |
| Temperature outside band | Thermal regulation | Power load |
| Radiation | Shielding | Capital, mass |
| Low gravity | Tethering, spin sections | Capital |

So a habitat on a calm temperate world is a building, and the same habitat on an
airless storm-scoured moon is a sealed, anchored, shielded, power-hungry version
of itself. **Same archetype, different composition** — variety is emergent from
environment rather than authored per planet, which is what makes many planets
affordable.

The consequence that matters strategically: hostile worlds are *expensive to
hold*. A holding whose power fails is a holding that dies, which makes utilities
a legitimate military target and makes remote holdings genuinely risky rather
than just far away.

### 7.3 Four networks, no more

A settlement is four graphs over the same set of buildings. Four, because that
is the most a person can hold in their head while walking around:

1. **Power** — generated, distributed, consumed. The one everything else needs.
2. **Life support** — atmosphere and pressure. Exists only where the environment
   demands it, which is why some cities are fragile and some are not.
3. **Water and coolant** — feeds industry and habitation.
4. **Transit** — moves goods and people between districts, and to the pads.

Each node has capacity, each edge a throughput. Deficits cascade in one
direction: lose power, lose life support, lose the district. That single
cascade is the whole of city crisis modelling and it is enough.

### 7.4 Construction is a directive

Building something is not a menu with a timer. It is a directive (§4.1) with
materials, labour and a site:

```
ConstructionProject {
    site:       a claim you hold
    design:     archetype + environment-demanded modules
    materials:  drawn from the economy, and they have to physically arrive
    labour:     agents with the build goal
    schedule:   real duration, disrupted by weather and by people
}
```

Materials come from `ledger-econ` and have to be *transported*, which means a
construction project on a remote world is a logistics problem and a convoy is a
target. Labour is agents, which means an unhappy workforce builds slowly. A
project can be sabotaged, starved, or simply forgotten by an over-extended owner.

### 7.5 Claims

Land is claimed, not bought from a menu:

- A **claim** is registered with an authority — the same act as chartering an
  organisation (§2.2), with the same consequence: somebody now has a
  relationship with you.
- Claims far from any authority are **cheap and unprotected**. This is the
  frontier, and it is a real strategic option: build where nobody can reach you,
  and accept that nobody will defend you either.
- Claims are **contestable**. Seizure is a directive. Ownership is a belief
  before it is a fact, so a disputed claim is disputed in the belief graph first.

### 7.6 Maintenance is the living part

Every structure has a condition that degrades, faster in hostile environments.
Degradation produces **work orders**, and work orders are exactly the tension the
mission generator reads (§5):

- An org's crews handle them, if the org has crews, and if the crews are paid,
  and if somebody is watching (§2.3).
- Unhandled work orders accumulate into failures, and failures cascade (§7.3).
- Unhandled work orders that an org cannot staff become **contracts on the open
  board** — which is where the player finds their first job, long before they
  understand there is a strategic layer generating it.

**NPC maintenance crews are agents, not scenery.** They have the maintain goal,
a competence, a loyalty, and beliefs about which conduit is broken. A crew that
believes the fault is in district four will go to district four. A rival who
wants your city dark does not need a bomb; they need your crews to believe the
wrong thing.

That is the whole design in one image: a city goes dark because somebody lied to
a repair crew, and a player standing in the street sees the lights go out.

## 8. Scale

The world has to be big enough for the strategic layer to have geography:

- **Multiple solar systems**, procedurally generated, deterministic from seed.
- **Planets** at real scale with continuous surface-to-orbit (already proven).
- **Settlements** from outposts to cities, generated, with interiors that matter
  because presence matters.
- **Travel** with real duration, because distance is what makes information slow
  and slow information is the entire design.

Travel time is not friction to be minimised. It is the mechanism that makes
belief decay, provenance, and hop count meaningful. A galaxy with instant travel
is a galaxy with instant information, and there is no game left.

---

## 9. Where this sits in the plan

**The technical model comes first.** Nothing in this document is built until the
game can be flown through: ships, flight, planets, travel, physics, bodies and
rendering, to a standard that holds up next to Star Citizen. That is milestones
M01 through M14 — roughly two and a half years — and it ends with an hour of
free play in a system that is not a prototype.

The reasoning is that the technical model is the part that can fail outright.
Seamless travel, nested physics frames and planet-scale streaming are each
capable of being impossible; the systems in this document are not. There is no
point simulating a galaxy nobody can fly through, and every one of these systems
would have to be rebuilt against whatever the technical model turned out to
allow.

Then the simulation, M15 through M23: core, agents, organisations, economy,
sites, power, missions, Command. Agents first, because everything here reduces
to agents. Then M24, which is this document made playable: arrive with nothing,
and leave with a coalition forming against you.

`docs/architecture/module-map.md` has the layering. `roadmap/` has the plan,
four hundred tasks of it, each with a finish condition that can be answered yes
or no.
