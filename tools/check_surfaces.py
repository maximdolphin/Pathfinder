# -*- coding: utf-8 -*-
"""Every surface texture must be accounted for, and must be what it claims.

**Why a validator and not a spreadsheet.** A project that audits its asset
licences at the end is a project that finds out, at the end, that it cannot
ship. The only version of this that works is one where an unaccounted texture
fails the build on the commit that added it, while the person who added it is
still in the room and still remembers where it came from.

The manifest is also the pipeline's input: it says what each set contains and
what the maps mean, so the importer does not have to guess from filenames
whether `_n` is a normal map or a noise map.

**And the images have to match the roles they claim.** Colour space in this
pipeline is not a property of the file, it is a property of the *role*: albedo
is sRGB, everything else is linear, and which it gets is decided by the slot a
texture sits in. So colour space does not go wrong by way of a wrong flag, it
goes wrong by way of the wrong image in the slot — a normal map in the albedo
slot, or a colour map in the normal slot, which renders as lighting that is
inside out and takes an afternoon to find.

That is checkable without opening anything. A tangent-space normal map points
mostly straight out of the surface, so its channel means sit near (128, 128,
240), and nothing else in a surface set looks remotely like that. So this reads
a strip of every texture and says, naming the file, when its contents do not
match its role.

    python tools/check_surfaces.py           validate
    python tools/check_surfaces.py --stub    write an example entry
"""

import argparse
import io
import json
import os
import struct
import sys

# Failures are printed to stderr, which on a Windows console is not UTF-8 by
# default, and a check whose message is unreadable is a check nobody acts on.
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_captures import read_png

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACES = os.path.join(ROOT, "surfaces")
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

# What a normal map looks like in bulk. Ground scans are gentle; even a rocky
# one stays well inside this, and the check only has to separate a normal map
# from an albedo or a packed mask, not grade its steepness.
NORMAL_BLUE_FLOOR = 190.0
NORMAL_NEUTRAL_RANGE = (96.0, 160.0)

# How much of each texture to look at. Undoing PNG's row filters is a byte at a
# time in Python, so a whole 4K texture is fifty million iterations. A tiling
# ground surface is statistically uniform, so a thin strip says what the whole
# image would say: sixteen rows of a 4K texture is still sixty-five thousand
# pixels behind every mean.
SAMPLE_ROWS = 16


def read_header(path):
    """(width, height, bit depth, colour type) from a PNG's IHDR, and nothing
    else read from the file. Hand-rolled on purpose: this runs in CI, and a
    check that needs an install is a check somebody eventually turns off."""
    with io.open(path, "rb") as handle:
        head = handle.read(26)
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", head[16:24])
    return width, height, head[24], head[25]


def channel_means(path):
    """Mean R, G and B over the top strip, or None if it cannot be read."""
    try:
        width, _, rows, channels = read_png(path, max_rows=SAMPLE_ROWS)
    except Exception:
        return None
    if not rows or width == 0:
        return None
    totals = [0, 0, 0]
    for row in rows:
        for x in range(0, width * channels, channels):
            totals[0] += row[x]
            totals[1] += row[x + 1]
            totals[2] += row[x + 2]
    samples = float(len(rows) * width)
    return [total / samples for total in totals]


def looks_like_a_normal_map(means):
    red, green, blue = means
    return (blue >= NORMAL_BLUE_FLOOR
            and NORMAL_NEUTRAL_RANGE[0] <= red <= NORMAL_NEUTRAL_RANGE[1]
            and NORMAL_NEUTRAL_RANGE[0] <= green <= NORMAL_NEUTRAL_RANGE[1])


