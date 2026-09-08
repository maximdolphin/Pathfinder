# Coding standards

Short on purpose. A standard nobody finishes reading is a standard nobody
follows, and most of what would go in a long one is already enforced by the
build (`docs/architecture/module-map.md` §2.1, Rule 2).

## Where things go

The module map is the answer. If a file's home is unclear, the design is
unclear — resolve that first rather than putting it somewhere plausible.

**No file over 500 lines.** Not a style preference: a file past that has stopped
having a subject, and the three god-modules the prototype produced were all
built one reasonable addition at a time. When a file approaches the limit, the
question is what two things it is doing, not how to fit.

**A header declares one subject.** `NodeKey` was private to one translation unit
for months while three files needed it, purely because nothing forced the
question. If two files need a thing, it gets a header.

## Naming

Unreal's conventions, since it is Unreal: `F` for plain structs, `U` for objects,
`A` for actors, `E` for enums, `b` for booleans. Members and functions in
`PascalCase`, locals too.

Names say what a thing *is*, not what it is made of. `PatchCache`, not
`PatchMap`. `WorstUnfilled`, not `MaxHoleCount`.

## Comments

**Comments explain why, and the code explains what.** A comment restating the
line above it is noise; a comment recording why a number is 0.35 rather than 2.0
is the only place that information will ever exist.

Three things are worth a comment every time:

1. **A constant that was measured.** Say what it was measured against. Cloud
   sample scale is 0.35 because 2.0 cost 90 ms of a 130 ms frame.
2. **A decision with a rejected alternative.** Say what was rejected and why.
   Water is opaque rather than translucent because translucent surfaces do not
   write depth and are skipped by screen-space reflection.
3. **A bug that cost a day.** The failure mode, not the fix. A material that
   fails to compile produces one warning and a silent swap to the default, so
   the surface renders grey and every subsequent edit appears to do nothing.

Mark deliberate simplifications `ponytail:` with the ceiling and the upgrade
path, so a shortcut reads as a decision rather than as ignorance.

## Determinism

Anything that feeds world generation or the simulation is a pure function of its
seed. No wall clock, no frame counter, no shared random state, no iteration
order over a hash map. This is testable and it is tested; see ARCH Rule 5.

Fixed-point `i64` in all authoritative simulation state. Floats are fine in the
client, which is presentation.

## Tests

Non-trivial logic leaves one runnable check behind — the smallest thing that
fails if the logic breaks. Not a suite per function.

**Verify by looking, not by reading the log.** Three bugs survived review
because only the log was checked, or only the image that happened to be right.
After a change to the scripted flight, open the captures and read
`out/performance.txt`.

## Formatting

`.clang-format` for C++, `rustfmt` for Rust, both enforced in CI. Tabs, Allman
braces, 100 columns — Unreal's, so engine code and project code read the same.
Nothing here is worth an argument, which is the point of automating it.
