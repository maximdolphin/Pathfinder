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
point: precaching is not covering these 34 pipelines, and no cache carries them
between runs, so every first run compiles them again. That is the half of the
acceptance that fails, and the driver disk caches were the wrong lever for it —
they are the driver's copy, not the engine's. The engine's own answer is a
recorded `.upipelinecache` shipped with the build, which nothing here does yet.

**The 22 stalls in the cold run are mostly the fixture.** They land at t=14.1,
42.1, 68.1, 78.1, 88.3 and 124.0 seconds — the capture timestamps. Taking a
screenshot stalls the frame it is taken on, and the perf recorder counts it.
Any stall attribution here has to exclude the capture frames first, or it is
counting the measurement.
