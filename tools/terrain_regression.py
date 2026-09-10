#!/usr/bin/env python3
"""Turn the four terrain failure modes into build failures. T067.

M02's gate names four things that must not happen on the transect: holes,
cracks, popping and budget overruns. Each is measured by the scripted flight and
written into `out/performance.txt` as a `key value` block; this reads that block
against the thresholds below and exits non-zero when one is exceeded.

The thresholds are in this file rather than in a data file on purpose. A
threshold is a claim about what is acceptable, it needs the paragraph explaining
why that number, and a number without its argument is a number somebody raises
the next time it fails.

Prove it works before trusting it:

    python tools/terrain_regression.py --check          # the current run
    python tools/terrain_regression.py --demonstrate    # every fault, run and caught

`--demonstrate` runs the flight five times: once clean, then once with each of
the deliberate faults below, and reports whether each was caught. A regression
suite that has never been shown to fail is a regression suite nobody should
trust.

    holes    -nolodbrake      the LOD brake off, so the tree out-subdivides the
                              section pool and the visible set starves
    cracks   -breakstitching  every patch edge declared un-stitched, so a patch
                              beside a coarser neighbour keeps vertices the
                              neighbour does not have
    popping  -breakmorph      MorphScale zero, so a collapsing node snaps the
                              whole transition instead of easing through it
    budget   -nopatchdisk     the disk cache off, so every patch is generated
"""

import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPORT = os.path.join(ROOT, "out", "performance.txt")
PROJECT = os.path.join(ROOT, "client", "Ledger.uproject")

# Each: (key, comparison, limit, what it means when it trips).
CHECKS = [
    ("holes_worst", "<=", 40,
     "Sections the tree wanted and the pool could not give it, at the worst\n"
     "frame of the flight. Not zero: this counter is a horizon test rather than\n"
     "a frustum test and overcounts by a margin nobody has pinned down (see\n"
     "docs/comparisons/overload/), so the threshold is set above the noise it\n"
     "reads on a clean run rather than at the number the name implies."),

    ("cracks_stitch_rejects", "<=", 400,
     "Cached patches thrown away because their edge stitching no longer matched\n"
     "their neighbours' depths. Each one was a seam on screen until the rebuild\n"
     "landed. Some are unavoidable -- a moving camera changes neighbour depths --\n"
     "so this catches the rate going wrong, not the event happening."),

    ("pop_morph_enabled", "==", 1,
     "Whether the LOD transition is being eased through rather than snapped.\n"
     "Binary, and the only one of these four that is: morphing is either wired\n"
     "up or it is not."),

    ("pop_hidden_quads", "<=", 2.5,
     "How far the surface moves at the largest LOD transition, measured in\n"
     "the patch's own quads rather than in metres.\n"
     "\n"
     "In metres this read 2,307 on a clean run and looked alarming. It was\n"
     "the root node: a patch ten thousand kilometres across has a colossal\n"
     "transition and it is never a pop, because nothing is ever close enough\n"
     "to one for it to subtend anything. Divided by the patch's own quad\n"
     "size the number is scale-free and comparable between depths, which is\n"
     "what a threshold needs."),

    ("budget_p99_ms", "<=", 110.0,
     "The 99th percentile frame. M02's gate is 16.7 and the terrain has never\n"
     "met it; this is set where the flight actually sits so that a REGRESSION\n"
     "fails the build, and it is not the gate. The gate is in the roadmap and\n"
     "is failing, which is a different fact and is recorded as one."),
]


def read_metrics(path):
    """The `key value` block at the end of the flight's report."""
    if not os.path.exists(path):
        raise SystemExit("no %s -- run the flight first" % path)

    text = open(path, encoding="utf-8", errors="replace").read()
    marker = "---- regression metrics ----"
    if marker not in text:
        raise SystemExit(
            "%s has no regression block. It is written by ULedgerPerfSubsystem::"
            "WriteReport; an old report predates it." % path)

    out = {}
    for line in text.split(marker, 1)[1].splitlines():
        match = re.match(r"^([a-z0-9_]+)\s+(-?[\d.]+)$", line.strip())
        if match:
            out[match.group(1)] = float(match.group(2))
    return out


def check(metrics, verbose=True):
    """True when every threshold holds. Prints what failed and why."""
    passed = True
    for key, op, limit, why in CHECKS:
        if key not in metrics:
            print("  MISSING  %-24s not in the report" % key)
            passed = False
            continue

        value = metrics[key]
        ok = (value <= limit) if op == "<=" else (value == limit)
        if verbose or not ok:
            print("  %-8s %-24s %10.2f  %s %g" % (
                "ok" if ok else "FAIL", key, value, op, limit))
        if not ok:
            for line in why.splitlines():
                print("           %s" % line)
            passed = False
    return passed


def fly(extra_args, editor):
    """One flight. Returns its metrics."""
    if os.path.exists(REPORT):
        os.remove(REPORT)

    command = [editor, PROJECT, "-game", "-windowed", "-resx=1280", "-resy=720",
               "-unattended", "-nosplash"] + extra_args
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    deadline = time.time() + 400
    while time.time() < deadline:
        if os.path.exists(REPORT):
            time.sleep(3)
            break
        time.sleep(5)
    try:
        process.wait(timeout=60)
    except subprocess.TimeoutExpired:
        process.kill()
    return read_metrics(REPORT)


FAULTS = [
    ("holes", ["-nolodbrake"], "holes_worst"),
    ("cracks", ["-breakstitching"], "cracks_stitch_rejects"),
    ("popping", ["-breakmorph"], "pop_morph_enabled"),
    ("budget", ["-nopatchdisk"], "budget_p99_ms"),
]


def demonstrate(editor):
    """Run the flight clean, then once per fault, and report what was caught."""
    print("clean run")
    clean = fly([], editor)
    clean_ok = check(clean)
    print("  -> %s\n" % ("PASS, as it must" if clean_ok
                         else "FAILED CLEAN -- the thresholds are wrong, not the build"))

    results = [("clean", clean_ok, "-")]
    for name, args, expect in FAULTS:
        print("fault: %s  (%s)" % (name, " ".join(args)))
        metrics = fly(args, editor)
        ok = check(metrics, verbose=False)
        caught = not ok
        print("  -> %s\n" % ("caught" if caught else "NOT CAUGHT"))
        results.append((name, ok, expect))

    print("summary")
    print("  %-10s %-14s %s" % ("fault", "verdict", "watched"))
    for name, ok, expect in results:
        if name == "clean":
            print("  %-10s %-14s %s" % (name, "pass" if ok else "FAIL", expect))
        else:
            print("  %-10s %-14s %s" % (name, "caught" if not ok else "MISSED", expect))

    every = clean_ok and all(not ok for _, ok, _ in results[1:])
    print("\nVERDICT: %s" % ("PASS" if every else "FAIL"))
    return every


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="check the existing out/performance.txt")
    parser.add_argument("--demonstrate", action="store_true",
                        help="run the flight clean and with each deliberate fault")
    parser.add_argument("--editor", default=os.path.join(
        os.environ.get("UE_ROOT_FULL",
                       r"C:\Program Files\Epic Games\UE_5.8"),
        "Engine", "Binaries", "Win64", "UnrealEditor.exe"))
    args = parser.parse_args()

    if args.demonstrate:
        return 0 if demonstrate(args.editor) else 1

    print("terrain regression, from %s" % REPORT)
    return 0 if check(read_metrics(REPORT)) else 1


if __name__ == "__main__":
    sys.exit(main())
