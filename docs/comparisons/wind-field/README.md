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

## Which consumers are actually wired

The acceptance names five. Two exist:

- **Flight** — `ALedgerShip` hands the model `Field.WindCmPerSecond` from this
  subsystem every frame.
- **Vegetation** — the subsystem publishes `WindDirection` and `WindSpeed` to a
  material parameter collection each tick, which is how a foliage shader reads a
  world value. A clone without the content pack has no collection asset; the
  subsystem says so once and every other consumer is unaffected.

Three do not:

- **Particles** — there is no particle system yet. Blowing dust and rain are
  **T095**'s, and they will read `WindAt` like everything else.
- **Audio** — **T102**. `SpeedAt` exists for it, which is the number a wind bed
  is mixed against.
- **The settlement layer** — the settlement exists but has nothing wind-driven on
  it yet; there is no flag, no smoke and no rigging to move.

**This is a roadmap ordering note rather than a shortfall in the field.** T093 is
scheduled before three of the five things its acceptance says must consume it.
The field answers at any point and any altitude today, and the two consumers that
exist read it; the other three read it when they exist. Marking it done on the
strength of five consumers when three of them are empty would be marking it done
on a technicality, so it is recorded as what it is.
