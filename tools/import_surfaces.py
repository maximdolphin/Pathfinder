# -*- coding: utf-8 -*-
"""Turns a downloaded Megascans texture-set zip into a surface the game can use.

Drop zips into `incoming/` and run this. For each one it:

  1. reads the bundled metadata for the scan's real-world size and name
  2. unpacks the maps it needs and discards the ones it does not
  3. packs roughness, ambient occlusion and height into one RGB texture
  4. writes a manifest entry recording where it came from and under what licence

**Why pack three maps into one.** The terrain material is triplanar: it samples
along three world axes and blends by the surface normal, so every texture is
sampled three times. Roughness, AO and height as separate textures is nine
samples per surface per pixel; packed, it is six — and with two surfaces
height-blending at a biome boundary that difference is twelve samples a pixel,
which is the sort of number that decides whether the ground costs 2 ms or 5.

They pack cleanly because all three are single-channel and none of them wants
sRGB. Base colour and normal stay separate: base colour is the only map that
*is* sRGB, and a normal map needs two full-precision channels of its own.

**Why not let Unreal do it.** Unreal will happily import nine textures and let
the material sample all nine. Doing it here means the decision is in a file that
explains itself, runs in CI, and produces the same result on any machine.

    python tools/import_surfaces.py            import everything in incoming/
    python tools/import_surfaces.py --list     say what is there, change nothing
"""

import argparse
import io
import json
import os
import re
import sys
import zipfile

try:
    from PIL import Image
except ImportError:
    sys.stderr.write(
        "This needs Pillow: python -m pip install Pillow\n"
        "It is a build-time tool, not something the game ships with.\n")
    raise SystemExit(1)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INCOMING = os.path.join(ROOT, "incoming")
SURFACES = os.path.join(ROOT, "client", "Content", "Surfaces")
MANIFEST = os.path.join(SURFACES, "manifest.json")

# What we keep, and what each one becomes.
#
# Displacement is Megascans' height map. It drives height blending — the thing
# that makes gravel interlock with sand at a biome boundary rather than
# cross-fading into it — so it is not optional detail.
WANTED = {
    "basecolor": "albedo",
    "normal": "normal",
    "roughness": "_pack_g",
    "ao": "_pack_r",
    "displacement": "_pack_b",
}

# Maps that come in the zip and are deliberately dropped. Named rather than
# ignored silently, so that "where did the cavity map go" has an answer.
DISCARDED = {
    "bump": "redundant with normal",
    "cavity": "folded into ambient occlusion for our purposes",
    "gloss": "the inverse of roughness; we keep roughness",
    "specular": "dielectric ground; the material uses a constant",
}

LICENCE = "Fab Standard License (free tier). See docs/asset-licensing.md."
SOURCE_PREFIX = "Quixel Megascans via Fab"


def slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def read_metadata(archive):
    """Scan area and display name, from the JSON Megascans bundles in."""
    for entry in archive.namelist():
        if entry.lower().endswith(".json"):
            try:
                data = json.loads(archive.read(entry))
            except ValueError:
                continue
            name = data.get("name")
            metres = None
            for item in data.get("meta") or []:
                if item.get("key") == "scanArea":
                    # "2x2 m" — the tiling distance the material needs, which is
                    # the difference between sand that reads as sand and sand
                    # that reads as a repeating texture.
                    match = re.match(r"\s*([\d.]+)\s*x", str(item.get("value", "")))
                    if match:
                        metres = float(match.group(1))
            asset_id = data.get("id")
            return name, metres, asset_id
    return None, None, None


def classify(filename):
    """Which map a file is, from its name. Longest match first: 'basecolor'
    contains 'color', and a normal map imported as base colour is an afternoon."""
    lowered = os.path.basename(filename).lower()
    for key in sorted(WANTED, key=len, reverse=True):
        if key in lowered:
            return key
    for key in DISCARDED:
        if key in lowered:
            return None
    return None


