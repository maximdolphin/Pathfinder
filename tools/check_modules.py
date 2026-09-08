# -*- coding: utf-8 -*-
"""Enforces the module layering in docs/architecture/module-map.md.

A layering that lives only in a document is a layering that lasts until the
first inconvenient afternoon. This parses every `*.Build.cs` in the project,
reads the dependencies it actually declares, and fails if they are not the ones
allowed here.

It checks two things:

1. **No undeclared edge.** A module may depend only on what this file says.
2. **No cycle, and no upward edge.** The allowed set is a strict order, so a
   dependency from a lower module to a higher one is a contradiction and is
   caught by construction rather than by inspection.

Run by CI on every commit. Exits non-zero with the offending edge and the rule
it broke.
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "client", "Source")

# The layering, lowest first. A module may depend on anything ABOVE it in this
# list and on nothing below — which is what makes the order the rule rather
# than a comment about the rule.
#
# LedgerGame does not exist yet; LedgerClient is standing in as the composition
# root and is the only module allowed a long list (ARCH Rule 2). It shrinks as
# LedgerSky, LedgerProcGen and LedgerPawn are carved out of it.
LAYERS = [
    "LedgerCore",
    "LedgerFlight",
    "LedgerMaterial",
    "LedgerTerrain",
    "LedgerClient",
    "LedgerHarness",
]

# Project dependencies each module is permitted. Engine modules are not listed:
# the rule is about our own graph.
ALLOWED = {
    "LedgerCore": set(),
    "LedgerFlight": set(),
    "LedgerMaterial": {"LedgerCore"},
    "LedgerTerrain": {"LedgerCore"},
    "LedgerClient": {"LedgerCore", "LedgerFlight", "LedgerMaterial", "LedgerTerrain"},
    "LedgerHarness": {"LedgerCore", "LedgerTerrain", "LedgerClient"},
}

# Everything but the composition root gets a short list. ARCH Rule 2.
MAX_DEPENDENCIES = 3
COMPOSITION_ROOTS = {"LedgerClient"}


def declared_dependencies(path):
    """Project module names named in a Build.cs, in declaration order."""
    body = io.open(path, encoding="utf-8").read()
    # Strip comments first, so a module named in a sentence is not a dependency.
    body = re.sub(r"//[^\n]*", "", body)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return [name for name in re.findall(r'"(Ledger\w+)"', body)]


def main():
    failures = []
    modules = {}

    for entry in sorted(os.listdir(SOURCE)):
        build = os.path.join(SOURCE, entry, entry + ".Build.cs")
        if os.path.isfile(build):
            modules[entry] = declared_dependencies(build)

    for name in sorted(modules):
        if name not in ALLOWED:
            failures.append(
                "%s is not in the layering. Add it to tools/check_modules.py "
                "and to docs/architecture/module-map.md, in that order of "
                "thinking about it." % name)
            continue

        deps = set(modules[name]) - {name}
        rank = LAYERS.index(name)

        for dep in sorted(deps):
            if dep not in ALLOWED:
                failures.append("%s depends on %s, which is not a known module."
                                % (name, dep))
                continue
            if dep not in ALLOWED[name]:
                failures.append(
                    "%s depends on %s, which it is not allowed to.\n"
                    "    ARCH Rule 2: every module declares a short, explicit "
                    "dependency list.\n"
                    "    If %s genuinely needs %s, the design is wrong, not the rule."
                    % (name, dep, name, dep))
            if LAYERS.index(dep) > rank:
                failures.append(
                    "%s depends on %s, which is ABOVE it in the layering.\n"
                    "    ARCH SS2.1: dependencies flow down only, never sideways "
                    "and never up.\n"
                    "    The shared thing moves down a layer; it does not get an "
                    "extra edge." % (name, dep))

        if name not in COMPOSITION_ROOTS and len(deps) > MAX_DEPENDENCIES:
            failures.append(
                "%s declares %d project dependencies; the limit is %d.\n"
                "    ARCH Rule 2: only the composition root may know about many "
                "others." % (name, len(deps), MAX_DEPENDENCIES))

    if failures:
        sys.stderr.write("\nModule layering violated:\n\n")
        for failure in failures:
            sys.stderr.write("  %s\n\n" % failure)
        return 1

    print("module layering: %d modules, all edges legal" % len(modules))
    for name in LAYERS:
        if name in modules:
            deps = sorted(set(modules[name]) - {name})
            print("  %-16s -> %s" % (name, ", ".join(deps) if deps else "(engine only)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
