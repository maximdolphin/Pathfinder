# Cloud altitude is a thermometer reading

T094. **A cumulus base is flat across a whole landscape because every parcel
starts at the same temperature and cools at the same rate.** It is not an art
choice and it is not a constant: it is the height at which a rising parcel
reaches its dew point, and it moves when the weather does.

All three decks are computed from the air. One of the three renders.

## Where they sit

```
a dry parcel cools at 9.76 K/km and the atmosphere at 6.49
cumulus 1230 to 3853 m, middle 3853 to 6164, cirrus 8476 to 10941,
tropopause 10941
```

Earth's own numbers through the same function, which is the check worth passing:
fair-weather cumulus at 1.2 km, cirrus at 8.5, the tropopause at 11.

Each boundary is a temperature converted to a height:

| deck | temperature | why |
|---|---|---|
| cumulus base | dew point | the lifting condensation level |
| cumulus top | −10 °C | where a water cloud glaciates |
| middle | −10 to −25 °C | the mixed-phase range |
| cirrus base | −40 °C | where water freezes with nothing to freeze onto |
| tropopause | 217 K | where the temperature stops falling |

## Two lapse rates, and using one for both is the mistake

**A dry parcel cools at `g/cp` — 9.76 K/km — and that is what sets a cumulus
base**, because the parcel rising off the ground has not condensed anything yet.
**The atmosphere as a whole cools at about 6.5**, because condensation releases
heat into the column on the way up.

```
cirrus would sit at 5636 m if the environment cooled at the dry rate,
against 8476 as it is
```

Three kilometres, from one confusion. It is the sort of error that looks like a
style choice about how high the sky is.

On a world with nothing to condense there is nothing to release, and the
environment really does follow the dry rate — so the two are the same number
there, for a reason rather than by a special case.

## Broken cloud, and the line T088 needed

Coverage is a query on the weather field, not a setting:

```
longitude   pressure     cumulus   middle
     -180      85026 Pa    0.61     0.50
      -90      86396 Pa    0.41     0.09
       +0      85494 Pa    0.54     0.36
      +60      86396 Pa    0.40     0.09
```

A deep low is overcast and a ridge is not, and the sky is between 40% and 61%
covered along one line of latitude rather than uniformly solid.

**That solid deck is what stopped a moon being photographed in T088.** The
component used to run at whatever coverage the material shipped with, over a
fixed 2–8 km layer, on every world with air. It now sits at the lifting
condensation level and covers as much of the sky as the pressure overhead says.

## The landing site is above the clouds

```
clouds: layer 1237 to 8538 m; cumulus 1237-1732 at 38%,
middle 1732-3733 at 4%, cirrus 6058-8538 at 43%
```

The home world is colder than Earth — 272 K rather than 288 — so its lifting
condensation level is at 1237 m and its cumulus deck is 500 m thick rather than
2600. **And T072 moved the landing site to a 2.4 km peak.** The site is
therefore a kilometre above the cloud tops, which is why a camera standing on it
photographs a clear blue sky with a hazy sheet along the horizon.

That is not a bug in either the clouds or the site. It is a mountain above the
weather, and it is what the two models jointly say. It does mean the ground
camera at the town is a poor instrument for judging a cloud deck, and a climb
capture has to start below one.

## What renders, and what does not

The deck the renderer draws is now in the right place with the right coverage,
and it moves when the clock does:

```
clouds: cumulus 1237 to 1732 m, 38% cover; middle 1732 to 3733 m at 4%;
cirrus 6058 to 8538 m at 43%; tropopause 8538 m
```

(The home world is colder than Earth — 272 K rather than 288 — so its decks are
lower and thinner. Nobody chose that either.)

**But only the cumulus deck is drawn.** Unreal renders one volumetric cloud per
scene: `UVolumetricCloudComponent` is a single layer, and adding a second
component does not add a second deck. Three decks in one component means one
*material* with three density bands against the sample's own altitude.

That material now exists — `BuildCloudMaterial`, in the same procedural style as
the terrain, water and flat materials this project already builds. **Four faults
have been found in it, three of them fixed, and it now draws.** What it does not
yet draw is cloud *shapes*:

- **The altitude output was the wrong one.** The node offers `Altitude`,
  `AltitudeInLayer`, `NormAltitudeInLayer` and `ShadowSampleDistance` in that
  order; only the third is the 0-to-1 coordinate the bands are drawn in. Picking
  index 1 by its name put every band in the bottom millimetre of the layer. The
  fix came from logging the node's outputs rather than guessing at them.
- **Turbulent noise is biased towards zero**, so a coverage threshold at
  `1 - 0.38` passed almost none of it. Plain gradient noise is centred on a half,
  which is what makes a coverage number mean coverage.
- **It was not declared usable on a volumetric cloud.** Unreal validates
  material usage per consumer and silently substitutes the default for anything
  that has not declared itself — the same trap that rendered three hundred and
  forty-two trees as black cut-outs in M02, presenting here as a sky with no
  cloud in it. `bUsedWithVolumetricCloud`.