def import_one(path, manifest):
    archive = zipfile.ZipFile(path)
    name, metres, asset_id = read_metadata(archive)
    if not name:
        name = os.path.basename(path).split("_4k")[0].replace("_", " ").title()

    key = slug(name)
    target = os.path.join(SURFACES, key)
    os.makedirs(target, exist_ok=True)

    found = {}
    for entry in archive.namelist():
        role = classify(entry)
        if role:
            found[role] = entry

    missing = [r for r in ("basecolor", "normal", "roughness") if r not in found]
    if missing:
        return None, "%s: no %s map in the archive" % (name, ", ".join(missing))

    maps = {}

    # Base colour and normal pass through unchanged.
    for role, out in (("basecolor", "albedo"), ("normal", "normal")):
        image = Image.open(io.BytesIO(archive.read(found[role]))).convert("RGB")
        relative = "%s/%s.png" % (key, out)
        image.save(os.path.join(SURFACES, relative), optimize=True)
        maps[out] = relative

    # Roughness, AO and height into one RGB texture.
    channels = {}
    size = None
    for role, channel in (("ao", "R"), ("roughness", "G"), ("displacement", "B")):
        if role in found:
            grey = Image.open(io.BytesIO(archive.read(found[role]))).convert("L")
            size = size or grey.size
            if grey.size != size:
                grey = grey.resize(size, Image.LANCZOS)
            channels[channel] = grey

    # A missing channel gets a neutral value rather than black: no AO map means
    # unoccluded, and no height map means flat. Black would mean fully occluded
    # and a surface that never wins a height blend.
    neutral = {"R": 255, "G": 128, "B": 128}
    for channel in ("R", "G", "B"):
        if channel not in channels:
            channels[channel] = Image.new("L", size, neutral[channel])

    packed = Image.merge("RGB", (channels["R"], channels["G"], channels["B"]))
    relative = "%s/packed_ao_rough_height.png" % key
    packed.save(os.path.join(SURFACES, relative), optimize=True)
    maps["roughness"] = relative
    maps["ao"] = relative
    if "displacement" in found:
        maps["height"] = relative

    entry = {
        "name": key,
        "display": name,
        "source": "%s, asset id %s" % (SOURCE_PREFIX, asset_id or "unknown"),
        "licence": LICENCE,
        "tiling_metres": metres or 2.0,
        "packing": "albedo sRGB; normal linear; packed = AO in red, roughness "
                   "in green, height in blue, all linear",
        "maps": maps,
    }
    return entry, None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true",
                        help="report what is waiting, import nothing")
    arguments = parser.parse_args()

    os.makedirs(INCOMING, exist_ok=True)
    zips = sorted(f for f in os.listdir(INCOMING) if f.lower().endswith(".zip"))

    if not zips:
        print("nothing in incoming/. Drop the Fab texture-set zips there.")
        return 0

    if arguments.list:
        for name in zips:
            size = os.path.getsize(os.path.join(INCOMING, name)) / 1048576.0
            print("  %7.1f MB  %s" % (size, name))
        return 0

    os.makedirs(SURFACES, exist_ok=True)
    manifest = {"sets": []}
    if os.path.isfile(MANIFEST):
        with io.open(MANIFEST, encoding="utf-8") as handle:
            manifest = json.load(handle)
    manifest.setdefault("sets", [])
    manifest["note"] = (
        "Generated by tools/import_surfaces.py. Every entry records where the "
        "asset came from and under what licence; tools/check_surfaces.py fails "
        "the build on any texture without one. See docs/asset-licensing.md.")

    by_name = {entry["name"]: index for index, entry in enumerate(manifest["sets"])}
    problems = []

    for name in zips:
        entry, problem = import_one(os.path.join(INCOMING, name), manifest)
        if problem:
            problems.append(problem)
            continue
        if entry["name"] in by_name:
            manifest["sets"][by_name[entry["name"]]] = entry
        else:
            by_name[entry["name"]] = len(manifest["sets"])
            manifest["sets"].append(entry)
        print("  imported %-28s %.1f m tiling" % (entry["name"], entry["tiling_metres"]))

    manifest["sets"].sort(key=lambda e: e["name"])
    with io.open(MANIFEST, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    for problem in problems:
        sys.stderr.write("  %s\n" % problem)

    print("\n%d surfaces in the manifest" % len(manifest["sets"]))
    print("discarded per set: %s" % ", ".join(
        "%s (%s)" % (k, v) for k, v in sorted(DISCARDED.items())))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
