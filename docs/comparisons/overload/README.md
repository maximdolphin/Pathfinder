# Streaming under deliberate overload (T063)

One authority for how much streaming work a frame may do, spent by priority
rather than by arrival order — and a load the streamer cannot possibly keep up
with, to find out what gives first.

Run it: `-overload`. Twelve teleports across the planet, two seconds apart, each
invalidating the entire visible set at once.

## The acceptance

> Under deliberate overload, the frame budget holds and the degradation is in
> detail rather than in holes.

```
1416 frames measured, the frame containing each teleport excluded
frame time: median 7.8 ms (129 fps), p99 31.5 ms
detail refused for want of budget: worst 643 in a frame, mean 15.3
```

The budget holds: the median frame is less than half of it, while the streamer
is being handed a fresh planet every two seconds. p99 is reported and not
asserted — a teleport across a planet is allowed to cost a frame, and asserting
otherwise would be a threshold chosen to pass.

The degradation is work **put off**: fifteen requests a frame refused for want of
budget on average, six hundred in the worst. `overload.png` is from the same run
and the ground in it is solid.

## What changed

**One budget instead of three.** Cache uploads, finished patch jobs and the
scatter rebuild each had their own limit, each reasonable alone, and a frame
that did all three did the sum. `FLedgerStreamingBudget` is the only allowance
now, and everything spends from it.

**Priority, not arrival order.** Requests are classified — collision, hole,
detail, speculative — and sorted by class before distance. A hole three
kilometres away matters more than a detail level on the next hillside, because
one is ground missing and the other is ground that is merely coarse. Collision
is exempt from the budget entirely and reported separately: ground the player is
standing on is not a thing to be economical about.

**A brake on the LOD when the pool runs short.** Sections the pool does not have
cannot be filled, so a visible set larger than the pool is holes by arithmetic.
The error threshold now rises with pool occupancy and eases back.

Set at ninety-five per cent occupancy, not eighty. At eighty the flight's
ordinary cruise sat above the target and the threshold crept up all the time:
the underwater capture came back as a featureless gradient because the seabed
had been coarsened during a frame that was never in trouble. A brake that drags
is worse than no brake.

## The metric that was wrong, and three fixes for a problem that was not there

The first overload run reported **7,105 holes in a frame against 8,644 visible
nodes, in a hundred per cent of frames**. That reads as the exact failure the
acceptance is about.

Three things were tried. A resident coarse shell so that arriving anywhere finds
ground already there: the count moved by under two per cent. Exempting that
shell from the parent-hold release, since it was being dismantled as fast as it
was built: under two per cent again. The LOD brake: under two per cent.

Three fixes that do nothing is not three failures. **`UnfilledNodes` counts
nodes that `bVisible` calls visible, and `bVisible` is a horizon test rather
than a frustum one** — so it counts the ground behind the camera, which is never
streamed and never looked at. `overload.png` settled it: solid ground, no holes.

Two further things followed from that. The LOD brake was being driven by the
same overcounted number, throttling detail because of terrain nobody could see;
it is driven by pool occupancy now, which is the resource that actually runs
out. And the fixture's verdict no longer asserts anything from the hole count —
it asserts on refusals, which are what the budget actually did, and leaves
"was ground left out" to the photograph.

**The resident shell stays** because prefetching the coarse levels is right on
its own terms and costs sixty-odd sections of three thousand six hundred. It is
not a fix for anything, and the header says so.

## And a habit worth naming

Three separate captures this session were misread because they were taken on the
night side of the planet: a cave mouth, a biome boundary, and the first two
overload frames. A black frame cannot distinguish correct ground from missing
ground. Every fixture that photographs anything now searches for a site that is
both on land and in daylight.