- **Volume materials must use an additive blend mode**, and one that does not
  fails to compile outright. The engine had been saying so in `LogMaterial` the
  whole time, under a warning about the *map* rather than the material — which
  is why two rounds went on explaining an empty sky as a density that came out
  zero. Reading the log beat four rounds of reasoning about the graph.
- **And the noise had no precision left.** This planet's surface is 6.3e8 cm
  from the origin, where a shader float's ULP is about sixty centimetres, so a
  noise coordinate built from absolute world position cannot vary smoothly
  across a cloud — it came back as one value with a dither on it. Driving the
  density from the raw noise proved it: the whole sky was flat. The coordinate
  is now camera-relative, with an origin parameter carrying the camera's own
  position modulo a hundred kilometres, computed on the client where doubles
  still exist. **This is the large-world precision problem the terrain solved
  with doubles, arriving somewhere doubles are not available.**

With those three fixed the noise varies and the layer draws. What is left is
that the decks read as a dense fog gradient rather than as cloud with sky
between: the coverage threshold is not cutting holes. That is the remaining
fault and it is a shaping problem rather than a plumbing one.

So it still sits behind `-threedecks`, and the default is the engine's own cloud
material driven at the altitude and coverage this project computes. One deck of
three, in the right place, at the right density. Left on by default a material
that draws nothing is a sky with the clouds computed, placed and invisible —
which is worse than one deck drawn correctly, and exactly the sort of thing that
gets mistaken for working.

**T094 is blocked on its render.** Marking it done on a single deck when the
acceptance says "climb through three distinct cloud decks" would be marking it
done on a third.

## And T088 is still blocked, for a different reason now

The passage frames are no longer behind a solid overcast — the moon transits at
79° through 43% cirrus. They are black because it is *night* and nothing renders
a night sky at this exposure yet. That is the star-field and exposure work, not
the cloud work, and T088's blocker has moved from T089/T094 to it.

## The density never reached the renderer

The "shaping problem" was not one. A volume-domain material's density is its
**Extinction** pin, and Extinction is the `SubsurfaceColor` property under
another name — `MaterialAttributeDefinitionMap.cpp` returns "Extinction" for
`MP_SubsurfaceColor` when the domain is `MD_Volume`. **Opacity is not read at
all.** This material wired its noise, its three bands and every coverage
threshold into Opacity, and left Extinction at the pin's default of one per
metre: seven thousand optical depths across a 7.3 km layer, the same
everywhere. Every uniform sky this task produced was that constant, and so was
every change that "changed no pixel" — the coverage ramp, the flattened noise,
the factor-of-a-hundred density change. None of them was connected to anything.

Two things gave it away once the probe was made thin enough to see through:

- At 3e-6 "per centimetre" a full column should be about two optical depths,
  so the ground ought to show through from orbit. It came back an opaque tan
  shell, which a density of that size cannot produce.
- From orbit there had been no cloud at all, because a ray only starts marching
  if the layer is within `TracingStartMaxDistance` (350 km by default) and the
  orbit frame is 12,700 km out. Raising that to 30,000 km turned an empty frame
  into the same uniform shell — the layer was there, at the default extinction,
  whatever the graph said.

The units were wrong too: volumetric cloud extinction is per metre, not per
centimetre. The fix wires `SubsurfaceColor`, with 0.04 per metre for the decks
(a 500 m cumulus deck is twenty optical depths, opaque as a cloud is) and 3e-4
per metre for the probe (about two optical depths a column).

## And the noise never moved

With the density on the right pin, the probe and the real decks both came back
an empty blue sky — two different graphs whose only shared input was the
noise. The noise was fed `WPT_CameraRelativeNoOffsets`, and the cloud pass never
fills that field: `UpdateMaterialCloudParam` in
`VolumetricCloudMaterialPixelCommon.ush` sets `WorldPosition_CamRelative` and
leaves `WorldPosition_NoOffsets_CamRelative` as a literal `// TODO`. So every
sample in every column read the same coordinate. It now uses
`WPT_CameraRelative`, which the pass does set. The earlier note that "driving the
density from the raw noise proved" a precision problem was reading a pin the
renderer ignored; the precision reasoning for the anchor still holds, but that
run proved nothing.

## And it did not compile

`WPT_CameraRelative` changed nothing either: probe and decks still an empty
sky, while a constant thin extinction (`-cloudconst`) drew a uniform haze and a
uniform shell from orbit — so the pin reached the integrator, and whatever
used the noise did not. The log had the answer, under the map's name rather
than the material's, as before:

    Failed to compile Material for platform PCD3D_SM6, Default Material will be used in game.
    (Node Subtract) Arithmetic between types float3 and float4 are undefined

Vector parameters are four wide and positions three. The anchor, the wind
direction and the up vector are all vector parameters, and while the noise
chain fed the Opacity pin nobody compiled it; the moment it fed Extinction the
material failed and the engine substituted its default, which draws nothing.
`-cloudconst` compiled because a constant does not touch that chain. The three
parameters are now masked to RGB, and the chains print "Failed to compile" when
they see it.
