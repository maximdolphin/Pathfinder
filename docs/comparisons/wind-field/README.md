# One wind

T093. **The point is that there is only one of it.** A wind that the flight
model, the grass, the particles, the audio and the settlement each worked out for
themselves would be five winds, and they would disagree the first time anybody
changed one — which is the failure the acceptance is written against.

So the physics lives in `LedgerWeather`, in latitude and longitude and metres per
second. `ULedgerWind` is the one place that turns a world position into that
question and the answer back into a world vector. It is a query, not a state:
nothing is ticked, accumulated or interpolated, because the weather is a function
of time and the wind now and the wind in an hour are two calls with different
arguments.

## The wind on the way down

Two things happen between the free atmosphere and your feet, and they are not the
same thing.

```
the free wind at 45 N is 17.2 m/s, and the friction layer is 2000 m deep

height    speed   fraction   backed by
     2 m     6.0     0.35     -16.3 deg
    10 m     8.6     0.50     -12.5 deg
    50 m    11.2     0.65      -8.7 deg
   200 m    13.5     0.78      -5.4 deg
  1000 m    16.1     0.93      -1.6 deg
  5000 m    17.2     1.00      +0.0 deg
```

**The speed falls logarithmically**, not linearly, because a turbulent boundary
layer over a rough surface has a log profile: `u(z)/u(h) = ln(z/z0)/ln(h/z0)`.
That is why a ten-metre mast reads half of what the free wind does and not a
five-hundredth, and it is the shape every wind atlas is written in.

**And the direction backs.** Friction breaks the balance between the pressure
gradient and Coriolis, and the leftover points down the gradient — so the surface
wind crosses the isobars towards the low instead of circling it forever. Sixteen
degrees here against a textbook twenty to thirty over land.

```
north backs -16.3 degrees and south +16.3
```

Opposite ways in the two hemispheres, because the sign comes from Coriolis rather
than from a constant. Above the friction layer the answer is *exactly* the free
wind — compared as zero difference, not as a small one.

## Drag acts on the speed through the air

The flight model gained one field and one subtraction:

```
after ten seconds from 50 m/s: still air leaves 0.39 m/s,
a 10 m/s wind leaves 10.31 m/s
and in the wind it ends up within 0.309 m/s of the air itself
```

**Drag drags a body towards the speed of the medium, and the medium is moving.**
Damping the ground speed instead is the same as asserting the atmosphere is
nailed to the planet, which is the assumption that makes a headwind and a
tailwind cost the same. With no wind the result is bit-for-bit what it was
before the term existed — asserted at exactly zero, so this could not have
quietly changed every trajectory in the project.

## The five consumers

When this task was first recorded, two of the five existed. They all do now, and
every one of them reads the same subsystem every frame:

- **Flight** — `ALedgerShip` hands the model the wind at the ship, and its drag
  acts on the speed through that air.
- **Vegetation** — `ULedgerWind` publishes `WindDirection` and `WindSpeed` to
  `/Game/Materials/MPC_LedgerWind`, and `M_Foliage` leans the canopies by it —
  by the dynamic pressure, so twice the wind is four times the lean, and by the
  square of the height up the tree, because a trunk is a cantilever. **Until
  this pass that consumer was fiction.** The collection asset had never been
  created, the log said "materials will not read the wind" on every run, and no
  material read it anyway; then `M_Foliage`'s first bake failed to compile
  silently, because a collection node binds by the parameter's GUID and the
  builder set only its name. See docs/comparisons/proper-world/.
- **Particles** — T095's rain, snow and dust move at the wind where they are
  plus their own fall speed, read every tick whether or not anything is falling.
- **Audio** — T102's wind noise is the dynamic pressure of the same wind at the
  listener.
- **The settlement** — a wind vane just off the pad: a twelve-metre pole and a
  streamer, both the baked building box scaled, turned each tick to stream
  downwind from whatever the field says is blowing twelve metres up.

## All five, timed

`-windprobe` changes the wind and times how long each consumer takes to follow.

```
before           t=0 s
after            t=259200 s (the free wind at the site turned 26 degrees)

  consumer      before m/s        after m/s         field m/s          moved   agreed after
  flight         5.5   3.4  -1.9   4.2   1.5  -3.7   4.2   1.5  -3.7   3.0   0.005 s, 1 frames
  vegetation    10.3   7.0  -2.1   8.3   3.5  -6.0   8.3   3.5  -6.0   5.6   0.005 s, 1 frames
  particles     10.3   7.0  -2.1   8.3   3.5  -6.0   8.3   3.5  -6.0   5.6   0.005 s, 1 frames
  audio         12.6 (speed)      10.8 (speed)      10.8 (speed)       1.8   0.005 s, 1 frames
  settlement     9.1   6.0  -2.2   7.2   2.9  -5.5   7.2   2.9  -5.5   4.9   0.005 s, 1 frames

every old reading was at least 3 tolerances from the new field
all five followed the change within a second: yes
```

One frame each, against an acceptance of a second. That is not a performance
number so much as a structural one: nothing between the field and a consumer
holds a copy, so there is nothing that could lag.

**Two requirements, and the first version of this fixture only had one.** It
checked that every consumer ended up agreeing with the new field, and it passed
a ship that had spawned two planetary radii away, where the change at the town
moved its reading by less than a metre a second — a ship that ignored the wind
entirely would have agreed just as well. So the fixture now also requires every
consumer's reading from *before* the change to be at least three tolerances from
the field *after* it, which is what makes agreeing mean something; and the ship
is put on the pad, in the same air as everything else, and left to fly its own
physics.

The change is chosen, not guessed: the moment in the next three days when the
wind at the site differs most from now, as a vector. It chose one with a 26°
turn and a 9.5 m/s change. An earlier version chose by angle alone, found the
same 26°, and refused to count it — correctly, since a turn is the wrong measure
of whether a stale reading is detectable.

Flight and the vane read the wind where they are; vegetation, particles and
audio read it at the viewer, because that is where they are drawn and heard.
Each is compared with the field at its own position.
