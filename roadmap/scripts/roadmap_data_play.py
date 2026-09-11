# -*- coding: utf-8 -*-
"""M5P: playability -- a build you can sit down and fly.

Added 2026-09-11, after the owner's first playtest of the whole world.

Every milestone before this one was proved by a fixture: a scripted camera,
a measured transect, a photograph taken from a place chosen for it. None of
them asked the question a player asks, which is whether the thing can be
picked up and flown. The first time anybody did, it could not:

  * The default launch handed the ship to the scripted reentry, so the stick
    did almost nothing. (-play now puts the ship over the town instead.)
  * The picture was blurry and grey. Space read as fog.
  * Moving forward sounded like static: the engine voice is a raw saw, the
    pump voice is white noise under a one-pole filter near 1 kHz that climbs
    with the reactor's heat, and every gain steps once a frame.
  * The ship "bugs out and flies all over the place", and the session ended in
    ensures about coordinates past what the GPU can represent -- a divergence,
    not a slow drift.

**How it is proved.** An offscreen playtest (-RenderOffScreen, so it never
takes the screen) flies the ship the way a player does -- the same six axes
the key bindings feed -- off the pad, out over the town and back, and records
what a player would notice: whether it stays in one piece, how it looks, how
the frames hold, what it sounds like. The owner flying the same build is the
last check, not the first.

**Why here.** M05 made the ship a machine and M06 is the flight model proper.
This sits between them because nothing in M06 can be judged by someone who
cannot fly the ship at all.
"""

M5P = [
    dict(title="Offscreen playtest harness",
         detail="A -playtest fixture on top of -play: the ship over the pad, then a "
                "scripted stick through the same six axes the key bindings feed -- lift "
                "off, climb, fly out and back over the town, descend, hover. Every second "
                "it records height above the ground, speed, spin rate, attitude, hull and "
                "frame time; at set moments it takes a capture; at the end it writes a "
                "verdict. Runs with -RenderOffScreen so a playtest never takes the screen.",
         acceptance="One command flies a scripted session offscreen and writes a report and "
                    "captures; a deliberately broken build fails it.",
         days=2, refs=["SS6.9"]),
    dict(title="Stable flight near the ground",
         detail="The owner's first flight bugged out and flew all over the place. Find the "
                "divergence -- the rate hold, the allocator, the integrator across a hitch, "
                "or ground contact -- from the playtest's own record, and fix the cause.",
         acceptance="Across the playtest and twenty randomised stick sequences, spin stays "
                    "under 180 deg/s, speed stays inside the flight envelope, nothing is NaN, "
                    "and the ship never leaves the float-safe range.",
         days=3, refs=["SS6.9"]),
    dict(title="No freeze or crash in a play session",
         detail="The first session ended in ensures: a view transform past the precision "
                "the GPU can hold, a scene transform mismatch, evicted ray-tracing geometry. "
                "Each is either a symptom of the divergence or its own bug; either way none "
                "may appear.",
         acceptance="A ten-minute playtest logs no ensure, no crash, and no hitch over "
                    "250 ms after its first minute.",
         days=2, refs=["SS6.9"]),
    dict(title="A crisp picture",
         detail="The owner saw a blurry, grey picture. Find what softens it -- the visor "
                "and canopy effects, the temporal upscale, exposure in space -- and make the "
                "player's camera as sharp as the fixtures' cameras already are.",
         acceptance="Playtest captures hold edge contrast within 10% of a static control frame "
                    "from the same place, and space is black.",
         days=2, refs=["SS6.8"]),
    dict(title="An engine that sounds like an engine",
         detail="Forward thrust sounded like static: a raw sawtooth, a pump hiss of white "
                "noise near 1 kHz rising with the reactor's heat, and gains that step once a "
                "frame. A low, tonal rumble that follows the thrust, a pump voice below it, "
                "and every level smoothed per sample.",
         acceptance="At full throttle most of the generator's energy is below 400 Hz, no gain "
                    "changes in a step, and the owner hears a rumble.",
         days=1, refs=["SS6.9"]),
    dict(title="Play from the pad by default",
         detail="The plain launch runs the scripted test flight; the player's path is a "
                "flag. Make the player's path the default and keep the scripted flight "
                "behind its own flag, including for the packaged-build check that uses it.",
         acceptance="Launching with no flags puts the player over the town and in control; "
                    "the scripted flight runs only when asked for.",
         days=1, refs=["SS6.9"]),
    dict(title="Playable proof: the owner's ten minutes",
         detail="Rule 6. The owner flies the build.",
         acceptance="The owner flies the build for ten minutes and signs off, with the "
                    "offscreen playtest's report from the same build attached.",
         days=1, refs=["ARCH Rule 6"]),
]
