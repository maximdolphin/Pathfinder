# -*- coding: utf-8 -*-
"""Builds the project's own level, and puts the world's actors in it.

    UnrealEditor-Cmd <project> -run=pythonscript -script=tools/make_level.py

**Why this exists.** The project loaded `/Engine/Maps/Entry` -- an empty engine
map -- and a subsystem spawned everything into it at runtime. So the world
existed only while code was running: nothing to open, nothing to select, and no
map for a packaged build to start in. ADR-0006.

**What goes in the level.** Every actor the world is made of: the sun, the sky
light, the post-process volume, and -- since ADR-0006's third tier was finished
-- the planet, its atmosphere and the settlement. The first version kept those
three out on the grounds that placing a 6,371 km procedural planet is placing a
pointer to a generator. That was half right: the *terrain* is streamed and
cannot be an asset, but the actor that streams it is a thing in the world like
any other, and a world whose planet only appears once code runs is a world a
person cannot open and look at. A crossing to another body now reconfigures
these same actors rather than destroying them.

The ship is not here: it is the player's pawn, and the game mode spawns pawns.

The actors are placed with default settings on purpose. `ULedgerWorldBuilder`
finds and configures them before they begin play, so the level says *what
exists* and the code says *how it behaves*, and neither has to duplicate the
other. A level that also carried the settings would be a second place for them
to drift.

**Rebuilt in place, never deleted.** The first version deleted the map and made
a new one. It had only ever run when there was no map, so that was never
tested, and when it was: `does_asset_exist` answers False for a map, so the
delete was skipped and `new_level` refused; removing the file by hand left the
editor's asset registry -- scanned at startup -- still sure it was there, so
`new_level` refused again and the project was left with no level at all. So an
existing map is opened, the actors this script owns are removed from it, and
fresh ones are placed and saved. Only a missing map is created.

**The verdict is read back, not remembered.** The first version wrote PASS from
the list of things it had spawned, including on the runs where the save had
been refused. Now each call is checked, and the verdict comes from loading the
saved map again and listing what is actually in it.
"""
import os

import unreal

LEVEL = "/Game/Maps/Ledger"

# Everything that should be in the level, by label, exactly once each.
EXPECTED = ["Sun", "SkyLight", "WorldPostProcess", "Planet", "Atmosphere", "Settlement"]


def owned(actor):
    """Whether this script put the actor there, and so may take it away."""
    classes = (unreal.DirectionalLight, unreal.SkyLight, unreal.PostProcessVolume,
               unreal.LedgerPlanet, unreal.LedgerAtmosphere, unreal.LedgerSettlement)
    return isinstance(actor, classes) or actor.get_actor_label() in EXPECTED


def build():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    problems = []

    map_file = os.path.normpath(os.path.join(
        unreal.Paths.project_content_dir(), "Maps", "Ledger.umap"))

    if os.path.exists(map_file):
        if not levels.load_level(LEVEL):
            problems.append("could not open %s" % LEVEL)
        for actor in list(actors.get_all_level_actors()):
            if owned(actor):
                actors.destroy_actor(actor)
    elif not levels.new_level(LEVEL):
        problems.append("new_level refused %s" % LEVEL)

    def place(cls, label):
        actor = actors.spawn_actor_from_class(
            cls, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        if actor:
            actor.set_actor_label(label)
        else:
            problems.append("could not spawn %s" % label)
        return actor

    # The sun. Rotation and intensity are set by the world builder, which knows
    # which side of the planet the descent comes down on.
    sun = place(unreal.DirectionalLight, "Sun")
    if sun:
        sun.root_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)

    # Sky fill. Without it every wall in the town reads as a black rectangle
    # under a lit roof.
    sky = place(unreal.SkyLight, "SkyLight")
    if sky:
        sky.root_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)

    # Unbound, so it applies everywhere. The camera goes from a lit surface to
    # orbit without leaving any volume it could be inside.
    post = place(unreal.PostProcessVolume, "WorldPostProcess")
    if post:
        post.set_editor_property("unbound", True)

    # The body under everything, at the origin where the terrain's centre is.
    # Its radius, relief, seed and materials are the world builder's to set.
    place(unreal.LedgerPlanet, "Planet")

    # The air over it, centred on the same point. On an airless body it is
    # still here and draws nothing.
    place(unreal.LedgerAtmosphere, "Atmosphere")

    # The town. Laid out on whichever body is current, on a site chosen for
    # daylight, so it is placed empty and filled at play.
    place(unreal.LedgerSettlement, "Settlement")

    if not levels.save_current_level():
        problems.append("save_current_level refused")

    # Read it back from disk and list it.
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if not levels.load_level(LEVEL):
        problems.append("could not load %s back" % LEVEL)
    level_actors = list(actors.get_all_level_actors())
    found = sorted("%s (%s)" % (actor.get_actor_label(), actor.get_class().get_name())
                   for actor in level_actors)
    labels = [actor.get_actor_label() for actor in level_actors]
    for label in EXPECTED:
        count = labels.count(label)
        if count != 1:
            problems.append("%s is in the saved level %d times" % (label, count))

    report = os.path.join(unreal.Paths.project_dir(), "..", "build", "make-level.txt")
    report = os.path.normpath(report)
    if not os.path.isdir(os.path.dirname(report)):
        os.makedirs(os.path.dirname(report))
    with open(report, "w") as handle:
        handle.write("Level: %s\n" % LEVEL)
        handle.write("In the saved level, read back from disk:\n")
        for line in found:
            handle.write("  %s\n" % line)
        for problem in problems:
            handle.write("Problem: %s\n" % problem)
        # The level's provenance lives here rather than on the asset. Unreal
        # drops package metadata set before a map save and does not persist it
        # when set afterwards; both were tried. tools/verify_assets.py checks
        # this line, so the level is not simply exempt from the rule.
        handle.write("Generator: tools/make_level.py\n")
        handle.write("VERDICT: %s\n" % ("FAIL" if problems else "PASS"))


build()
