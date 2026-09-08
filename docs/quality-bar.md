# The quality bar

*What "done" means. Written down so it can be held to, and so that falling short
is visible rather than arguable.*

The target is Star Citizen's **technical model and physical fidelity**, and it
is worth being exact about what that does and does not include.

---

## 1. What is promised

### The ship has to feel like a machine, not a vehicle

This is the part that is engineering rather than art, and it is the part I will
not compromise on. Each of these is a milestone gate, not an aspiration:

- **Thrusters are solved, not faked.** The allocator takes the actual positions
  and orientations of the thrusters and solves for the combination producing the
  commanded force and torque. Disable one and it compensates, losing authority in
  the axis the geometry predicts. Handling is a *consequence* of the layout, not
  a handling number somebody tuned. (M06)
- **Mass is real.** Centre of mass and inertia tensor computed from hull,
  components, fuel and cargo, updating as fuel burns. Load one side and it
  handles unevenly. (M05, M06)
- **The component graph is the ship.** Power plant, buses, thrusters, coolant,
  fuel, avionics, life support, shields — connected by typed ports. Shoot out a
  coupling and the thrusters on that bus go dead, heat climbs, and things fail in
  the order the topology explains. Repair it and it comes back. (M05)
- **A cold start is a sequence.** Systems come up in dependency order and
  skipping a step prevents the next one. (M05)
- **Air behaves like air.** Lift, drag and control surfaces from angle of attack
  and the atmosphere's actual density. A winged ship glides unpowered in
  atmosphere and does not in vacuum. Stall and spin have edges you can fall off
  and recover from. (M06)
- **Re-entry heats you** as a function of the density-velocity integral. A steep
  entry burns and a shallow one does not, because of the integral and not because
  of an altitude trigger. (M04, M06)
- **You can walk around inside it while it flies.** From the stern to the cockpit
  while the ship accelerates, rolls and crosses an atmosphere. Set a cup on a
  table and it stays there. Step onto a landing pad on a rotating planet and
  inherit the right frame. (M08)
- **Every gauge traces to a component reading.** A failed sensor makes its
  instrument *unavailable*, not wrong. (M05)

### The planet has to be a planet

- Real orbital mechanics. Watch a moon rise, transit and set on the schedule the
  ephemeris predicts, then fly to it. Sky and ephemeris agree to the arcminute.
  (M03)
- Surface of one planet to a moon of another **in one continuous shot** — no
  loading screen, no cut, no precision artefact anywhere on the path. (M07)
- Weather that is modelled, not placed: pressure cells, wind fields, storms that
  move. Fly into a front and visibility, wind loading and audio change together,
  and the same storm is visible from orbit where the model puts it. (M04)

### It has to look right

- Side by side against reference footage at three distances and three lighting
  conditions, **a stranger cannot immediately sort ours from theirs** on material
  and lighting alone. (M11)
- No placeholder geometry anywhere in the vertical slice. An automated audit
  says so. (M14)
- **Sixty frames a second, no frame over 16.7 ms**, in the densest city with a
  docked ship interior and traffic in view. (M13)

---

## 2. What is not promised

**Content breadth.** Star Citizen has hundreds of people and over a decade. The
plan is *one* system, *one* city, *one* station, *one* hostile outpost, *three*
ships — each to the standard above. Not thirty of anything.

One system done properly beats thirty done like a student project, and it is the
only version of this a single engineer can actually reach. Everything that scales
past that has to *generate* — which is why ADR-0004 insists on frameworks rather
than instances, and why buildings compose themselves from what their environment
demands rather than being modelled one at a time.

**Character fidelity is the weakest area** and I would rather say so now. Skin,
cloth, hair and facial animation are where a solo project most visibly falls
short of a studio, and no amount of architecture fixes it. Expect ships,
interiors, terrain and lighting to reach the bar before characters do.

**Four years, one engineer.** The schedule says so and it is not padded.

---

## 3. How I work, and how you can check

These are the process standards, and every one of them exists because it was
violated first.

- **Nothing is done without evidence attached.** Every completed task carries a
  capture, a report or a measurement in the roadmap. "It builds" is not evidence.
- **Verify by looking, not by reading the log.** A failed material produces one
  warning and a silent swap to the default. A screenshot contains no frame time.
  Checking the one image that happens to be right proves nothing.
- **Every measurement on an idle machine.** A profile measures a machine; a
  profile of a machine with something else running measures that instead.
- **Frame time is checked by the harness, not by eye.** Every run writes a report
  with a pass or fail against the 16.7 ms budget.
- **No file over 500 lines**, and the module layering is enforced by a script
  that fails the build, not by a document nobody reads.
- **Frameworks before instances**, inside every milestone.
- **A constant that was measured says what it was measured against**, in a
  comment, next to it.

---

## 4. Where this has already been fallen short of

Kept because a standards document listing only successes is a marketing
document.

- **The scripted flight ran at 19 fps for twenty-six tasks** and nobody noticed,
  because screenshots do not contain frame times. Fixed by making the harness
  record and judge them.
- **The town went unphotographed for two milestones.** A step I added stole the
  ship at the exact second the town capture was due, and it went unnoticed
  because the capture being inspected each run was one that was right.
- **A day went into optimising a cloud pass that costs 0.24 ms**, on the strength
  of a profile taken while another game was running. The "fix" traded away cloud
  draw distance to solve a problem that did not exist, and was reverted.
- **The sea rendered as grey default material for a whole task** because a
  material that fails to compile produces one warning line and a silent
  substitution, and every subsequent edit appeared to do nothing.

Every one of these is now something the tooling catches. That is the actual
standard: not that mistakes do not happen, but that each one leaves behind the
check that would have caught it.
