# Which component the terrain is made of, measured (T423)

`UProceduralMeshComponent` was the prototype's choice. It was never compared
against anything, which makes it a default that survived rather than a
decision. This is the comparison, and the answer that came out of it is not the
one the task expected.

Every number here is the mean over one phase of the same scripted flight —
`out/performance.txt`, 160 seconds, eleven phases, same seed, same machine,
nothing else running. The raw reports are in `terrain-component/`.

## The measurement that mattered was not the component

The frame budget has failed on the **game thread** for the whole project: 20 to
33 ms at low altitude against 16.7, with the GPU idle at 5 to 9. The only
terrain cost anybody had a number for was upload, which worsts at about 7 ms,
and `FLedgerTerrainStats::LastFrameUploadMs` carried a comment saying upload
"is now the *only* terrain cost on the game thread". Nothing had ever measured
that claim.

So before comparing components, the terrain tick was broken into its phases
(`tick ms (worst)` in the terrain log). The first run said:

```
tick ms (worst)  tree 24.07  harvest 7.03  collect 0.50  sort 6.45  imbalance 1.15  whole tick 31.87
```

and later, in the single frame where a climb collapses the tree,

```
tick ms (worst)  tree 380.22  harvest 11.48  collect 2.72  sort 7.53  imbalance 2.06  whole tick 381.28
```

`tree` is `UpdateTree` — the LOD walk. Not upload. Upload was third.

Per phase, terrain against the whole game thread:

| phase | game ms | terrain ms | terrain share |
|---|---|---|---|
| atmospheric entry | 9.9 | 5.1 | 52% |
| surface | 20.8 | 12.8 | 62% |
| town | 20.7 | 12.9 | 62% |
| ridge sweep | 33.4 | 18.8 | 56% |
| coast | 26.7 | 11.7 | 44% |
| underwater | 25.9 | 17.1 | 66% |

Terrain owns roughly 60% of a game thread that is roughly twice its budget, and
three quarters of that is the tree walk. The component type could not have been
the answer, because the component type is not where the time goes.

### Why the tree walk cost 24 ms

`UpdateTree` needs the distance to the **ground**, not to the reference sphere —
at low altitude over a mountain that difference is the entire LOD decision. It
got it by calling `SurfaceRadiusAt`, which is a full fractal elevation sample,
once per visited node per frame. A node's centre never moves, so that sample
has the same answer for the life of the node.

Caching it on the node (`FLedgerQuadNode::SurfacePoint`, set once in `Split`)
is five lines. Same flight, same seed:

| phase | terrain ms before | after |
|---|---|---|
| atmospheric entry | 5.14 | 2.13 |
| surface | 12.83 | 8.33 |
| town | 12.87 | 8.25 |
| ridge sweep | 18.83 | 11.21 |
| coast | 11.70 | 4.25 |
| underwater | 17.10 | 5.89 |

Overall p99 across the flight went from 43.3 ms to 35.1 ms. Still failing, but
the largest single item in the terrain tick is gone, and it was found by
measuring rather than by guessing — the standing suspicion had been the
per-frame imbalance diagnostic, which turns out to cost 1 to 2 ms.

## The component comparison

<!-- filled in below once the static run lands -->

## Decision

<!-- filled in -->
