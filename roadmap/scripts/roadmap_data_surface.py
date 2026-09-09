# -*- coding: utf-8 -*-
"""M2S: close-range surface fidelity.

Added 2026-09-09, after standing on the ground and finding it indefensible.

The brief was "on par with Star Citizen", and it is worth writing down exactly
what that does and does not mean, because a milestone measured against a vibe
cannot be passed or failed.

**What is already equal.** The source data. The surface library is 23 Megascan
sets at 4096x4096 with albedo, normal, roughness, AO and height, tiling at 0.5-2
metres -- around 2,000 texels per metre. That is the same class of photogrammetry
Star Citizen's ground is built from, and it is already imported, licence-audited
and wired into a triplanar material that blends by height rather than by alpha.
Nothing in this milestone is about buying better textures. There are no better
textures.

**What is not.** What the renderer does with them, and what the geometry does
underneath them:

  * The ground has a hard geometric floor at 4.77 m. MaxDepth 15, 64 quads a
    patch, 6,371 km planet. Standing 20 m from a quad at 1920 px and 90 degrees
    horizontal, one triangle edge is 229 pixels wide.
  * The height field has a floor too, around 33 m -- the finest band is sampled
    at 150,000 on the unit sphere with four octaves, worth 9-36 m of amplitude
    against a 9 km ceiling. Within 30 m of the camera the ground is a plane, so
    subdividing further would only interpolate flatness more finely.
  * Every scan's height map is sampled and used only as a blending bid. Nothing
    is displaced, parallaxed or occluded. Relief at centimetre scale is carried
    entirely by the normal map, which is why lit gravel reads as a photograph of
    gravel printed on glass.
  * A 2 m tile across a 305 m patch repeats 152 times in each direction with no
    rotation, offset or stochastic variation.
  * There is one tiling scale. Close up it is right; at 200 m it aliases into
    an average colour.

Each of those is a bounded piece of work with a measurable result, which is what
the tasks below are. Together they are the difference between correct materials
and convincing ground.

**What this milestone is not.** It is not Star Citizen's whole planet pipeline
-- their object containers, their atmospheric scattering, their years of art
direction. It is the surface under the player's feet and out to the horizon, and
it is measured against photographs rather than against a competitor's marketing.

**Why here and not in M11.** M11 (Rendering fidelity, week 95) owns the master
material framework, Lumen, shadows and the post chain -- the whole-game
rendering standard. This is upstream of that and specific to terrain: it is the
difference between the terrain milestone having produced something worth
looking at or not, and M02's own gate ("no popping that reads as a seam") is
silent about whether the ground is convincing at walking distance. Deferring it
to week 95 means seventy weeks of building on top of ground nobody wants to
stand on.
"""

