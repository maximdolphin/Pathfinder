# ADR-0001 — Tech stack, and what is deferred past Phase 0

**Status:** Accepted
**Date:** 2026-09-07
**Supersedes:** nothing
**Related:** design §9 (stack), §13.1 (Phase 0), §5.1 (why the sim is not in Unreal)

## Context

§8.5 requires an ADR for architectural decisions, and §16 step 3 asks for this one specifically. Two things need recording: the original stack choice, and the v1.1 amendments to it.

The Phase 0 gate asks one question — *is the world interesting in a text file?* — and answers it with a text dump read by a human. A large part of the stack in §9 as originally written serves a world that has already passed that gate.

## Decision

**Sim core in Rust, fixed-point `i64`, event-sourced.** Unchanged from §9. Exhaustive `match` makes unhandled states a build failure; ownership makes fold-over-event-log natural; no engine dependency means the invariant suite runs in CI.

**The wire *contract* and the wire *transport* are separate decisions.** protobuf is the contract and is non-negotiable — it is what makes the boundary structurally incapable of carrying a magic string (§8.2). gRPC is one transport for that contract, and not the one Phase 1 needs.

**Phase 0 runs single-process with no external dependencies.** Event log is a `Vec<Event>` serialised to a file. Postgres arrives at Phase 1, Redis at Phase 2, gRPC at Phase 2, OpenTelemetry at Phase 1. §12's metrics are printed into the text dump from day one — measured, not dashboarded.

**Crate layout starts as one crate.** §10's ten-crate layout is the Phase 1 target. Split a crate when it genuinely needs its own dependency set or compile unit.

## Consequences

**Good.** Phase 0's dependency surface is `serde` and a test framework, so the gate is reachable in weeks rather than months, and a NO-GO discards proportionally less work. The gRPC-in-UE5 integration slog — UE disables exceptions and RTTI, and its `check`/`verify` macros collide with protobuf and abseil headers — moves from an unbudgeted Phase 1 surprise to a scheduled Phase 2 work item.

**Bad.** Deferred infrastructure is deferred, not avoided; Phase 1 pays a migration from `Vec<Event>` to Postgres. This is cheap precisely because event sourcing makes the store an implementation detail behind an append-and-fold interface — but it is not free, and the interface must be kept honest from the first commit or the migration stops being cheap.

**Risk.** "Split the crate later" degrades into one large crate if nobody splits. The §10 layout is the target; revisit at the Phase 0 → Phase 1 boundary.

## Rejected alternatives

- **Sim inside UE (C++).** Cannot run in CI without an engine, and couples the crown jewel to engine churn. Rejected.
- **Sim in C#.** Viable, but float determinism requires constant vigilance rather than compile-time enforcement. Rejected.
- **Server meshing / 400-player scale.** Explicit anti-goal (§1.1, §6.10). Rejected.
- **gRPC for the Phase 1 bridge.** Multi-day UE third-party module integration to buy streaming and backpressure that a single-player contract board does not use. Deferred, not rejected — it is the Phase 2 answer.
- **Postgres in Phase 0.** Buys durability and queryability for a world that may be deleted at the gate. Deferred.
