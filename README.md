# LEDGER

A corporate-feudal simulation where information is the scarce commodity.
Design: [docs/design/ledger-design.md](docs/design/ledger-design.md). Decisions: [docs/adr](docs/adr).

Current phase: **Phase 0** — proving the world is interesting in a text file,
before any rendering exists (design §13.1).

## Layout

```
src/        the sim — Rust, headless, deterministic, zero dependencies
tests/      Tier 2 invariants and Tier 4 replay determinism
client/     the UE5 client — C++ only, no Blueprint logic
out/        generated: world-dump.txt and snapshot.json (gitignored)
```

## Run the sim

```bash
cargo run --release
```

50,000 ticks in about 2.5 seconds. Writes `out/world-dump.txt` (the thing the
Phase 0 gate asks you to read) and `out/snapshot.json` (what the client reads).

Arguments are `ticks seed entities`, so a shorter run is `cargo run --release -- 5000`.

## Test

```bash
cargo test --release
```

69 tests. The long-horizon Tier 3 fuzz is `#[ignore]`d for normal runs:

```bash
cargo test --release -- --ignored one_million_ticks
```

## The Phase 0 gate

Read `out/world-dump.txt` and answer three questions (§13.1):

1. Can you trace a corporate collapse back four causes?
2. Do two NPCs disagree about the same fact for legible reasons?
3. Did anything surprise you?

The metrics table at the bottom of the dump is the falsifiable half. **If the
dump is boring, the game is boring** — no amount of Unreal fixes that.

## The client

`client/` is a UE 5.8 C++ project with no Blueprint logic and no content. It
reads `out/snapshot.json` through a typed boundary and prints the contract
board. It exists to prove the sim→client contract, not to render anything.

```bash
# from client/
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" \
  Ledger Win64 Development -Project="D:\Ledger\client\Ledger.uproject"
```

Console commands once running: `Ledger.Refresh`, `Ledger.Board`.

### Known blocker

The **editor** target does not build on this machine:

```
Unable to instantiate module 'SwarmInterface': Could not find NetFxSDK install dir
```

Unreal's `UnrealEd` module needs the .NET Framework 4.6+ SDK, which is not
installed alongside VS Build Tools 2022. The **game** target builds and links
fine (`client/Binaries/Win64/Ledger.exe`), but a standalone game target cannot
run against uncooked content, so it cannot be launched without either the editor
or a cook.

Fix: install the .NET Framework 4.8 SDK — the "Microsoft.Net.Component.4.8.SDK"
component in the Visual Studio Installer, or Microsoft's standalone Developer
Pack. After that, `Build.bat LedgerEditor Win64 Development` and the editor
opens the project.

## Standards

Design §8 applies to every commit. In short: failing test first, typed enums
never magic strings, no floats in authoritative state, and every state mutation
is an event.
