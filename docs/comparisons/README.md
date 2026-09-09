# Before and after, kept rather than described

`noise-before/` is the terrain as it stood until T046: the surface was
runtime-generated noise, tinted by a per-vertex biome colour. These were the
project's reference captures at commit 8867a14, which is why they cost nothing
to keep — they already existed and were already the thing every regression
check compared against.

The current reference captures in `docs/reference-captures/` are the same
flight over the same seed with two photogrammetric scans height-blended by
slope, and with Lumen for global illumination.

Kept as files because "it looks better" is not a measurement and a description
of an image is not an image. `tools/compare_captures.py` will quantify the
difference between any two of these; the numbers for this change were a
structural residual of 24 on the surface and town frames and 110 on the coast,
against a regression threshold of 2.5.
