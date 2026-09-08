//! LEDGER — the authoritative simulation. Design §5.
//!
//! Headless, deterministic, engine-free. Everything here runs in CI with no
//! GPU, no editor, and no Unreal, which is the single decision the design leans
//! on hardest (§5.1) — a million-tick fuzz run against the economy invariants
//! is only possible because nothing in this crate knows what a renderer is.
//!
//! Module boundaries follow §8.3: each has one reason to change, and depends on
//! narrow interfaces rather than on its neighbours' internals. `world` is the
//! only module that may mutate state, and it does so exclusively by folding
//! events.

pub mod belief;
pub mod dump;
pub mod economy;
pub mod event;
pub mod fixed;
pub mod ids;
pub mod investigation;
pub mod metrics;
pub mod mission;
pub mod obligation;
pub mod rng;
pub mod snapshot;
pub mod world;