M2S = [
    dict(title="Near-field height detail and a deeper LOD floor",
         detail="The two floors have to move together: MaxDepth 15 to 18 takes the finest "
                "quad from 4.77 m to 0.60 m, and a new fine band in the height function "
                "(30 m down to about 0.5 m, under 2 m of amplitude) gives the new "
                "triangles something to describe. Either alone is wasted -- more "
                "triangles interpolating a plane, or detail the mesh cannot resolve.\n\n"
                "The design cost is real and belongs in the write-up: the band must be "
                "limited to what a patch's own vertex spacing can carry, or coarse "
                "patches pay for detail that aliases. The field stops being one function "
                "and becomes one function per spacing. That is defensible -- it is what "
                "mip-mapping does, the morph targets already exist to hide the "
                "disagreement between depths, and SampleTerrain reads the drawn mesh "
                "rather than the field, so collision and queries stay consistent by "
                "construction -- but it is a change to an invariant and must be argued "
                "rather than slipped in.",
         acceptance="Standing on flat ground, no terrain triangle edge exceeds 40 px at "
                    "1920x1080; a 50 m profile of the drawn mesh has RMS deviation from "
                    "a fitted plane above 15 cm; and the transect frame time does not "
                    "regress by more than 15 per cent.",
         days=3, refs=["SS6.8"]),

    dict(title="Parallax occlusion mapping on close ground",
         detail="Every scan ships a height map, the material samples all of them, and "
                "they are used only to decide which layer wins. Nothing is displaced. "
                "This is the single largest gap between the ground as rendered and the "
                "ground as photographed: a normal map relights a flat surface but never "
                "occludes, so pebbles have no silhouette against each other and the "
                "surface slides under a moving camera instead of parallaxing.\n\n"
                "POM in the material, faded out with distance (the cost is per-pixel ray "
                "marching and it is invisible past a few metres), with silhouette "
                "clipping left off -- it costs more than it returns on ground seen from "
                "above.",
         acceptance="A raking-light capture at 2 m shows gravel occluding gravel, and "
                    "the occlusion moves correctly as the camera translates; measured by "
                    "differencing two captures a metre apart, where a normal-mapped "
                    "surface produces no parallax and a displaced one does.",
         days=3, refs=["SS6.8"]),

    dict(title="Break the tiling repetition",
         detail="A 2 m tile over a 305 m patch is 152 repeats each way, unrotated and "
                "unoffset. The eye finds that grid immediately, and it is the most "
                "recognisable tell of procedural ground.\n\n"
                "Stochastic sampling -- three hex-grid taps per texture, each with a "
                "noise-driven offset and rotation, blended by barycentric weight. Three "
                "times the samples on the ground layers, which is why it needs measuring "
                "rather than assuming.",
         acceptance="An overhead 200 m capture has no visible grid, and the "
                    "autocorrelation of its albedo shows no peak at the tiling period "
                    "above the noise floor. The before capture shows that peak.",
         days=3, refs=["SS6.8"]),

    dict(title="Multi-scale detail, blended by distance",
         detail="One tiling scale cannot serve one metre and one kilometre. Close up the "
                "2 m tile is right; at 200 m it averages to a flat colour, and past that "
                "it aliases. Three scales: a macro variation layer at 20-50 m that "
                "breaks up large-scale flatness, the scan's own tile, and a detail "
                "normal at around 0.25 m that survives being stood on -- crossfaded by "
                "distance so exactly one pair is ever active.",
         acceptance="Texel density measured at 1 m, 10 m, 100 m and 1 km stays inside "
                    "one octave of the target; captures at those four distances show "
                    "neither smearing up close nor a flat average far away.",
         days=3, refs=["SS6.8"]),

    dict(title="Audit what the shading model actually receives",
         detail="Before adding anything further: confirm every channel the scans provide "
                "reaches the output and means what the shading model thinks it means. "
                "AO reaching base colour rather than the AO input, a normal map sampled "
                "as sRGB, roughness that was authored as gloss -- each is a one-line bug "
                "that costs the whole surface its realism, and none of them announce "
                "themselves.\n\n"
                "Includes specular occlusion from AO, which is free once AO is wired and "
                "is most of why crevices read as crevices.",
         acceptance="A checklist capture per channel: each of albedo, normal, roughness, "
                    "AO and height isolated in a debug view and shown to be the map the "
                    "manifest names, in the colour space the manifest names.",
         days=2, refs=["SS6.8"]),

    dict(title="Rocks with silhouettes, generated by rule",
         detail="Ground detail that a heightfield cannot hold: boulders, cobbles and "
                "slabs as real geometry, placed by the same slope and biome rules the "
                "material blends by, so a gravel surface has gravel standing proud of it "
                "and a scree slope has stones with edges. ADR-0005 applies -- these are "
                "generated (noise-deformed convex hulls, eroded), not bought.\n\n"
                "This is also where the cone trees start being replaced, but rocks "
                "first: they are generatable to a convincing standard by rule, which "
                "vegetation is not, and they are what makes ground read as ground.",
         acceptance="A capture at 3 m shows stones whose silhouettes break the horizon "
                    "line of the surface behind them, at a density that matches the "
                    "material underneath; and the transect frame time holds.",
         days=4, refs=["ADR-0005", "SS6.8"]),

    dict(title="Reference comparison against photographs",
         detail="The measuring stick, and the reason this milestone can be failed. "
                "Freely licensed photographs of real ground -- arid gravel, scree, "
                "forest floor, snow -- put beside the render at matched focal length, "
                "exposure and sun angle, at 2 m, 20 m and 200 m. The existing capture "
                "comparison harness does the framing; what is new is the reference "
                "plates and the honest verdict.\n\n"
                "The write-up names where the render still loses. A comparison page that "
                "concludes everything is fine is a comparison page nobody ran.",
         acceptance="Twelve side-by-side pairs published, each with a written verdict, "
                    "and at least three naming a specific remaining deficiency.",
         days=3, refs=["QB", "SS6.8"]),

    dict(title="The cost of all of it, measured",
         detail="Every task above adds per-pixel or per-vertex work to the thing that "
                "already dominates the frame. Measured together, on the transect, "
                "against the budget -- and if it does not fit, the write-up says which "
                "of these is being turned down and to what.",
         acceptance="A frame time breakdown before and after the whole milestone at 300 "
                    "m and at 900 m/s, with the terrain's share attributed to geometry, "
                    "material and scatter separately.",
         days=2, refs=["SS6.8"]),

    dict(title="Playable proof: stand still and look at the ground",
         detail="Rule 6 applied to a surface. Walk to four different biomes, stop, and "
                "look down, then out to the horizon.",
         acceptance="Four captures at eye height in four biomes, each of which a person "
                    "shown it without context does not identify as a video game "
                    "heightfield -- and the four look like four different places.",
         days=2, refs=["QB", "SS6.8"]),
]
