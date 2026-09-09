# Pipeline state objects, cold against warm (T424)

T424 wants a packaged build with no compilation stall on first sight of a
material, and a first run's frame-time trace that matches a second run's.
Both halves were measured, both under `-useFixedTimeStep -fps=60`, back to back
on an idle machine. `cold.txt` is the run with the caches deleted; `warm.txt`
is the run straight after it.

## The two D3D12 driver disk caches cannot be reached at all

This was recorded as "not enabled yet". It is stronger than that: there is no
route to them from outside the engine.

| tried | result |
|---|---|
| `[/Script/Engine.RendererSettings]` | `r.D3D12.PSO.DiskCache=0` |
| `[SystemSettings]` | `r.D3D12.PSO.DiskCache=0` |
| `[ConsoleVariables]` — the earliest project-owned section, applied by `FConfigCacheIni::LoadConsoleVariablesFromINI` in PreInit | `r.D3D12.PSO.DiskCache=0` |
| `-dpcvars=r.D3D12.PSO.DiskCache=1,...` on the command line | `r.D3D12.PSO.DiskCache=0` |

All four produce the same pair of lines:

```
LogConfig:    CVar [[r.D3D12.PSO.DiskCache:1]] deferred - dummy variable created
LogD3D12RHI:  Not using pipeline state disk cache per r.D3D12.PSO.DiskCache=0
```

"deferred — dummy variable created" is the engine saying the setting arrived
before the variable existed. The D3D12 RHI reads the value in the same pass
that registers it, so the deferred value lands afterwards and is never seen.
The lines are out of `DefaultEngine.ini` rather than sitting there looking like
a setting somebody chose.

## Cold against warm

| | cold | warm |
|---|---|---|
| overall mean | 16.4 ms | 16.6 ms |
| p99 | 25.7 ms | 28.9 ms |
| frames over budget | 50.4% | 50.4% |
| PSOPrecacheState: Missed | 34 | 34 |

Phase means agree within about 2% (surface 17.0/17.0, town 17.0/17.0, ridge
sweep 17.2/17.2, coast 20.6/20.8, underwater 22.5/22.9). The previous attempt
at this comparison had the town phase 18% apart with nothing changed; that gap
was the free-running fixture, and it is gone.

p99 is 12% apart, which is the half of the trace that a mean does not see.

## What is not met, and what it needs

**34 `PSOPrecacheState: Missed`, identical cold and warm.** Identical is the
point: precaching is not covering these 34 pipelines, and no cache carried them
between runs, so every first run compiled them again.

Three things were wrong, and each hid the next.

1. **`r.PSOPrecaching`, `r.PSOPrecache.Resources` and
   `r.ShaderPipelineCache.Enabled` were in a section that ignores them.**
   `[/Script/WindowsTargetPlatform.WindowsTargetSettings]` is a UObject
   section; a stray `r.*` key there never becomes a console variable, and none
   of the three appeared in the log's `Set CVar` lines. Two of them happen to
   match the engine default, so the mistake cost nothing and showed nothing.
2. **The cook was never asked for stable shader keys.** Without
   `NeedsShaderStableKeys` there is no `.shk`, and without a `.shk` a recorded
   PSO log cannot become a shipped cache — the recording names shaders by a
   hash that changes every cook.
3. **The flight never exited.** The pipeline cache writes its recording on
   shutdown, and the fixture wrote its report and then sat there until it was
   killed. `r.ShaderPipelineCache.SaveBoundPSOLog=1` produced nothing at all
   until the flight learned to stop. (The same bug meant the CI job, which
   runs the flight and then compares captures, could never have passed.)

With all three fixed: a clean run produces a `.rec.upipelinecache`,
`ShaderPipelineCacheTools Expand` turns its 38 recorded PSOs into 82 stable
ones against 21,175 shader info lines, and `Ledger_SM6.spc` is committed under
`client/Build/Windows/PipelineCaches/` for the cook to convert.

**Most of the 22 stalls in the cold run were the fixture.** They landed at
t=14.1, 42.1, 68.1, 78.1, 88.3 and 124.0 seconds — the capture timestamps.
Taking a screenshot stalls the frame it is taken on, and the recorder counted
it. The recorder now drops frames while a screenshot is outstanding, and the
count goes 22 → 12 (`cold-captures-excluded-stalls.txt`).

A fixed two-frame skip was tried first and was the wrong shape — it removed
nine and left the coast and underwater captures in the list, because the
request is served whenever the render thread gets to it. Picking a larger
number until the list looked clean would have been picking the answer, so the
skip runs while `FScreenshotRequest::IsScreenshotRequested()` instead.

What is left is not shader compilation:

```
 40.2 ms t=63.1s entry     506.0 ms t=124.0s coast    128.6 ms t=140.1s ascent
209.5 ms t=63.8s entry     197.3 ms t=124.1s coast     57.8 ms t=140.2s ascent
 44.2 ms t=78.1s town       47.4 ms t=132.0s under     34.9 ms t=141.0s ascent
 36.3 ms t=111.6s sweep    686.9 ms t=133.5s under
                           192.1 ms t=133.6s under
```

t=124 is the coast teleport, t=132 the underwater one, t=140 the start of the
climb. They are the frames where the camera moves somewhere new or the tree
collapses — the terrain's problem, and the same ~300 ms spike the component
comparison found. None of them coincides with a precache miss.

## Where this stopped

The shipped cache is built and in place, and it is not being opened.

`client/Content/PipelineCaches/Windows/Ledger_PCD3D_SM6.stable.upipelinecache`
matches the path and name `FPipelineFileCacheManager` constructs
(`PipelineFileCache.cpp:2196`): project content dir, `PipelineCaches`, the ini
platform name, then `<Name>_<PlatformName>.stable.upipelinecache`. UAT stages
`*.upipelinecache` from that directory as UFS, so it is inside the pak. The
count of `PSOPrecacheState: Missed` is unchanged at 34, and the log contains no
`FShaderPipelineCache` or `PipelineFileCache` lines at all — the subsystem
never opened it.

Two things to check next, and the second may invalidate the whole approach:

1. **Nothing calls `OpenPipelineFileCache`.** The bundled cache is opened by
   name at a point the game chooses; a project that never asks gets nothing.
   `r.ShaderPipelineCache.StartupFile` or an explicit open in game code is the
   likely missing piece.
2. **`PSOPrecacheState: Missed` may be the wrong meter.** It is reported by the
   PSO *precaching* system, which builds pipelines from materials at load. The
   bundled pipeline cache is a different mechanism that pre-creates PSOs from a
   recorded list. A bundled cache could be working perfectly and this counter
   would not move. Before spending more on the cache, find a counter that
   measures what the cache actually does — otherwise this is another
   measurement that cannot show success.

The second point is why this stopped rather than continuing to iterate. Four
package-and-fly cycles were spent moving a number that may not be connected to
the thing being changed.
