# -*- coding: utf-8 -*-
"""Reads back what Unreal actually built, rather than what the bake asked for.

    UnrealEditor-Cmd <project> -run=pythonscript -script=tools/verify_meshes.py

The bake reports its own settings, in the process that set them. That is the
weaker claim: "I set the flag" and "the asset has it" are different, and this
project has already shipped a camera check that passed forty-eight times by
comparing a measurement against itself. A fresh process opening the saved asset
is the stronger one.
"""
import os

import unreal

# Nanite where it was asked for, and off where it was deliberately declined.
# The trees are forty-six triangles and their colour is per-slot; Nanite
# would buy nothing and does not carry mesh vertex colours through.
EXPECTED = {"SM_ShipHull": True, "SM_Tree_A": False, "SM_Tree_B": False}

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Meshes"], force_rescan=True)
assets = registry.get_assets_by_path("/Game/Meshes", recursive=True)

lines = ["Baked static meshes, read back off the assets.", ""]
problems = []
checked = 0

for data in assets:
    mesh = data.get_asset()
    if not isinstance(mesh, unreal.StaticMesh):
        continue
    name = str(data.asset_name)
    checked += 1

    nanite = mesh.get_editor_property("nanite_settings").get_editor_property("enabled")
    distance_field = mesh.get_editor_property("generate_mesh_distance_field")
    body = mesh.get_editor_property("body_setup")
    tris = mesh.get_num_triangles(0)
    lods = mesh.get_num_lods()
    generator = unreal.EditorAssetLibrary.get_metadata_tag(mesh, "Ledger.Generator")

    lines.append("  %-16s %6d tris  nanite %-3s  lods %d  collision %-3s  dist field %-3s  by %s"
                 % (name, tris, "on" if nanite else "OFF", lods,
                    "yes" if body else "NO", "yes" if distance_field else "NO",
                    generator or "UNRECORDED"))

    wanted = EXPECTED.get(name)
    if wanted is not None and bool(nanite) != wanted:
        problems.append("%s: Nanite is %s and should be %s"
                        % (name, "on" if nanite else "off", "on" if wanted else "off"))
    if not distance_field:
        problems.append("%s: no mesh distance field" % name)
    if not body:
        problems.append("%s: no collision" % name)
    if not generator:
        problems.append("%s: does not record the generator that made it" % name)

lines.append("")
lines.append("  %d meshes checked" % checked)
lines += ["  " + p for p in problems]
lines.append("")
lines.append("VERDICT: %s" % ("PASS" if checked and not problems
                              else "FAIL (%d problems, %d checked)" % (len(problems), checked)))

out = os.path.normpath(os.path.join(unreal.Paths.project_dir(), "..", "out", "verify-meshes.txt"))
with open(out, "w") as handle:
    handle.write("\n".join(lines) + "\n")
