# Caves and overhangs (T056) — partial, and here is exactly how far

A height field has one answer per direction, so it can never describe a passage
with ground above and below it. Caves are therefore a second, volumetric field;
the surface consults it only to know where to leave a hole.

Run it: `-caves` for the survey, `-caves -cavephoto -cavemesh` to photograph a
mouth. Meshing is behind `-cavemesh` and **off by default**, so the terrain
every other measurement in this repository is taken against is untouched.

## What works, with evidence

**The field.** Tunnels are the intersection of two noise level sets — where two
independent 3D fields are both near zero, which in three dimensions is a curve
rather than a region. Passages, not caverns, with no carving or culling.

```
cave country: 26.6% of the surface by area
first mouth at 14.00 lat, 54.50 lon, ground 158 m
local box 256 x 256 x 96 cells at 8 m: 1563 open (0.025% of the rock)
the passage from that mouth: 1123 cells, 680 m across, 648 m deep
it meets the sky at 35 cells, and its two furthest mouths are 120 m apart
```

That is the acceptance's substance measured without a mesher, a collision shape
or a pawn: a mouth that leads somewhere, and somewhere that comes back out.

**The hole in the surface.** `cave-approach.png`. The surface is generated as if
there were no caves and then the triangles standing where a passage breaches are
dropped — a height field cannot have a hole, so the hole is a deletion. The edge
is visibly sawtoothed because the terrain resolves 4.8 m and the passage is 14 m
across; that is the surface's resolution showing, not a bug in the field.

**The wall mesh runs.** Surface nets rather than marching cubes: one vertex per
crossed cell at the average of its edge crossings, four of them joined around
each crossed edge. Eighty lines and no 256-entry case table. 745 patches meshed
walls on the run that produced these pictures.

## What does not work, stated plainly

**The walls are not verifiably a closed surface.** `cave-inside.png` is taken
eighteen metres below the ground at the mouth and is a uniform sky blue: from in
there, every surface in the world is back-facing and the camera sees through all
of it. Either there is no wall at that point or the winding is inverted. The
faint speckle visible inside the slot in `cave-inside`'s predecessor says the
mesh does render from outside, so this is a question about orientation and
coverage rather than about whether geometry exists — but it is not answered, and
a cave you can see into and fall through is not a cave.

**And these are the things not started at all**, so that the list is honest:

- No LOD. A brick is meshed at one resolution for patches under 1.2 km and not
  at all above, so caves pop in as a wall of geometry at that boundary.
- No seam handling between bricks. Adjacent patches mesh independently and the
  brick boundary is deliberately left open rather than capped, which avoids a
  wall across every passage and leaves a crack instead.
- Cave rock has no material. The walls are painted with the terrain material and
  their vertex colour is unset, so all three biome slots weigh equally and a
  cave wall reads as a blend of the three grounds above it. A placeholder.
- Nothing lights the inside.

## Why it stops here rather than going on

> Walk into a cave mouth, through a passage, and out the other side, with
> collision and lighting correct throughout.

There is no pawn with legs. Embodiment is M09, sixty-odd tasks away, and there
is no way to walk anything anywhere until it exists. Collision is requested on
the cave section and lighting is not addressed, so two of the three things that
sentence asks to be correct throughout cannot be checked at all.

The field and the hole are worth having now because they are what the rest
depends on and because they are measurable now. The mesher is far enough along
to prove the approach and not far enough to ship, and the honest place to stop
is with that written down rather than with a screenshot of a slot in the ground
presented as a cave.
