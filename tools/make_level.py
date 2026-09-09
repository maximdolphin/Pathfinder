# -*- coding: utf-8 -*-
"""Builds the project's own level, and puts the fixed world actors in it.

    UnrealEditor-Cmd <project> -run=pythonscript -script=tools/make_level.py

**Why this exists.** The project loaded `/Engine/Maps/Entry` -- an empty engine
map -- and a subsystem spawned everything into it at runtime. So the world
existed only while code was running: nothing to open, nothing to select, and no
map for a packaged build to start in. ADR-0006.

**What goes in the level and what does not.** The level holds the things that
are not generated: the sun, the sky light, the post-process volume. The planet,
its terrain, the settlement and the ship are generated, and they stay that way --
placing a 6,371 km procedural planet in a map would be placing a pointer to a
generator, which is what code is for.

The actors are placed with default settings on purpose. `ULedgerWorldBuilder`
finds and configures them at BeginPlay, so the level says *what exists* and the
code says *how it behaves*, and neither has to duplicate the other. A level
that also carried the settings would be a second place for them to drift.

Written as a script rather than by hand so that rebuilding the level is a
command, not an afternoon of clicking, and so that what is in it is reviewable
as a diff.
"""
import os

import unreal

LEVEL = "/Game/Maps/Ledger"


def build():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # Rebuilt from scratch each time. `new_level` refuses to overwrite, so
    # without this the second run of the generator fails -- and "running it
    # again produces no diff" is the property that makes a generated asset
    # trustworthy at all. Deleting first is what makes this idempotent.
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        unreal.EditorAssetLibrary.delete_asset(LEVEL)

    levels.new_level(LEVEL)

    placed = []

    # The sun. Rotation and intensity are set by the world builder, which knows
    # which side of the planet the descent comes down on.
    sun = actors.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if sun:
        sun.set_actor_label("Sun")
        sun.root_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        placed.append("Sun")

    # Sky fill. Without it every wall in the town reads as a black rectangle
    # under a lit roof.
    sky = actors.spawn_actor_from_class(
        unreal.SkyLight, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if sky:
        sky.set_actor_label("SkyLight")
        sky.root_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        placed.append("SkyLight")

    # Unbound, so it applies everywhere. The camera goes from a lit surface to
    # orbit without leaving any volume it could be inside.
    post = actors.spawn_actor_from_class(
        unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if post:
        post.set_actor_label("WorldPostProcess")
        post.set_editor_property("unbound", True)
        placed.append("WorldPostProcess")

    levels.save_current_level()

    report = os.path.join(unreal.Paths.project_dir(), "..", "build", "make-level.txt")
    report = os.path.normpath(report)
    if not os.path.isdir(os.path.dirname(report)):
        os.makedirs(os.path.dirname(report))
    with open(report, "w") as handle:
        handle.write("Level: %s\n" % LEVEL)
        handle.write("Placed: %s\n" % (", ".join(placed) if placed else "nothing"))
        # The level's provenance lives here rather than on the asset. Unreal
        # drops package metadata set before a map save and does not persist it
        # when set afterwards; both were tried. tools/verify_assets.py checks
        # this line, so the level is not simply exempt from the rule.
        handle.write("Generator: tools/make_level.py\n")
        handle.write("VERDICT: %s\n" % ("PASS" if len(placed) == 3 else "FAIL"))


build()
