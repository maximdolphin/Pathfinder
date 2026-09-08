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

### Running it

The editor target builds once the .NET Framework 4.8 SDK is present — `UnrealEd`
needs it, and VS Build Tools 2022 does not install it by default. If
`Build.bat LedgerEditor` fails with *"Could not find NetFxSDK install dir"*, add
`Microsoft.Net.Component.4.8.SDK` in the Visual Studio Installer.

```bash
# build the editor target
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat"   LedgerEditor Win64 Development -Project="D:\Ledger\client\Ledger.uproject"

# open the project
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"   "D:\Ledger\client\Ledger.uproject"
```

`ULedgerSimSubsystem` is an engine subsystem, so it loads the snapshot at editor
startup — no Play-In-Editor needed. It logs the contract board to `LogLedger`,
and registers two console commands: `Ledger.Refresh` and `Ledger.Board`.

Tier 6 tests, headless:

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"   "D:\Ledger\client\Ledger.uproject" -nullrhi -unattended -nosplash -nopause   -ExecCmds="Automation RunTests Ledger.Snapshot" -TestExit="Automation Test Queue Empty"
```

The standalone **game** target links but cannot be launched uncooked — a game
build needs cooked content. Use the editor until there is something to cook.

## Standards

Design §8 applies to every commit. In short: failing test first, typed enums
never magic strings, no floats in authoritative state, and every state mutation
is an event.
