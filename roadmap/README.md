# LEDGER roadmap

A year and a bit of planned work, in one linear order, with a gate at every
checkpoint and space to attach the proof that each one was passed.

**One writer, many readers.** The agent building the game maintains this through
`scripts/task.mjs`; the React app is a read-only window onto the same file.
There are no buttons, because a second writer would eventually disagree with the
first and the roadmap would stop being trustworthy.

The whole database is `public/roadmap.json` in this git repository. Every status
change is therefore a diff with a date on it, and the history of the project is
the history of the file.

## Look at it

```bash
npm install
npm run dev
```

Then open http://localhost:5178. It polls the JSON, so it updates as work lands
without a reload.

## Drive it

```bash
node scripts/task.mjs status                    # where everything stands
node scripts/task.mjs next                      # the one thing to do now
node scripts/task.mjs show T042                 # full detail, including acceptance
node scripts/task.mjs show M3                   # a milestone and its gate
node scripts/task.mjs list --milestone M1 --status todo
node scripts/task.mjs list --search erosion

node scripts/task.mjs start T042
node scripts/task.mjs note T042 "patch generation now 4.2 ms"
node scripts/task.mjs done T042 --note "landed in c58cf58"
node scripts/task.mjs block T042 --reason "waiting on IP counsel"
node scripts/task.mjs unblock T042

node scripts/task.mjs evidence T042 ../out/terrain-orbit.png --caption "..."
node scripts/task.mjs evidence M1 ../out/transect.mp4 --caption "..."
node scripts/task.mjs gate M1                   # show the gate and readiness
node scripts/task.mjs gate M1 --pass --note "..."
```

Two rules the CLI enforces rather than suggests:

- **One task active at a time.** A plan with three things in flight and one
  developer is not a plan, it is three half-finished things.
- **A gate cannot be passed without evidence, or with unfinished tasks.** A gate
  without proof is an opinion.

Evidence files are *copied* into `public/evidence/`, not referenced. Proof that
lives outside the repository is proof that can quietly disappear, and a
milestone whose evidence has vanished is a milestone nobody can check.

## Change the plan

The tasks are authored as Python data in `scripts/roadmap_data*.py` — they
belong in version control next to the code they describe, where a change to the
plan shows up in a diff.

```bash
python scripts/generate_roadmap.py           # regenerate, preserving progress
python scripts/generate_roadmap.py --wipe    # regenerate, discarding progress
```

Progress is merged forward by task id. A task that disappears from the plan takes
its progress with it, which is correct: it is no longer work anybody is doing.

`generate_roadmap.py` also **calibrates** the schedule. The task estimates and
the milestone week ranges were authored separately and disagreed by a factor of
two; rather than re-guess two hundred and forty estimates, the relative weights
are kept and each milestone's total is scaled to the weeks it was allotted. The
relative weights are the part that was reasoned about. The absolute ones were
never more than a guess at velocity.

## Shape of the plan

| | | |
|---|---|---|
| M0 | Foundations | done — sim, planet, town, ship |
| M1 | Terrain to production standard | water, stable LOD, cooked assets |
| M2 | Surface fidelity | biomes, texture sets, day/night |
| M3 | Ship systems and flight | cockpit, HUD, gear, fuel, damage |
| M4 | Sim to client bridge | protobuf over TCP, live world state |
| M5 | The bounty loop, end to end | **Phase 1 gate** |
| M6 | Narration | local model, constrained decoding |
| M7 | Economy and the news feed | markets that move because of events |
| M8 | Crew | the losable thing |
| M9 | All six mission archetypes | one algorithm, six verbs |
| M10 | Simulation LOD and reification | a world that ticks without you |
| M11 | Multiplayer foundation | 16–32 players, IP counsel |
| M12 | MVP gate | **the four pillar tests** |

Every milestone's gate is a question that can be answered yes or no by looking at
a build. That is deliberate: the design document (§2, §13) insists on falsifiable
gates, and a roadmap of unfalsifiable intentions cannot tell you whether it is on
schedule.