def check_image(label, role, relative):
    """Everything checkable about one texture, given the role it claims.

    Returns (problems, size). Size is None when the file is not a PNG, which
    the importer never writes; a hand-placed TGA is still licence-checked, this
    just has nothing to say about its contents."""
    path = os.path.join(SURFACES, relative)
    problems = []

    header = read_header(path)
    if header is None:
        return problems, None
    width, height, _depth, colour = header

    if colour not in (2, 6):
        problems.append(
            "%s: %s is greyscale or paletted (PNG colour type %d) and sits in "
            "the %s slot, which needs three channels."
            % (label, relative, colour, role))
        return problems, (width, height)

    # Non-power-of-two textures cannot stream: Unreal will not build a full mip
    # chain for one, so the surface never drops resolution with distance and
    # costs its full bandwidth from orbit.
    for size, side in ((width, "width"), (height, "height")):
        if size & (size - 1):
            problems.append(
                "%s: %s has a %s of %d, which is not a power of two. It will "
                "not mip, so it will not stream." % (label, relative, side, size))

    means = channel_means(path)
    if means is None:
        return problems, (width, height)

    formatted = "(%.0f, %.0f, %.0f)" % tuple(means)
    if role == "normal":
        if not looks_like_a_normal_map(means):
            problems.append(
                "%s: %s sits in the normal slot and does not look like a normal "
                "map — channel means %s, expected blue above %.0f with red and "
                "green near neutral. A colour map here renders as lighting that "
                "is inside out."
                % (label, relative, formatted, NORMAL_BLUE_FLOOR))
    elif looks_like_a_normal_map(means):
        problems.append(
            "%s: %s sits in the %s slot and looks like a normal map — channel "
            "means %s. The %s slot is %s."
            % (label, relative, role, formatted, role, KNOWN_MAPS[role]))

    return problems, (width, height)


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
                    "licence": "Fab Standard License (free tier)",
                    "biomes": ["mountain", "scree"],
                    "tiling_metres": 4.0,
                    "maps": {
                        "albedo": "granite_rough/albedo.png",
                        "normal": "granite_rough/normal.png",
                        "roughness": "granite_rough/packed_ao_rough_height.png",
                        "ao": "granite_rough/packed_ao_rough_height.png",
                        "height": "granite_rough/packed_ao_rough_height.png",
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
    unfetched = []

    for index, entry in enumerate(manifest.get("sets", [])):
        label = entry.get("name", "set %d" % index)
        for field in REQUIRED:
            if not entry.get(field):
                failures.append("%s: no %s. It is not optional — somebody has to "
                                "type where this came from." % (label, field))

        # One texture can fill several slots — the packed map is AO, roughness
        # and height at once — so check each file once, against every role it
        # claims, and size it once.
        # The textures themselves are not in version control — they are 80 MB a
        # set and re-fetchable from the asset id above. So a set with none of
        # its files present is a clone that has not fetched them, which is a
        # normal state and not a licence problem. A set with *some* of its
        # files is a broken set, and that is worth failing over.
        roles = {role: path for role, path in (entry.get("maps") or {}).items()
                 if role in KNOWN_MAPS}
        for role in set(entry.get("maps") or {}) - set(roles):
            failures.append("%s: unknown map role %r. Known roles: %s"
                            % (label, role, ", ".join(sorted(KNOWN_MAPS))))
        claimed.update(roles.values())

        present = {role: path for role, path in roles.items()
                   if os.path.isfile(os.path.join(SURFACES, path))}
        if roles and not present:
            unfetched.append(label)
            continue

        sizes = {}
        checked = set()
        for role, path in sorted(roles.items()):
            if role not in present:
                failures.append("%s: %s is in the manifest and not on disk, but "
                                "the rest of the set is. A half-fetched set will "
                                "render wrong rather than not render."
                                % (label, path))
                continue
            problems, size = check_image(label, role, path)
            failures.extend(problems)
            if size and path not in checked:
                sizes.setdefault(size, []).append(path)
                checked.add(path)

        # Every map in a set has to be the same size. They are sampled with one
        # set of UVs, so a mismatch is not a quality difference, it is normals
        # that no longer line up with the colour they shade.
        if len(sizes) > 1:
            described = "; ".join(
                "%dx%d: %s" % (size[0], size[1], ", ".join(sorted(paths)))
                for size, paths in sorted(sizes.items()))
            failures.append("%s: the maps in this set are different sizes, so "
                            "they will not line up. %s" % (label, described))

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
    if unfetched:
        print("%d sets in the manifest with no files here: %s"
              % (len(unfetched), ", ".join(sorted(unfetched))))
        print("  Textures are not in version control. Re-fetch from the asset "
              "ids in manifest.json and run tools/import_surfaces.py.")
    print("surfaces: %d sets, %d textures, all accounted for"
          % (len(sets), len(textures)))
    for entry in sets:
        print("  %-20s %5.1f m tiling   %s"
              % (entry["name"], entry.get("tiling_metres", 0.0), entry["licence"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
