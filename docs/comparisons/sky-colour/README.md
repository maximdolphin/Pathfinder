# A butterscotch sky, and a sunset that is not blue

T090. The acceptance has two halves. **The first is delivered and the second is
not**, and the second is not a matter of effort — it is a thing the engine's sky
model cannot express, which is worth writing down precisely rather than
approximating.

Run it: `UnrealEditor.exe client/Ledger.uproject -game -airshow -systemseed=20260900 -body=2`

## The colour is predicted before anything renders

Single scattering, in air masses rather than metres:

```
I(channel) = tau_scatter (1 - e^-tau_total) / tau_total
```

with each component integrated over **its own** scale height. That last part is
the whole of the first bug: the first version multiplied every coefficient by
one path length, which quietly assumed the dust and the gas were mixed to the
same height. They are not — haze sits in the bottom kilometre and the gas goes
up eight — and treating them alike gave Earth's haze six times the column it has
and turned the zenith a washed-out pale blue.

```
Earth overhead  R 0.288  G 0.504  B 1.000
Earth low sun   R 0.807  G 0.928  B 1.000
Mars  overhead  R 1.000  G 0.928  B 0.681
Mars  low sun   R 1.000  G 0.926  B 0.675
the same CO2 with the dust taken out: R 0.176  G 0.410  B 1.000
```

**That last line is the point.** Carbon dioxide scatters blue *harder* than air
does — 2.86 times at the same density, because it bends light more and its
molecules are less spherical. A clean carbon-dioxide sky would be a deeper blue
than Earth's. What makes it butterscotch is the dust: about a tenth iron oxide,
which scatters 63% of the blue it intercepts and 94% of the red. The gas is the
red herring and the mineral is the answer.

## Two things the aerosol needed before it worked

**Dust and haze do not live at the same height.** Water haze is condensed out of
a wet lower atmosphere and stays in the bottom kilometre — a seventh of the gas's
scale height. Dust on a dry world is lofted by wind and mixed through the whole
column, which is why Mars's dust scale height is its gas scale height and why a
dust storm there darkens the sky from the top down.

**A thin atmosphere is not a clean one.** Mars carries a dust optical depth of
about half in six millibars while Earth's haze manages a tenth in a thousand,
because what suspends dust is wind and what settles it is air resistance, and a
thin atmosphere is bad at the second. Scaling the aerosol with pressure alone
made the dustiest sky in the solar system the clearest, so a dry world gets a
floor of half an optical depth over its own column.

## What it renders

```
carbon dioxide, 55666 Pa at 394.8 K, scale height 8.24 km, top 98.9 km,
Rayleigh 0.0289/0.0119/0.0051 per km,
dust scatter 0.03822/0.05278/0.05702 absorb 0.02245/0.00789/0.00364, g=0.75
```

A butterscotch sky over dark hills, from a body found by asking the generator
which of forty systems had a carbon-dioxide world (26 of 40 did). Nothing in the
fixture is per-body; it reads the profile and points the camera.

Alongside it, the home world's sunset — mountains in silhouette, orange sky, a
cloud deck lit from below — from the same code with different numbers in it.

## The blue sunset, and why it is not here

Mars's sunsets are blue within about ten degrees of the sun. This does not
reproduce it, and the reason is specific.

The blue aureole is a **diffraction** effect. Martian dust grains are a micron
or two across, so the forward lobe of their phase function is narrow, and its
width goes as wavelength over particle size — which means the blue lobe is
*narrower and brighter* than the red one. Close to the sun you are looking down
the blue lobe; a few degrees away you are outside it and only the absorption is
left, so the sky goes back to butterscotch.

That needs a **per-channel phase function**. Unreal's Sky Atmosphere has one
`MieAnisotropy` for all three channels: the same Henyey-Greenstein lobe multiplies
whatever colour is in `MieScattering`, and for dust that colour is red-dominant,
so the aureole comes out golden. The single-scattering estimate above says the
same thing — Mars's colour barely moves between one air mass and thirty-eight,
because at that optical depth the sky tends to the aerosol's albedo ratio and
stops caring about path length.

Both halves of the model agree, and they agree that a per-channel lobe is the
missing piece. It is not expressible in the component; it needs either a custom
sky shader or a separate aureole element drawn around the sun disc. **T090 is
therefore blocked on its second half**, with the mechanism named, rather than
marked done on the strength of a sky that is the right colour for the wrong
half of the sky.

## What is delivered

- Rayleigh coefficients from refractivity and number density, per composition.
- Aerosol scattering and absorption split per channel from a measured
  single-scattering albedo, with anisotropy per particle type.
- Aerosol scale height per composition, and an optical-depth floor for dry worlds.
- A sky-colour predictor that answers before the renderer does, so a render that
  disagrees with it is a bug in one of the two rather than a matter of opinion.
- A butterscotch sky and a golden dusty sunset on a carbon-dioxide world, and a
  blue sky and an orange sunset on an oxygen one, from the same code.
