# ADR-0003 — The technical model before the simulation

**Status:** Accepted
**Date:** 2026-09-08
**Related:** `docs/design/living-world.md`, `docs/architecture/module-map.md` §5

## Context

The prototype proved two things: a deterministic belief simulation can be made
legible, and a real-scale planet can be flown to and from without a loading
screen. `living-world.md` then set out a much larger game — player
organisations, a power ceiling made of coalitions, missions generated from
tension, settlements that have to be maintained.

That leaves an ordering question that is not obvious. The simulation is the
game's identity; the technical model is what carries it. Which gets built first?

## Decision

**The technical model, in full, before any of the simulation.** Milestones M02
through M14: terrain to production standard, planetary bodies and orbital
mechanics, atmosphere and weather, ships as component graphs, flight, seamless
travel, physics at scale, embodiment, interiors, rendering, the asset pipeline,
performance, and a vertical slice that is not a prototype. Roughly two and a
half years. Only then M15 onward.

## Why

**The technical model is the part that can fail outright.** Nested reference
frames — walking around inside a ship that is itself moving at speed near a
rotating planet — are engine-level surgery that Chaos does not support, and
design §6.9 already flags them as the reason the flight model integrates itself
rather than using the engine's. Seamless travel across twelve orders of
magnitude is a precision problem with several ways to lose. Either could turn
out to be unaffordable for one engineer.

Nothing in `living-world.md` is at that kind of risk. Agents, organisations,
threat models and mission grammars are hard to get *right*, but there is no
version of them that cannot be built.

**And a simulation built first would be rebuilt.** Every system in the living
world is expressed in terms the technical model defines: what a site is, how
long travel takes, what a ship can carry, where a person can stand. Building
those against assumptions and then discovering what the engine actually permits
means writing them twice.

The reverse ordering costs much less. The technical model needs to know almost
nothing about the simulation — it needs a planet, ships, bodies, and somewhere
to put a settlement, all of which are already specified.

## What this costs

**Two and a half years before the game is playable as a game.** The vertical
slice at M14 is an hour of free play in a system that looks and behaves right
and contains nobody with a motive. That is a long time to work without the thing
that makes the project worth doing, and it is a real risk to morale and to
judgement — it is easy to over-build a technical model when nothing is pulling
against it.

**Some technical work will turn out to be wrong** once the simulation arrives
with its actual requirements. The mitigation is that M14 ends with a review and
an ADR recording what the model proved and what it forces on the simulation
plan, rather than assuming the answer.

## What was considered instead

**Simulation first, technical model after.** Rejected: it means simulating a
galaxy that may turn out not to be flyable, and rebuilding it against whatever
the engine permits.

**Interleaved, a slice of each per milestone.** Rejected, though it is the
closest call. It would keep a playable game in view throughout, which is worth
a great deal. But the technical work has long dependency chains — physics
frames need flight, which needs ships, which need components — and slicing
across them means repeatedly returning to half-built systems. It also splits
attention on the milestones most likely to fail, which is exactly where it
should be concentrated.

**Buy the technical model.** Not seriously available. There is no off-the-shelf
solution for planet-scale seamless travel with nested physics frames; the
existing plugins solve terrain, which is the part already built.
