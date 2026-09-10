# Rings: where they can be, and the shadow they throw

T080. The acceptance: *fly through a ring plane — density resolves into
particles and the ring's shadow moves across the planet correctly.*

**The shadow half is delivered and measured. The fly-through is not**, for the
same reason T079's descent is not, and T080 is recorded blocked rather than
done.

## The shadow sweeps a year

`out/t080-ring-shadow.png` is a year in twelfths, each column a pole-to-pole
slice of the giant with the ring's shadow drawn on it.

```
   0/12: declination -16.65, shadow  +4.00 to +27.00  (23.00 wide)
   2/12: declination  -7.78, shadow  +2.00 to +13.00  (11.00 wide)
   3/12: declination  +0.25, no shadow at all
   4/12: declination  +7.82, shadow -12.00 to  -2.00  (10.00 wide)
   6/12: declination +16.46, shadow -27.00 to  -4.00  (23.00 wide)
   9/12: declination  +3.68, shadow  -6.00 to  -1.00  ( 5.00 wide)
  11/12: declination  -5.49, shadow  +3.50 to +20.50  (17.00 wide)
```

Three things fall out of that table and none of them were written:

- **The shadow is always in the hemisphere the star is not over.** Declination
  negative, shadow north; declination positive, shadow south. The ring shades
  the winter side, because that is the side the light has to cross it to reach.
- **It vanishes at the equinox.** At 3/12 the star is 0.25° out of the ring
  plane and there is no shadow at all — the ring is edge-on to the light and
  throws a line of zero width. This is what Saturn does every fifteen years.
- **Its width tracks the declination.** 23° across when the star is 16° out of
  the plane, 5° when it is 3.7° out.

There is no shadow code that knows about seasons. There is a line from a point
on the surface towards the star, and a question about whether it crosses the
ring's annulus on the way.

## Where a ring can be

**A ring is where a moon cannot be.** Inside the Roche limit, tidal forces pull
a self-gravitating body apart faster than its own gravity can hold it together,
so anything there stays rubble. The outer edge is that limit, computed for icy
rubble — not chosen:

```
gas giant radius 80715 km; Roche limit for ice 158530 km (1.96 radii);
rings from 96858 to 158530 km
```

1.96 body radii, against Saturn's rings ending at about 2.3 of its own. The
right neighbourhood rather than a coincidence of units.

### The reasoning this task got wrong

The first version refused rings to rocky bodies, on the grounds that a rocky
body's Roche limit sits barely outside its own surface so there is nowhere to
put them.

**That is backwards, and the test said so with both numbers side by side:**

```
the rocky planet's Roche limit is 4.40 radii against the giant's 1.96,
because it is the denser body
```

The limit scales with the cube root of *the primary's* density. A dense rocky
planet has **more** room for icy rings than a puffy gas giant does. Earth's
Roche limit for ice is over four Earth radii.

The real reason the inner planets have no rings is supply: ring material is ice,
ice is not solid inside the frost line, and what little arrives is swept up or
dragged down long before anybody looks. So the condition is now about where a
body formed and what it is — a formation question — and the Roche limit does the
job it actually does, which is setting the outer edge.

## Gaps

A gap is a resonance. A particle whose orbital period is a simple ratio of a
shepherd moon's gets the same nudge at the same point every few orbits and is
eventually somewhere else, which is why Saturn's Cassini division is where it
is.

```
5 gaps across the ring, covering 23.94% of its width
  a gap starting at 113848 km (0.275 of the way out)
  a gap starting at 126167 km (0.475 of the way out)
  a gap starting at 142480 km (0.740 of the way out)
```

Since period goes as the three-halves power of radius, a period ratio of N is a
radius ratio of N to the two-thirds — so the gaps land where the moon puts them
and nowhere else. `Ledger.Rings.ResonancesWithAMoonClearGaps` checks the
converse too: **with no moon, the ring has zero gap samples.** They come from
the moon rather than from a stripe pattern.

## What is not delivered

You cannot fly through it. Density resolving into particles at close range needs
the same volumetric machinery T079's descent needs, and the same atmosphere
calibration is in front of it.

What exists is the ring's geometry — extent, gaps, optical depth — and the
shadow it throws, which is the half that can be computed and checked. Committed
and measured; the other half named.
