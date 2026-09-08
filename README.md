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

`client/` is a UE 5.8 C++ project with **no Blueprint logic and no content**.
There is no `.umap` with anything placed in it: a world subsystem spawns the
planet, its atmosphere, the sun, a town and the ship on begin play, so the whole
scene is source rather than assets.

What runs:

| | |
|---|---|
| Planet | 6,371 km radius, cube-sphere quadtree, 4.8 m quads at the finest LOD |
| Terrain | domain-warped gradient noise, 8-octave ridged multifractal, analytic erosion |
| Surface | triplanar detail from generated mip-mapped textures, two scales, depth-faded |
| Atmosphere | Sky Atmosphere at Earth's own values, ozone included; volumetric cloud deck at 2–8 km |
| Town | 32 buildings and 342 trees, laid out in a tangent basis and projected onto the sphere |
| Ship | 6-DOF flight, inverse-square gravity, exponential drag, ground contact |
| Generation | patches built on worker threads; the game thread only uploads |

Launching the game runs a scripted sequence — orbit, reentry, landing, a look at
the town, then a climb back to space — capturing to `out/` as it goes. The climb
runs through the ship's own flight model, so whether it reaches orbit is a
question about the numbers rather than about the animation.

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"   "D:\Ledger\client\Ledger.uproject" -game -windowed -ResX=1600 -ResY=900
```

Controls after the scripted sequence hands over: `W`/`S` throttle, `A`/`D`
strafe, `Q`/`E` roll, `Space`/`Ctrl` lift, mouse to pitch and yaw.

### Building

The editor target needs the .NET Framework 4.8 SDK — `UnrealEd` requires it and
VS Build Tools 2022 does not install it by default. If `Build.bat` fails with
*"Could not find NetFxSDK install dir"*, add `Microsoft.Net.Component.4.8.SDK`
in the Visual Studio Installer.

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat"   LedgerEditor Win64 Development -Project="D:\Ledger\client\Ledger.uproject"
```

Tier 6 tests, headless:

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"   "D:\Ledger\client\Ledger.uproject" -nullrhi -unattended -nosplash -nopause   -ExecCmds="Automation RunTests Ledger.Snapshot" -TestExit="Automation Test Queue Empty"
```

Console: `Ledger.Board`, `Ledger.Refresh`, `Ledger.Terrain.Stats`.

### Known limits

The terrain material is generated at runtime, so it exists only in an editor
build; a packaged game needs it saved as an asset. Oceans are coloured terrain,
not a water surface. Nothing walks around — the player is the ship.

## Standards

Design §8 applies to every commit. In short: failing test first, typed enums
never magic strings, no floats in authoritative state, and every state mutation
is an event.
