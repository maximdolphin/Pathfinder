# ADR-0004 — Frameworks before instances, inside every milestone

**Status:** Accepted
**Date:** 2026-09-08
**Related:** ADR-0003, `docs/architecture/module-map.md` Rule 6

## Context

ADR-0003 settles the order of the two halves. This settles what happens inside
each milestone: a biome *system* or one biome, a weather *model* or one storm, a
component graph or one ship.

The prototype answered this badly by default. It has a town because a town was
needed for a screenshot, so `LedgerSettlement` generates one settlement one way,
and every settlement in the game will be that settlement until it is rewritten.
The same is true of the terrain material, the ship, and the atmosphere: each is
one instance with no mechanism behind it.

## Decision

**Every milestone builds a general mechanism, and content only as far as is
needed to exercise it.** A biome is a data file the biome framework reads. A
ship is a data file the component framework reads. A storm is an output of the
weather model, not a thing that was placed.

Concretely, in each milestone's tasks: if a task would produce a thing, it
produces the rule that makes things, plus one thing.

## Why

The alternative is measurable in the prototype. Thirty-two buildings exist
because a loop places thirty-two boxes; there is no building framework, so the
first time a building needs to differ by environment — which `living-world.md`
§7.2 requires — that loop is rewritten and the thirty-two buildings are lost.
Multiply that by every system and the second year is spent replacing the first.

It is also what makes the scale affordable at all. Thirty-two star systems
cannot be authored. Neither can the buildings on them, the interiors inside
those, or the surfaces under all of it. A project with one engineer is only
allowed content that generates.

## What this costs

**A framework can be built for a year and turn out not to fit.** This is the
real risk and it is not small: general mechanisms designed without a concrete
consumer tend to be general in the wrong dimensions — flexible where nothing
varies, rigid where everything does.

**The only mitigation is Rule 6**: every milestone ends with a *playable proof*,
not a passing test. Something that runs, that a person looks at, exercising the
framework through the actual game loop however crudely. A framework that cannot
produce one is a framework that has gone wrong, and finding that out at the end
of a milestone is affordable in a way that finding it out at the end of a year
is not.

Where a framework and its first instance genuinely cannot be separated, build
the instance and write down that it is one — as this ADR does for the town. An
honest instance is recoverable; an instance that everybody has forgotten is a
framework is not.

## What was considered instead

**Instances first, generalise on the third.** The usual advice, and usually
right. Rejected here because the third instance mostly does not arrive in time:
the schedule visits each system once, and a system built as an instance stays
one until something forces the issue, by which point everything downstream
depends on its shape.
