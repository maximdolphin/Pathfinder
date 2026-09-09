# -*- coding: utf-8 -*-
"""Records, on each imported texture, what produced it.

    UnrealEditor-Cmd <project> -run=pythonscript -script=tools/tag_surfaces.py

The materials and meshes tag themselves as they are baked, because the code that
makes them is ours. The textures are imported by Unreal's own ImportAssets
commandlet, which knows nothing about this project, so their provenance has to
be written afterwards.

It comes from client/Config/surfaces.json rather than being invented here: the
manifest already records the Fab asset id and the licence for every set, and a
second place to type that is a second place for it to be wrong.
"""
import json
import os

import unreal

ROOT = os.path.normpath(os.path.join(unreal.Paths.project_dir(), ".."))
MANIFEST = os.path.join(ROOT, "client", "Config", "surfaces.json")

with open(MANIFEST) as handle:
    manifest = json.load(handle)

by_set = {entry["name"]: entry for entry in manifest.get("sets", [])}

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Surfaces"], force_rescan=True)

tagged = 0
unknown = []

for data in registry.get_assets_by_path("/Game/Surfaces", recursive=True):
    package = str(data.package_name)
    # /Game/Surfaces/<set>/<map>
    parts = package.split("/")
    if len(parts) < 5:
        unknown.append(package)
        continue
    entry = by_set.get(parts[3])
    if entry is None:
        unknown.append(package)
        continue

    asset = data.get_asset()
    unreal.EditorAssetLibrary.set_metadata_tag(
        asset, "Ledger.Generator", "tools/import_surfaces.py + ImportAssets")
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "Ledger.Source", entry["source"])
    unreal.EditorAssetLibrary.set_metadata_tag(asset, "Ledger.Licence", entry["licence"])
    unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False)
    tagged += 1

lines = ["Surface provenance tags.", ""]
lines.append("  %d textures tagged from client/Config/surfaces.json" % tagged)
for package in unknown:
    lines.append("  no manifest entry for %s" % package)
lines.append("")
lines.append("VERDICT: %s" % ("PASS" if tagged and not unknown
                              else "FAIL (%d untagged)" % len(unknown)))

out = os.path.join(ROOT, "out", "tag-surfaces.txt")
with open(out, "w") as handle:
    handle.write("\n".join(lines) + "\n")
