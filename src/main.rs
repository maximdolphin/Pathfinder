//! The Phase 0 harness. Design §13.1.
//!
//! Runs the sim headless for N ticks with a scripted agent, writes the text
//! world-dump, and writes a snapshot for the client. Zero rendering, zero
//! Unreal, no art — and no external dependencies, so the whole thing runs in CI
//! in under a second.
//!
//! Usage: `ledger [ticks] [seed] [entities]`

use ledger::{dump, metrics, snapshot, world::World};
use std::time::Instant;

const DEFAULT_TICKS: u64 = 50_000;
const DEFAULT_SEED: u64 = 20260908;
const DEFAULT_ENTITIES: usize = 200;

fn arg<T: std::str::FromStr>(position: usize, fallback: T) -> T {
    std::env::args()
        .nth(position)
        .and_then(|a| a.parse().ok())
        .unwrap_or(fallback)
}

fn main() -> std::io::Result<()> {
    let ticks: u64 = arg(1, DEFAULT_TICKS);
    let seed: u64 = arg(2, DEFAULT_SEED);
    let entities: usize = arg(3, DEFAULT_ENTITIES);

    let mut world = World::genesis(seed, entities);

    let started = Instant::now();
    for _ in 0..ticks {
        world.tick();
    }
    let elapsed = started.elapsed();

    let per_tick_micros = if ticks == 0 {
        0
    } else {
        elapsed.as_micros() / ticks as u128
    };

    let measured = metrics::collect(&world, per_tick_micros);
    let text = dump::render(&world, &measured);

    std::fs::create_dir_all("out")?;
    std::fs::write("out/world-dump.txt", &text)?;
    std::fs::write("out/snapshot.json", snapshot::to_json(&world))?;

    println!("{}", text);
    println!(
        "\n{} ticks in {:.2}s ({} us/tick) · {} events\n\
         wrote out/world-dump.txt and out/snapshot.json",
        ticks,
        elapsed.as_secs_f64(),
        per_tick_micros,
        world.log().len()
    );

    Ok(())
}
