# Twenty systems, all plausible, none a rearrangement of another

T084. **"Plausible" is not an opinion here.** Every rule the checker applies was
built and measured by an earlier task in this milestone — the frost line from
T079, the Roche limit from T080, the Hill sphere from T083, the mass-luminosity
relation from T076. This is where they stop being descriptions of one system and
become constraints on all of them.

## The twenty

```
   0:  8 bodies, star 0.90 solar (0.663 L), 3 rocky 1 giants  2 moons, home at 0.914 au
   1: 18 bodies, star 0.93 solar (0.763 L), 2 rocky 4 giants 10 moons, home at 0.912 au
   2: 15 bodies, star 1.44 solar (4.352 L), 3 rocky 3 giants  7 moons, home at 2.490 au
   4: 11 bodies, star 1.19 solar (2.010 L), 1 rocky 3 giants  5 moons, home at 1.886 au
   8: 19 bodies, star 1.45 solar (4.376 L), 2 rocky 4 giants 11 moons, home at 2.926 au
  13: 16 bodies, star 0.63 solar (0.153 L), 2 rocky 4 giants  8 moons, home at 0.495 au
  15:  8 bodies, star 1.43 solar (4.213 L), 3 rocky 1 giants  2 moons, home at 2.692 au
```

Eight to nineteen bodies; stars from 0.63 to 1.45 solar masses, which is a
luminosity range of **0.153 to 4.376**.

**The habitable zone follows the star, and nothing says so anywhere.** The dim
star's world sits at 0.495 au and the bright one's at 2.926, because the zone
goes as the square root of the luminosity and the luminosity goes as a power of
the mass. Pick a star and the rest follows.

Across all 190 pairs: **0 identical, 0 rearrangements.** Sixteen pairs share
only a body count, which is expected — there are only so many counts — and is
recorded so the number is not mistaken for a near miss.

## The checker earns its keep by refusing

A plausibility function that says yes to everything is not a checker, so each
rule is shown to bite by breaking a real system in exactly one way:

```
  gas giant inside the frost line    Giant 2 is a gas giant at 0.60 au, inside the frost line at 2.20
  home world too close to the star   the home world is at 0.120 au, outside the habitable zone of
                                     0.773 to 1.221 for a star of 0.663 solar luminosities
  moon inside the Roche limit        Companion orbits Home inside its Roche limit and would be a ring
  moon the star would steal          Companion orbits Home beyond half its Hill sphere
  a rocky planet denser than iron    Home has a density of 2198768 kg/m3, outside 2500 to 8000
  a home world on a comet's orbit    Home has an eccentricity of 0.700
```

**And refused for the *right* reason.** Checking only that a broken system is
rejected lets a case pass on somebody else's rule, and two did:

- Moving the home world out to four astronomical units was meant to test the
  habitable zone. It was refused for landing next to a gas giant. True, and not
  the thing being tested.
- Moving it *in* to a tenth of an au instead was refused because a Hill sphere
  scales with the orbit, so its companion ended up outside a sphere that had
  shrunk by seven — and the moon rule fires before the zone rule is reached.

Breaking one thing means breaking exactly one thing. The moon now moves with the
planet, and each case asserts the reason it names.

## What the checker found in the generator

The first run rejected **sixteen of twenty** systems, and every rejection was a
real defect:

**"The planets are not in order of distance"** was the checker's fault, not the
generator's. It was a rule about the array rather than about the system, and it
refused any description that listed the home world before an inner planet. Where
a body sits in the description is nobody's business but the description's; the
planets are sorted by distance before the spacing is checked now.

**Moons had moons.** The giant's index was computed inside the moon loop as
`Num() - 1`, which is the giant only until the first moon is added — after that
it is the previous *moon*. Eighteen systems had a moon orbiting a moon. The
index is taken before the loop.

**Pairs seven or eight mutual Hill radii apart.** A geometric walk outwards does
not know how heavy the next planet is. The mass is chosen first now, and the
orbit is pushed out until there is room for it.

**Small gas giants lighter than balsa.** Radius-goes-as-mass-to-the-0.08 says a
small giant is nearly as wide as a large one and therefore far less dense; a
0.12-Jupiter body came out at **265 kg/m³**. Real small giants are *denser* —
Neptune is a sixteenth of Jupiter's mass, a third of its radius, and half again
its density. The model is now two segments fitted through Neptune, Saturn and
Jupiter: 0.52 below a third of a Jupiter mass and 0.14 above, where degeneracy
holds the radius flat however much is added.

## Two indices, not five

`Generate` used to produce exactly five bodies, and seven test files had learned
that body 3 was the gas giant and body 4 the station. A system with four planets
and one with seven cannot both put a giant at index 3.

Only **body 0 (the star) and body 1 (the home world)** are fixed now. Everything
else is asked for by role — `FirstOfKind`, `FirstChildOfKind` — and the seven
files were converted. Two of them were then found to be looking at the wrong
body entirely: an eclipse test measuring the subtended size of something that
was nowhere near the sun, and a noon test whose "moon" had become an inner
planet with a months-long day.

The mass-luminosity relation moved to `LedgerBodies` in the same pass, because
the generator needs it before anything has a sky — a system has to be laid out
before it can be looked at, and two copies of a piecewise power law is two
chances for a system to be laid out against one and lit by the other.
