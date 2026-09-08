# -*- coding: utf-8 -*-
"""Every surface texture must be accounted for in the manifest.

**Why a validator and not a spreadsheet.** A project that audits its asset
licences at the end is a project that finds out, at the end, that it cannot
ship. The only version of this that works is one where an unaccounted texture
fails the build on the commit that added it, while the person who added it is
still in the room and still remembers where it came from.

The manifest is also the pipeline's input: it says what each set contains and
what the maps mean, so the importer does not have to guess from filenames
whether `_n` is a normal map or a noise map.

    python tools/check_surfaces.py           validate
    python tools/check_surfaces.py --stub    write an example entry
"""

import argparse
import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACES = os.path.join(ROOT, "client", "Content", "Surfaces")
MANIFEST = os.path.join(SURFACES, "manifest.json")

TEXTURE_SUFFIXES = (".png", ".tga", ".exr", ".jpg", ".jpeg", ".tif", ".tiff")

# What a set has to declare. `licence` is not optional and has no default: the
# whole point is that somebody had to type it.
REQUIRED = ("name", "source", "licence", "maps")

# Map roles the importer knows how to set up. Anything else is a typo, and a
# typo here is a normal map imported as sRGB, which looks like the lighting is
# inside out and takes an afternoon to find.
KNOWN_MAPS = {
    "albedo": "sRGB colour",
    "normal": "tangent-space normal, linear, no sRGB",
    "roughness": "linear, packed to green by the importer",
    "ao": "linear, packed to red",
    "height": "linear, packed to blue, used for height blending",
    "displacement": "linear, optional",
}


def load_manifest():
    if not os.path.isfile(MANIFEST):
        return None
    with io.open(MANIFEST, encoding="utf-8") as handle:
        return json.load(handle)


def find_textures():
    found = []
    if not os.path.isdir(SURFACES):
        return found
    for base, _, names in os.walk(SURFACES):
        for name in names:
            if name.lower().endswith(TEXTURE_SUFFIXES):
                path = os.path.join(base, name)
                found.append(os.path.relpath(path, SURFACES).replace("\\", "/"))
    return sorted(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stub", action="store_true",
                        help="write an example manifest entry and exit")
    arguments = parser.parse_args()

    if arguments.stub:
        os.makedirs(SURFACES, exist_ok=True)
        example = {
            "note": "Every set here is licensed for use in this project. See "
                    "docs/naming.md for what the project is called (it is not "
                    "decided) and tools/check_surfaces.py for why this file is "
                    "enforced rather than maintained.",
            "sets": [
                {
                    "name": "granite_rough",
                    "source": "Quixel Megascans via Fab, asset id TBD",
                    "licence": "Megascans licence, free for Unreal Engine use",
                    "biomes": ["mountain", "scree"],
                    "tiling_metres": 4.0,
                    "maps": {
                        "albedo": "granite_rough/albedo.png",
                        "normal": "granite_rough/normal.png",
                        "roughness": "granite_rough/roughness.png",
                        "ao": "granite_rough/ao.png",
                        "height": "granite_rough/height.png",
                    },
                }
            ],
        }
        with io.open(MANIFEST, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(example, handle, indent=2)
            handle.write("\n")
        print("wrote an example manifest to %s" % MANIFEST)
        return 0

    textures = find_textures()
    manifest = load_manifest()

    if manifest is None:
        if not textures:
            print("no surface textures yet, and no manifest. Nothing to check.")
            return 0
        sys.stderr.write("%d textures present and no manifest.\n" % len(textures))
        sys.stderr.write("Run: python tools/check_surfaces.py --stub\n")
        return 1

    failures = []
    claimed = set()

    for index, entry in enumerate(manifest.get("sets", [])):
        label = entry.get("name", "set %d" % index)
        for field in REQUIRED:
            if not entry.get(field):
                failures.append("%s: no %s. It is not optional — somebody has to "
                                "type where this came from." % (label, field))

        for role, path in (entry.get("maps") or {}).items():
            if role not in KNOWN_MAPS:
                failures.append("%s: unknown map role %r. Known roles: %s"
                                % (label, role, ", ".join(sorted(KNOWN_MAPS))))
            claimed.add(path)
            if not os.path.isfile(os.path.join(SURFACES, path)):
                failures.append("%s: %s is in the manifest and not on disk."
                                % (label, path))

    for path in textures:
        if path not in claimed:
            failures.append(
                "%s is on disk and in no manifest entry.\n"
                "    Where did it come from, and under what licence? Answer in "
                "manifest.json." % path)

    if failures:
        sys.stderr.write("\nSurface manifest:\n\n")
        for failure in failures:
            sys.stderr.write("  %s\n\n" % failure)
        return 1

    sets = manifest.get("sets", [])
    print("surfaces: %d sets, %d textures, all accounted for" % (len(sets), len(textures)))
    for entry in sets:
        print("  %-20s %s" % (entry["name"], entry["licence"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
