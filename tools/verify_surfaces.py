# Reads back what Unreal actually did, rather than what the import plan asked
# for. The plan says TC_Normalmap; this says whether the asset on disk agrees.
#
#   UnrealEditor-Cmd <project> -run=pythonscript -script=build/verify_surfaces.py
#
# Writes build/verify-surfaces.txt, because print() from a commandlet is not
# reliably in the log and a check nobody can read is not a check.
import unreal, os

EXPECTED = {
    "albedo": ("TC_DEFAULT", True),
    "normal": ("TC_NORMALMAP", False),
    "packed_ao_rough_height": ("TC_MASKS", False),
}

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Surfaces"], force_rescan=True)
assets = registry.get_assets_by_path("/Game/Surfaces", recursive=True)

lines, problems, checked = [], [], 0
for data in assets:
    texture = data.get_asset()
    if not isinstance(texture, unreal.Texture2D):
        continue
    name = str(data.asset_name)
    want = EXPECTED.get(name)
    if want is None:
        problems.append("%s: unexpected asset name" % data.package_name)
        continue
    compression = str(texture.get_editor_property("compression_settings"))
    srgb = bool(texture.get_editor_property("srgb"))
    checked += 1
    if want[0] not in compression.upper():
        problems.append("%s: compression %s, expected %s"
                        % (data.package_name, compression, want[0]))
    if srgb != want[1]:
        problems.append("%s: sRGB %s, expected %s"
                        % (data.package_name, srgb, want[1]))
    if checked <= 3:
        lines.append("  %-52s %-34s sRGB=%s"
                     % (data.package_name, compression, srgb))

body = ["Imported surface textures, read back off the assets.", ""]
body += lines
body += ["", "  %d textures checked" % checked]
body += ["  %s" % p for p in problems]
body += ["", "VERDICT: %s" % ("PASS" if problems == [] and checked else
                              "FAIL (%d problems, %d checked)" % (len(problems), checked))]

out = os.path.join(unreal.Paths.project_dir(), "..", "build", "verify-surfaces.txt")
with open(os.path.normpath(out), "w") as handle:
    handle.write("\n".join(body) + "\n")
