# -*- coding: utf-8 -*-
"""Writes the import plan Unreal's own commandlet reads.

**Why not write an import module.** Unreal ships `-run=ImportAssets`, which
takes a JSON file of import groups and hands each group's settings to the
factory that will do the work. `UTextureFactory` implements the settings
parser, so compression and colour space can be set per group without a line of
C++. Writing an editor module to do this would be reimplementing the texture
pipeline — badly, and without streaming, mips or GPU compression.

**What the settings are actually for.** The one thing that has to be right is
the compression setting, because it decides colour space:

    albedo   TC_Default    BC1/BC3, sRGB      the only map that is colour
    normal   TC_Normalmap  BC5, linear        two channels at full precision
    packed   TC_Masks      linear, no sRGB    three unrelated masks

Get the normal map's wrong and the lighting is inside out; get the packed map's
wrong and roughness is gamma-curved, so the ground is glossy in shadow and
matte in sun. Both are afternoons. Neither is a judgement call — it follows
from the role, which is why it is generated rather than clicked.

    python tools/make_import_settings.py
    UnrealEditor-Cmd <project> -run=ImportAssets -importsettings=<the file>
"""

import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SURFACES = os.path.join(ROOT, "surfaces")
MANIFEST = os.path.join(SURFACES, "manifest.json")
PLAN = os.path.join(ROOT, "build", "surface-import.json")

# Role -> the compression setting that also decides its colour space.
COMPRESSION = {
    "albedo": "TC_Default",
    "normal": "TC_Normalmap",
    "packed": "TC_Masks",
}


def main():
    if not os.path.isfile(MANIFEST):
        sys.stderr.write("no manifest. Run tools/import_surfaces.py first.\n")
        return 1

    with io.open(MANIFEST, encoding="utf-8") as handle:
        manifest = json.load(handle)

    groups = []
    for entry in manifest.get("sets", []):
        name = entry["name"]

        # One file per role, not one per map slot: the packed texture fills
        # three slots and must be imported once.
        by_file = {}
        for role, relative in (entry.get("maps") or {}).items():
            by_file.setdefault(relative, role)

        for relative, role in sorted(by_file.items()):
            kind = role if role in COMPRESSION else "packed"
            source = os.path.join(SURFACES, relative.replace("/", os.sep))
            if not os.path.isfile(source):
                continue
            groups.append({
                "GroupName": "%s_%s" % (name, kind),
                "Filenames": [source.replace("\\", "/")],
                "DestinationPath": "/Game/Surfaces/%s" % name,
                "bReplaceExisting": True,
                "bSkipReadOnly": False,
                "FactoryName": "TextureFactory",
                "ImportSettings": {
                    "CompressionSettings": COMPRESSION[kind],
                    # Terrain ground is sampled at every distance from boots to
                    # orbit, so it needs the whole mip chain. Without it the
                    # near detail aliases into shimmer the moment it is small.
                    "MipGenSettings": "TMGS_FromTextureGroup",
                    "LODGroup": "TEXTUREGROUP_World",
                },
            })

    os.makedirs(os.path.dirname(PLAN), exist_ok=True)
    with io.open(PLAN, "w", encoding="utf-8", newline="\n") as handle:
        json.dump({"ImportGroups": groups}, handle, indent=2)
        handle.write("\n")

    print("%d import groups -> %s" % (len(groups), PLAN))
    for kind in sorted(COMPRESSION):
        count = sum(1 for g in groups if g["GroupName"].endswith("_" + kind))
        print("  %-8s %2d  %s" % (kind, count, COMPRESSION[kind]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
