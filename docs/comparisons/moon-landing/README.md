# A moon, on the same terrain framework

T078. The acceptance: land on a moon using the same terrain code path, with
correct low gravity and no atmosphere.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -body=2`. That is the
whole interface. `-body=` chooses which row of the system description the world
builds, and nothing else changes — same quadtree, same streamer, same materials,
same scatter, same climate field.

```
body 1 (planet): radius 6320 km, gravity 9.55 m/s^2 (0.973 g), escape 10985 m/s, 245 K, air yes
body 2 (moon):   radius 2159 km, gravity 0.99 m/s^2 (0.101 g), escape  2067 m/s, 245 K, air none
```

**The moon pulls at 0.104 of the planet's surface gravity.** Nobody chose that
number: it is `GM/r²` for a body whose mass and radius were generated in T069,
handed to the flight model in the centimetres it works in.

## Who keeps an atmosphere is derived, not declared

A flag on the body would have to be set by whoever generated it, and would then
be a second opinion about something the mass and radius already decide. So the
rule is the physical one — compare escape velocity with the thermal speed of a
nitrogen molecule at the body's equilibrium temperature, because a gas is a
distribution and its fast tail leaves first.

```
  Earth    escape  11186 m/s, thermal  477 m/s, ratio  23.5 -> air     (expected air)
  Moon     escape   2375 m/s, thermal  490 m/s, ratio   4.8 -> none    (expected none)
  Mars     escape   5027 m/s, thermal  432 m/s, ratio  11.6 -> air     (expected air)
  Titan    escape   2641 m/s, thermal  289 m/s, ratio   9.1 -> air     (expected air)
  Mercury  escape   4250 m/s, thermal  626 m/s, ratio   6.8 -> none    (expected none)
  Ceres    escape    517 m/s, thermal  457 m/s, ratio   1.1 -> none    (expected none)
```

**Titan is why this is worth doing properly.** It is smaller than the Moon and
has a thicker atmosphere than Earth, because it is cold. Any rule that looks
only at size gets it backwards, and a stored boolean would simply have to be
told.

### The threshold was wrong and the test said so

Six is the textbook figure and it is what this shipped with for about ten
minutes. **Mercury failed at 6.8**, which is exactly what the six real cases are
in the test for. They sort with a clean gap:

```
  Earth   23.5        Mercury  6.8
  Mars    11.6        Moon     4.9
  Titan    9.1        Ceres    1.1
```

Nothing between 6.8 and 9.1 gets any of them wrong, and eight sits in the middle
of it. The number came out of the cases rather than being chosen and then
defended.

## No air, no water

The first moon came back with **snow-capped peaks under a black sky**. The sky
was right; the snow was not.

The climate field marches moisture upwind from an ocean, and it was still doing
that on a body with neither ocean nor wind. It now returns zero moisture
immediately when `bHasAtmosphere` is false — before the march rather than after
it, because marching forty steps to arrive at zero is forty steps wasted on
every climate sample of an airless world. Snow, vegetation and the wet end of
the biome set all follow from moisture, so all of them go at once.

Sea level goes with it: an airless body's is set below zero, which is to say the
whole surface is land.

Patch-disk `FormatVersion` is 10 and `bHasAtmosphere` is in the content key,
because it changes the vertex colours and the scatter on every patch.

## A fixture that was quietly lying

`-daysweep -body=2` produced correct pictures of the moon under a report quoting
**the planet's** rotation period and axial tilt: the fixture had `constexpr
int32 DaySweepBody = 1`. It now asks the world which body it built. The moon's
day is 724.67 hours and its tilt is 0.0°, which is what a tidally locked moon
should read and what the report now says.

## What is still the planet's

The settlement is spawned on the moon too, which is a base rather than a town
and is not obviously wrong, but nothing decided it. And the surface reads as
pale rock on a tan plain — that is the shared material doing its height blend,
not snow, since moisture is zero by construction. Whether an airless body should
have its own surface composition is T053's question, and T053 is waiting on a
texture-array decision.
