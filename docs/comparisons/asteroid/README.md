# Standing on something that is not a sphere

T081. The acceptance: *land on an irregular asteroid with correct local gravity
direction at every point on its surface.*

**"Correct" is the load-bearing word.** The cheap answer — straight at the centre
— is exactly right on a sphere, wrong on everything else, and looks right in a
screenshot either way. So gravity here is a sum over the body's actual mass
rather than a formula about its middle.

## The check that the sum is a gravity field

Set the irregularity to zero and the body is a sphere, where the shell theorem
says the answer must be `GM/r²` pointing exactly at the centre. A
fifty-thousand-term sum that cannot reproduce the one case anybody can do by
hand is not reproducing the others either.

```
integrated mass 1.04657e+15 kg against an exact 1.0472e+15 kg (0.060% out)

  at  1.05 radii: magnitude 0.6221% out, direction 0.1954 degrees off
  at  1.50 radii: magnitude 0.1192% out, direction 0.0105 degrees off
  at  3.00 radii: magnitude 0.0662% out, direction 0.0005 degrees off
  at 10.00 radii: magnitude 0.0603% out, direction 0.0000 degrees off
```

## The lean

```
across 400 surface points: gravity leans 3.09 degrees off radial on average,
                           7.64 at worst
the same integrator on a sphere leans 1.0242 degrees
the lumpy body leans 7.5 times as far as the integrator's own floor
```

**The floor is measured, not assumed, and the claim is stated against it.**
Evaluating gravity exactly *on* a surface is the integrator's worst case — the
point sits amid the cells it is summing — so a perfect sphere still reads about
a degree of false lean there. That is real and will not go away without a much
finer grid. The honest claim is not "the floor is small"; it is that the lumpy
body's lean is seven and a half times it, measured the same way in the same
place.

An earlier version asserted the floor was under half a degree. It is not. The
bound was wrong, and comparing signal to a measured floor is a better test than
comparing it to a number somebody picked.

## Every point

```
2000 surface points: 0 with gravity pointing outwards
worst local slope 34.79 degrees
surface gravity runs 0.002546 to 0.002765 m/s^2, a factor of 1.09
```

Nowhere on it throws a lander off, and nowhere is steeper than loose rubble
holds — **34.79° against an angle of repose near 35°**. You weigh nine per cent
more at one end than the other, because the near end of a lumpy body is nearer
its own mass.

### The shape was walked down, not the bound walked up

At an irregularity of 0.32 the body carried 50° slopes; at 0.22, 40°. Loose
material sits at about 35° and slumps past it, so a body with faces that steep
would have rearranged itself long before anybody landed on one. The shape is now
0.18 and its slopes are ones a real asteroid is observed to hold.

## A trap worth naming

The first run reported gravity pointing **90.0000 degrees off** at ten radii,
with magnitudes correct to a fifth of a per cent the whole way out.

`GetSafeNormal()` tests the *squared* length against a default 1e-8. An asteroid
pulls at a few thousandths of a metre per second squared, and ten radii out it
is 2.8e-5 — whose square is 7.8e-10, comfortably under the threshold. The
default normalise returned a zero vector and every angle taken from it came out
as a right angle.

It is documented on `GravityAt` now, because any caller normalising a
micro-gravity vector meets the same wall.

## What is not here

The mesh. `out/t081-asteroid.png` charts the shape and the lean over the
surface, and the shape is a displaced sphere — defined everywhere, askable in
any direction, and the same function that gives the surface gives the mass
distribution. Turning it into drawn geometry reuses the icosahedron path T434
built for stones; that is a rendering job and not what decides whether the
gravity is right.
