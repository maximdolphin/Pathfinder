# The game has no name

## Three names now, none of them the game's

- **`Pathfinder`** is the *repository*, chosen 2026-09-08 when the project went
  public. A repository needs a name and the game does not have one; picking one
  that sounds like a title would have quietly decided the question.
- **`Ledger`** is the *codename*, and it is all over the source: `LedgerCore`,
  `LedgerTerrain`, `ALedgerPlanet`, `Ledger.uproject`.
- The **game's name** is undecided, and nothing in the code should assume one.

The rename cost estimated below is unchanged by the repository name. Renaming a
GitHub repository is a redirect and five minutes; renaming the code is the part
that gets more expensive every week.

**Undecided as of 2026-09-08.** Nothing in this repository is a naming decision.

## "Ledger" is a codename

It appears as a prefix on every module, class and file: `LedgerCore`,
`ALedgerPlanet`, `LedgerFlightModel.h`. It is also the repository directory and
the Unreal project name.

That is a **codename**, not a title. Codenames outliving the working title is
the normal case rather than an accident — the engine behind Halo is still called
Blam, and Destiny's codebase is still full of Tiger. A prefix is an identifier
the compiler cares about and nobody else sees.

## What this means in practice

**Do not put the codename anywhere it reads as the game's name.** Not in
document titles, not in the roadmap's heading, not in anything that would be
shown to a person who is not editing the code. Those places say UNTITLED, and
the roadmap subtitle says so explicitly.

**Do keep using it as the code prefix**, for now. It is consistent, it is
unambiguous, and changing it is a mechanical rename that gets slightly more
expensive every week but never becomes hard: six modules, about fifty files, and
every class. Perhaps two hours today, most of a day by M05.

## When to decide

The cheapest moment to rename the code is before M05, when the ship framework
adds a large number of new types. The cheapest moment to *choose* is whenever
the game is clear enough to name — which is not now, and pretending otherwise
would produce a name that has to be lived with.

If the answer turns out to be "keep Ledger", nothing needs doing. If it is
anything else, say so and the rename is a single afternoon's mechanical work.

## Things that are decided

The *fiction* is not the name. `docs/design/ledger-design.md` describes a
corporate-feudal galaxy where information is the scarce commodity, and
`docs/design/living-world.md` describes what a player does in it. Neither
depends on what the game is called, and neither should be read as proposing one.
