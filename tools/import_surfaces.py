# -*- coding: utf-8 -*-
"""Turns downloaded Megascans texture sets into surfaces the game can use.

Point it at a zip, a folder of zips, or the Epic launcher's vault cache. For
each set it:

  1. reads the bundled metadata for the scan's real-world size, name and tags
  2. takes the maps it needs and discards the ones it does not
  3. packs ambient occlusion, roughness and height into one RGB texture
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
the material sample all nine. Doing it here means the decision lives in a file
that explains itself, runs in CI, and produces the same result on any machine.

    python tools/import_surfaces.py                      everything in incoming/
    python tools/import_surfaces.py --list               say what is there
    python tools/import_surfaces.py <path> [<path> ...]  import from elsewhere
    python tools/import_surfaces.py --vault              the Epic vault cache
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

# Where the Epic launcher unpacks Fab downloads. Downloading through the
# launcher rather than the browser gives extracted folders instead of zips,
# which is why this reads both.
VAULT = r"C:\ProgramData\Epic\EpicGamesLauncher\VaultCache\FabLibrary"

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

# Maps that come in the set and are deliberately dropped. Named rather than
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


class Bundle(object):
    """One texture set, from a zip or from a directory.

    The launcher and the browser deliver the same files in different wrappers,
    and everything downstream only wants to list names and read bytes. Two
    small readers here is a great deal less code than two import paths."""

    def __init__(self, label, names, reader):
        self.label = label
        self._names = names
        self._reader = reader

    def namelist(self):
        return self._names

    def read(self, name):
        return self._reader(name)


def bundle_from_zip(path):
    archive = zipfile.ZipFile(path)
    return Bundle(os.path.basename(path), archive.namelist(), archive.read)


def bundle_from_directory(path):
    names = sorted(os.listdir(path))

    def read(name):
        with io.open(os.path.join(path, name), "rb") as handle:
            return handle.read()

    return Bundle(os.path.basename(path), names, read)


def is_a_texture_set(names):
    """A directory is a texture set if it holds a base colour map. Anything
    else in a vault cache — thumbnails, plugin folders, the listings database —
    does not, so this is enough to tell them apart without a whitelist."""
    return any(classify(name) == "basecolor" for name in names)


def discover(source):
    """Every texture set under one path, whatever shape it arrives in."""
    if os.path.isfile(source):
        return [bundle_from_zip(source)] if source.lower().endswith(".zip") else []

    if not os.path.isdir(source):
        sys.stderr.write("  no such path: %s\n" % source)
        return []

    bundles = []
    for base, _, names in os.walk(source):
        for name in sorted(names):
            if name.lower().endswith(".zip"):
                bundles.append(bundle_from_zip(os.path.join(base, name)))
        if is_a_texture_set(names):
            bundles.append(bundle_from_directory(base))
    return bundles


def read_metadata(bundle):
    """Scan area, display name, asset id and tags, from the JSON Megascans
    bundles in beside the maps."""
    for entry in bundle.namelist():
        if not entry.lower().endswith(".json"):
            continue
        try:
            data = json.loads(bundle.read(entry))
        except ValueError:
            continue
        metres = None
        for item in data.get("meta") or []:
            if item.get("key") == "scanArea":
                # "2x2 m" — the tiling distance the material needs, which is
                # the difference between sand that reads as sand and sand that
                # reads as a repeating texture.
                match = re.match(r"\s*([\d.]+)\s*x", str(item.get("value", "")))
                if match:
                    metres = float(match.group(1))
        return data.get("name"), metres, data.get("id"), data.get("tags") or []
    return None, None, None, []


def classify(filename):
    """Which map a file is, from its name.

    Megascans names every file `<Name>_<id>_<resolution>_<MapType>.<ext>`, so
    the map type is the last underscore-separated piece of the stem and nothing
    else. Matching it anywhere in the name instead is what this used to do, and
    it broke on the first asset whose id happened to end in a role name: "ao"
    appears inside `Dry_Sand_xfhsfao_4K_Bump.jpg`, and inside `xfhsfao.json`,
    so the ambient occlusion slot ended up holding the metadata file and Pillow
    was handed JSON to decode. Anchoring to the last piece cannot do that."""
    stem = os.path.splitext(os.path.basename(filename))[0].lower()
    suffix = stem.rsplit("_", 1)[-1]
    if suffix in WANTED:
        return suffix
    return None


def check_classify():
    """The classifier is the one piece of guesswork in here, and it has been
    wrong once. Cheap enough to assert on every run."""
    assert classify("Dry_Sand_xfhsfao_4K_AO.jpg") == "ao"
    assert classify("Dry_Sand_xfhsfao_4K_BaseColor.jpg") == "basecolor"
    assert classify("Dry_Sand_xfhsfao_4K_Normal.jpg") == "normal"
    assert classify("Dry_Sand_xfhsfao_4K_Roughness.jpg") == "roughness"
    assert classify("Dry_Sand_xfhsfao_4K_Displacement.jpg") == "displacement"
    # The three that broke it: an asset id ending in a role name.
    assert classify("Dry_Sand_xfhsfao_4K_Bump.jpg") is None
    assert classify("xfhsfao.json") is None
    assert classify("Dry_Sand_xfhsfao_4K_Cavity.jpg") is None
    # Paths from inside a zip, and the maps we drop.
    assert classify("some/dir/Rippled_Sand_vd3lecfs_4K_Gloss.jpg") is None
    assert classify("thumbnail.jpeg") is None


def import_one(bundle):
    name, metres, asset_id, tags = read_metadata(bundle)
    if not asset_id:
        return None, "%s: no metadata, so no asset id to record it under" % bundle.label
    if not name:
        name = bundle.label

    # Keyed on the asset id, not the name. Megascans reuses display names —
    # three different scans here are all called "Rocky Ground" — so a name is
    # not an identity, and inventing collision suffixes later is worse than
    # carrying the id that was always going to be the real key.
    key = "%s_%s" % (slug(name), asset_id)
    target = os.path.join(SURFACES, key)
    os.makedirs(target, exist_ok=True)

    found = {}
    for entry in bundle.namelist():
        role = classify(entry)
        if role:
            found[role] = entry

    missing = [r for r in ("basecolor", "normal", "roughness") if r not in found]
    if missing:
        return None, "%s: no %s map in the set" % (name, ", ".join(missing))

    # Barely compressed on purpose. These are a build intermediate: they are
    # not in version control and Unreal recompresses everything to a GPU
    # format on import, so squeezing the PNG is minutes of CPU spent on
    # bytes nothing ever reads.
    maps = {}

    # Base colour and normal pass through unchanged.
    for role, out in (("basecolor", "albedo"), ("normal", "normal")):
        image = Image.open(io.BytesIO(bundle.read(found[role]))).convert("RGB")
        relative = "%s/%s.png" % (key, out)
        image.save(os.path.join(SURFACES, relative), compress_level=1)
        maps[out] = relative

    # Ambient occlusion, roughness and height into one RGB texture.
    channels = {}
    size = None
    for role, channel in (("ao", "R"), ("roughness", "G"), ("displacement", "B")):
        if role in found:
            grey = Image.open(io.BytesIO(bundle.read(found[role]))).convert("L")
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
    packed.save(os.path.join(SURFACES, relative), compress_level=1)
    maps["roughness"] = relative
    maps["ao"] = relative
    if "displacement" in found:
        maps["height"] = relative

    entry = {
        "name": key,
        "display": name,
        "source": "%s, asset id %s" % (SOURCE_PREFIX, asset_id),
        "licence": LICENCE,
        "tiling_metres": metres or 2.0,
        # Kept because the biome assignment has to come from somewhere, and
        # "brown, quarry, limestone, desert" is what tells three scans that
        # share the name "Rocky Ground" apart.
        "tags": tags,
        "packing": "albedo sRGB; normal linear; packed = AO in red, roughness "
                   "in green, height in blue, all linear",
        "maps": maps,
    }
    return entry, None


def resolution_of(bundle):
    """Bigger base colour wins. If a set was downloaded at more than one
    quality the vault keeps them all, and importing whichever came first in
    directory order is how a project ends up with 1K ground by accident."""
    for entry in bundle.namelist():
        if classify(entry) == "basecolor":
            try:
                return len(bundle.read(entry))
            except Exception:
                return 0
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", nargs="*",
                        help="zips or folders to import from (default: incoming/)")
    parser.add_argument("--list", action="store_true",
                        help="report what is there, import nothing")
    parser.add_argument("--vault", action="store_true",
                        help="also read the Epic launcher's Fab vault cache")
    arguments = parser.parse_args()
    check_classify()

    sources = list(arguments.sources) or [INCOMING]
    if arguments.vault:
        sources.append(VAULT)

    os.makedirs(INCOMING, exist_ok=True)
    bundles = []
    for source in sources:
        bundles.extend(discover(source))

    if not bundles:
        print("no texture sets found in: %s" % ", ".join(sources))
        print("Drop Fab texture-set zips into incoming/, or pass --vault.")
        return 0

    # One set per asset, at the best resolution present.
    best = {}
    for bundle in bundles:
        _, _, asset_id, _ = read_metadata(bundle)
        if not asset_id:
            continue
        if asset_id not in best or resolution_of(bundle) > resolution_of(best[asset_id]):
            best[asset_id] = bundle
    chosen = [best[key] for key in sorted(best)]

    if arguments.list:
        for bundle in chosen:
            name, metres, asset_id, _ = read_metadata(bundle)
            print("  %-34s %-10s %.1f m   %s"
                  % (name, asset_id, metres or 0.0, bundle.label))
        print("\n%d sets" % len(chosen))
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

    for bundle in chosen:
        # A set that blows up is one bad set, not a lost run. The manifest is
        # written after this loop, so an exception here used to throw away
        # every successful import that came before it.
        try:
            entry, problem = import_one(bundle)
        except Exception as failure:
            problems.append("%s: %s: %s"
                            % (bundle.label, type(failure).__name__, failure))
            continue
        if problem:
            problems.append(problem)
            continue
        if entry["name"] in by_name:
            manifest["sets"][by_name[entry["name"]]] = entry
        else:
            by_name[entry["name"]] = len(manifest["sets"])
            manifest["sets"].append(entry)
        print("  %-40s %.1f m tiling   %s"
              % (entry["name"], entry["tiling_metres"],
                 ", ".join(entry["tags"][:4])))

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
