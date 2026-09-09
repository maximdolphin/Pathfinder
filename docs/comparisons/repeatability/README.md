# The fixture could not reproduce itself (T427)

T427 asks the packaged build to produce captures matching the editor's. Four of
eight failed — town 4.16, coast 8.26, surface 10.13, sweep 14.70, against a mean
limit of 2.50 — and two explanations were written down for it. The first was
that captures fired before terrain had streamed; that was tested and wrong. The
second was that the packaged build stalls more, so at a given timestamp it has
flown less far and photographs a different place.

The second explanation was never tested either. The test is one line: **run the
editor build twice and compare it against itself.**

## Free-running: the same build disagrees with itself

Two runs of the same binary, back to back, on an idle machine, nothing else
changed. `coast-same-build/run-a.png` and `run-b.png` are the two coast frames;
`fixed-a.png` and `fixed-b.png` are the same pair with a fixed timestep. The
other twenty-eight images are not kept -- the numbers are the evidence and the
test is two commands.

```
terrain-coast.png:   mean 7.671, worst channel  29   (limits 0.5 and 16)
terrain-entry.png:   mean 0.656, worst channel  96
terrain-orbit.png:   mean 0.028, worst channel  20
terrain-space.png:   mean 1.335, worst channel 116
terrain-surface.png: mean 1.715, worst channel 128
terrain-sweep.png:   mean 0.592, worst channel  53
terrain-town.png:    mean 0.437, worst channel  43
```

Seven of eight shots disagree. The coast disagrees with itself by 7.67 — against
the 8.26 that had been attributed to the packaged build. **Most of what T427 was
measuring was the fixture's own noise.**

## Why

The flight is driven by game time, and game time advances by however long the
last frame took. Every position on the rails, every LOD decision, every frame of
Lumen probe history and TSR accumulation is therefore a function of the frame
rate — and the frame rate is a function of what else the machine was doing.
Nothing about this is specific to a packaged build. Two runs on one machine
differ for the same reason.

## Fixed timestep

`-useFixedTimeStep -fps=60` makes `DeltaSeconds` exactly 1/60 on every frame
regardless of how long the frame took, so two runs see identical simulated time.
Same test, same machine, same session:

```
terrain-coast.png:   mean 0.610, worst channel 21   (limits 0.5 and 16)
terrain-sweep.png:   mean 0.275, worst channel 21
six other shots: pass
```

Worst single channel across the whole set: **128 → 21**. Six of eight now pass
outright, and the two that do not are just over a limit that was set from
turntable data rather than from this fixture.

The residue is the part that is not a function of simulated time: Lumen's screen
probes and TSR converge over frames of *GPU* work, whose ordering a fixed
simulation step does not pin down.

## What this changes

- The scripted flight runs with `-useFixedTimeStep -fps=60` whenever its images
  are going to be compared. CI does this.
- The frame-time report no longer measures `DeltaSeconds`, which under a fixed
  step is the constant 16.7 whatever the machine actually did. It measures the
  wall clock, which is the same number in a free-running run and an honest one
  in a fixed-step run.
- Every capture comparison in the project before this — including the terrain
  component comparison in `../terrain-component.md` — was made against a fixture
  with this much noise in it. The component comparison survives it, because the
  difference it found was 199 against a repeat noise of 128, but that is a
  narrower margin than it looked.
