# Earth becomes one row

T089. The renderer had a scattering coefficient of `0.0331`, a scale height of
`8.0`, an ozone term of `0.001881` and a 60 km atmosphere written into it, each
with a paragraph defending it. Every one of those is exactly right for one
planet and silently wrong for every other. They are now computed.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -airshow -body=<n>`

## Five real atmospheres through one function

The point of making Earth a row is that the other rows have to come out right
too, and there are five atmospheres in this solar system whose numbers are
published:

```
body      scale height        lapse rate         Rayleigh 440 nm
Earth      8428 m (  8500)   0.00976 (0.00976)   2.523e-05 /m
Mars      10856 m ( 11100)   0.00439 (0.00450)   5.962e-07 /m
Venus     15940 m ( 15900)   0.01048 (0.01050)   2.562e-03 /m
Titan     20638 m ( 21000)   0.00130 (0.00130)   1.269e-04 /m
Jupiter   24061 m ( 27000)   0.00225 (0.00200)   9.615e-06 /m
```

Scale height is `kT/mg` and lapse rate is `g/cp`; nothing is fitted. Jupiter is
11% low because its real mean molecular mass is 2.22 and the round 2.30 is used
here, which is the size of error a rounded constant buys.

**Rayleigh scattering comes out of the gas rather than out of a slider:**

```
beta(lambda) = 8 pi^3 (n^2 - 1)^2 F / (3 N lambda^4)
```

Everything about the gas enters through how much it bends light (`n`) and how
anisotropic its molecules are (the King factor `F`); everything about the planet
enters through the number density `N`. The one trap is that refractivity is
quoted at Loschmidt's density and has to be scaled to the density it is being
evaluated at — miss that and the answer is off by whatever ratio the planet's
surface density bears to Earth's, which looks like a taste question about how
blue the sky should be.

```
at one density: air 2.475e-05, carbon dioxide 7.085e-05 (2.86x),
                hydrogen-helium 5.405e-06 (0.22x)
blue scatters 5.70 times as much as red, against (680/440)^4 = 5.70
```

Earth's 550 nm coefficient comes out 12% under the measured 1.17e-5, and the
direction is known: refractivity is treated as constant with wavelength when it
actually rises towards the blue. The *ratio* between channels is exact, being
pure `lambda^-4`, and that is what sets the colour.

## The greenhouse, because Venus is the hard one

Equilibrium temperature is what sunlight alone gives, and it is 255 K on Earth
against a measured 288. The grey-atmosphere result closes it:

```
T_surface = T_equilibrium (1 + 3 tau / 4)^(1/4)
```

with the infrared optical depth `tau` proportional to how much gas there is and
how hard that gas absorbs.

```
Earth  1.000 bar: 255 K of sunlight becomes 288.1 K of ground, against 288
Venus  90.800 bar: 232 K becomes 721.6 K, against 737
Titan  1.450 bar: 82 K becomes 94.0 K, against 94
Mars   0.006 bar: 210 K becomes 210.3 K, against 215
```

**Getting Earth right is easy — one number does it.** Venus is 500 K of
greenhouse from the same two lines, and a model tuned to Earth alone misses it
by a factor of three. That is why Venus is in the table.

## Two things that are admissions, not derivations

**Surface pressure is generated.** It is not a function of mass and radius: Mars
and Titan are within a few per cent of each other in radius and differ by two
hundred times in pressure, because pressure records a history of outgassing and
loss rather than a fact of geometry. So it is drawn from the seed and bounded by
the physics — a body that barely holds its air gets less of it, using T078's
retention margin.

**Free oxygen is a biosignature.** Nothing about a planet's mass or distance
produces an oxygen atmosphere; on Earth it is the output of three billion years
of photosynthesis and would be gone in a few million without it. The home world
has oxygen because the game assumes people breathe there, and no other generated
body gets it — one row in eleven. Its pressure is likewise pinned near a bar for
the same reason: two hundred millibars of anything is a spacesuit world, and this
one has already been decided not to be.

Both are stated in the header rather than buried, because a generated fact and
an assumed one should not look alike.

## Three bodies, from data alone

```
Home      nitrogen-oxygen   96898 Pa  272.1 K   H  8.01 km  top  96.1 km  clouds
Outer 1   nitrogen            907 Pa  156.8 K   H  6.23 km  top  74.7 km  none
Giant 2   hydrogen-helium  101325 Pa  130.2 K   H 43.22 km  top 518.6 km  clouds
```

- **Home** — a blue sky with scattered cumulus at 1.2 to 5.2 km, mountain ranges
  receding into aerial perspective, and from orbit a blue disc with a bright rim
  against black.
- **Outer 1** — a hundred times less gas, so the zenith is nearly black and the
  horizon barely brightens. Same code, same light, ninety-nine per cent less
  scattering.
- **Giant 2** — a 43 km scale height and a 519 km deep atmosphere, which from
  orbit is a soft hazy limb rather than a crisp one. The depth is the difference,
  and the depth is `kT/mg` on a body with light molecules and strong gravity.

Nothing in the fixture is per-body. It reads the profile and points the camera.

## What this fixed on the way past

The M03 gate's photographs were washed white (see
`docs/comparisons/moon-passage/`). Part of that was the atmosphere: a
hardcoded coefficient 30% above the physical one, and a cloud deck at 2 to 8 km
regardless of what the air was doing. With the profile driving it the home
world's sky is blue and the deck is broken cumulus.

The rest is cloud *coverage*, which lives in the cloud material and belongs to
T094. T088 stays blocked until a moon can be seen through it.

## And one bug about geometry, not air

The orbit view first pointed 29° below horizontal and photographed pure black.
From two radii up the planet is not below the horizon — it is nearly underneath
you. The angle from straight down to the edge of the disc is `asin(R/(R+h))`,
19° at that altitude, so the limb sits 70° below horizontal. The camera now works
that out instead of being told.
