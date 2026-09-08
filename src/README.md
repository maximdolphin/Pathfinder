# The Phase 0 simulation — frozen spike

**Status: frozen 2026-09-08. Not under development. Do not extend.**

This is fifteen flat files, one of them 1,430 lines. That is not an oversight and
it is not a style anybody is defending — it is a spike that answered its
question and stopped.

## What it was for

One question: *can a deterministic belief simulation be made legible?* Can you
read a dump of a running world and trace a corporate collapse back four causes,
and see two NPCs disagreeing about the same fact for reasons you can follow?

The answer was yes. `out/world-dump.txt` is the evidence and M00's gate is
passed on it.

## Why it is not being refactored

The obvious move is to split it into the crates in
`docs/architecture/module-map.md` §2. That was planned as M01 T037–T039 and has
been withdrawn, because it is four days of restructuring code that gets
**rewritten** in M15, against a design that has already changed underneath it:
organisations, the power ceiling, coalitions, sites and directives all postdate
this code and none of them fit its shape.

Refactoring a prototype so that it can later be replaced is work for nobody.

## What is worth keeping when the real one is written

Not the structure. These:

- **Fixed-point `i64` arithmetic** in all authoritative state, and the discipline
  that no float ever enters it (`fixed.rs`).
- **SplitMix64 seeded from `(world_seed, region_id, tick)`** — no shared
  generator state, so any part of the world can be computed independently and
  identically (`rng.rs`).
- **`State_n = fold(reduce, State_0, events[0..n])`**, with events carrying their
  cause so a dump can walk backward (`event.rs`).
- **The belief graph** (`belief.rs`) — propositions with confidence, provenance
  and hop count, propagating with latency, decaying at different rates by kind,
  and distorting as they travel. The constants in there took five rounds of
  tuning against the §12 metrics; each round found a modelling gap rather than a
  knob, and the comments say which.
- **Disposition computed, never stored.** There is no attitude field anywhere,
  and that is what stops the social model becoming a set of numbers that drift.
- **Conservation laws** on goods and money, enforced by test over long runs.

## What happens to it

Nothing, until M15. It stays here, it stays out of CI's critical path, and the
client no longer depends on running it — `out/snapshot.json` is checked in as a
fixture so the region markers survive without a Rust build.

When M15 arrives, the crates in ARCH §2 get created as the real simulation is
written, and this becomes a reference to read rather than a codebase to move.
