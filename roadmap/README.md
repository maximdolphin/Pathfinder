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

**Generated from `public/roadmap.json`, not maintained by hand.** The
version of this table before 2026-09-10 described a thirteen-milestone plan
that had not existed for some time. A summary that drifts from the thing it
summarises is worse than no summary.

| | | done | |
|---|---|---|---|
| M00 | Prototype spike (delivered) | 26/27 | wk -6-0 |
| M01 | Architecture and the split | 16/16 | wk 1-4 |
| M2W | The world as an Unreal project | 11/11 | wk 5-9 |
| M02 | Terrain to production standard | 15/27 | wk 10-21 |
| M2S | Close-range surface fidelity | 6/9 | wk 22-28 |
| M03 | Planetary bodies and orbital mechanics | 0/20 | wk 29-36 |
| M04 | Atmosphere, weather and environment | 0/19 | wk 37-44 |
| M05 | Ship framework: hulls, components, subsystems | 0/24 | wk 45-54 |
| M06 | Flight model in vacuum and in air | 0/21 | wk 55-63 |
| M07 | Seamless travel across a system | 0/15 | wk 64-72 |
| M08 | Physics at scale and local grids | 0/16 | wk 73-81 |
| M09 | Embodiment and animation | 0/20 | wk 82-92 |
| M10 | Interiors and modular architecture | 0/14 | wk 93-101 |
| M11 | Rendering fidelity | 0/17 | wk 102-112 |
| M12 | Asset pipeline and world content | 0/19 | wk 113-121 |
| M13 | Performance and optimisation | 0/11 | wk 122-129 |
| M14 | Technical vertical slice | 0/11 | wk 130-137 |
| M15 | Simulation core and determinism | 0/17 | wk 138-145 |
| M16 | Agent framework | 0/17 | wk 146-157 |
| M17 | Organisation framework | 0/14 | wk 158-165 |
| M18 | Economy and logistics | 0/13 | wk 166-173 |
| M19 | Sites, construction and city life | 0/17 | wk 174-183 |
| M20 | Power, threat and coalitions | 0/13 | wk 184-191 |
| M21 | Missions and directives | 0/13 | wk 192-200 |
| M22 | Presence and Command | 0/10 | wk 201-207 |
| M23 | The living world, integrated | 0/11 | wk 208-216 |
| M24 | Vertical slice: nothing to coalition | 0/14 | wk 217-227 |

Every milestone's gate is a question that can be answered yes or no by looking at
a build. That is deliberate: the design document (§2, §13) insists on falsifiable
gates, and a roadmap of unfalsifiable intentions cannot tell you whether it is on
schedule.
