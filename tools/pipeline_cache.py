# -*- coding: utf-8 -*-
"""Builds the shipped PSO cache and puts it where the packaged build reads it.

    python tools/pipeline_cache.py            build and install into the stage
    python tools/pipeline_cache.py --record   print how to record a new one

**Why this is a script and not a config setting.** Unreal reads the bundled
cache from `<Project>/Content/PipelineCaches/<Platform>/`, and UAT stages that
directory -- but `BuildCookRun` *deletes* loose non-asset files from `Content/`
during the cook. Put the cache there and package, and it is gone: the staged
build logs "Could not open FPipelineCacheFile" naming the exact path it wanted.
So the cache is built from a committed `.spc` after packaging and copied
straight into the staged build.

That took the packaged run from 34 `PSOPrecacheState: Missed` to 0.

Recording a new one, when the shaders change enough that the old cache misses:

    Ledger.exe -useFixedTimeStep -fps=60 \\
        -dpcvars="r.ShaderPipelineCache.SaveBoundPSOLog=1,r.ShaderPipelineCache.LogPSO=1"

The flight has to reach its end and exit -- the recording is flushed on
shutdown, which is why it produced nothing at all until the harness learned to
stop. Then Expand the `.rec.upipelinecache` against the cook's `.shk` files
into `client/Build/Windows/PipelineCaches/Ledger_SM6.spc`, which is committed.
"""

import io
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
PROJECT = os.path.join(ROOT, "client", "Ledger.uproject")

STABLE = os.path.join(ROOT, "client", "Build", "Windows", "PipelineCaches", "Ledger_SM6.spc")
KEYS = os.path.join(ROOT, "client", "Saved", "Cooked", "Windows", "Ledger",
                    "Metadata", "PipelineCaches")
STAGE = os.path.join(ROOT, "client", "Saved", "StagedBuilds", "Windows", "Ledger",
                     "Content", "PipelineCaches", "Windows")

# The name is not ours to choose: FPipelineFileCacheManager builds it as
# <Name>_<ShaderPlatform>.stable.upipelinecache and opens exactly that.
CACHE_NAME = "Ledger_PCD3D_SM6.stable.upipelinecache"


def main():
    if "--record" in sys.argv:
        print(__doc__)
        return 0

    keys = [os.path.join(KEYS, name) for name in
            ("ShaderStableInfo-Global-PCD3D_SM6.shk", "ShaderStableInfo-Ledger-PCD3D_SM6.shk")]
    missing = [path for path in [STABLE] + keys if not os.path.isfile(path)]
    if missing:
        for path in missing:
            sys.stderr.write("missing: %s\n" % path)
        sys.stderr.write(
            "The .shk files come from the cook and need NeedsShaderStableKeys\n"
            "under [DevOptions.Shaders]; cook once before running this.\n")
        return 1

    built = os.path.join(ROOT, "out", "pso", CACHE_NAME)
    os.makedirs(os.path.dirname(built), exist_ok=True)

    # Built outside Content/ deliberately. See the note at the top.
    result = subprocess.run(
        [ENGINE, PROJECT, "-run=ShaderPipelineCacheTools", "Build", STABLE]
        + keys + [built, "-unattended", "-nopause"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", "replace")
    if "Success - 0 error" not in output:
        sys.stderr.write(output[-3000:])
        return 1

    if not os.path.isdir(os.path.dirname(STAGE)):
        sys.stderr.write("no staged build at %s -- package first\n" % STAGE)
        return 1
    os.makedirs(STAGE, exist_ok=True)
    shutil.copy2(built, os.path.join(STAGE, CACHE_NAME))

    print("installed %s" % os.path.join(STAGE, CACHE_NAME))
    print("a cold packaged run should now log 'Opened FPipelineCacheFile' and")
    print("report zero PSOPrecacheState: Missed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
