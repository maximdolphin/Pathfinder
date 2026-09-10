# A year in twelve milliseconds

T085. The task asked for a thousand times normal. The measurement is **two and a
half billion**, and the reason is not that the code is fast.

## What was measured

A year of the home world's orbit — 29,013,065 seconds — stepped an hour at a
time, placing every body in the system and asking the sky for the sun's
declination at each of the 8,060 steps:

```
a year of 29013065 s stepped in 8060 hourly steps, sampling the ephemeris and
the sky at each: 0.012 real seconds
the clock stopped at 29013064.918159 s against a year of 29013064.918159 s,
a difference of 0.000e+00 s
every body is within 0.000000 m of where asking directly puts it
the sun reached 10.58 degrees of declination against an axial tilt of 10.04
```

Under a minute by three and a half orders of magnitude, and the last two lines
are the ones that matter: **the year ended exactly where asking the model once
puts it.** Not close. The same double.

## Why it is that fast

**Because T070 refused to integrate.** An integrated solar system has to be
stepped from the epoch to the moment you care about, so knowing where a moon
will be next August costs a simulated year of work whether or not anybody
watches it happen. An analytic ephemeris is a function of time: next August
costs exactly what now costs.

That shows up in a stopwatch as its own test. Skipping a century and placing
every body afterwards takes **three microseconds**, and lands within a floating
point zero of walking the same century in year-long steps. The acceleration
knob is therefore not a second simulation running in a hurry; it is the same
question asked at a different argument.

The 8,060 steps above are not the clock's cost. They are the cost of *looking*
— of sampling seasons finely enough that nothing is stepped over. A year nobody
needs to watch is `Skip`, and `Skip` is one addition.

## The bug the acceptance was written to catch

The first `Advance` accumulated: `SecondsFromEpoch += Step`, eight thousand
times. That is an integration of the clock itself, and it drifts — the same
failure mode, one level up, that the analytic ephemeris exists to avoid.

Every step is now measured from where the interval started, and the last one is
assigned the destination rather than computed towards it:

```cpp
SecondsFromEpoch = FMath::Min(Start + Cap * Steps, Target);
```

Which is why the difference above is `0.000e+00` and not a plausible-looking
`4e-7` that nobody would have questioned.

## The cap is a correctness knob

At a year a minute, one frame of real time is 8,760 simulated seconds. Nothing
downstream is handed that in one piece:

```
one sixtieth of a second at 525600x is 8760 simulated seconds, taken in 3 steps
of at most 3600
with no cap the same interval is 1 step, arriving at 8760.000000000 s
```

Three steps and one step arrive at the same instant, to the bit. The cap costs
time and keeps the answer, which is the only trade it is allowed to make — the
ephemeris does not need it, but the climate, the atmosphere and anything else
that carries state between steps will, and they will be handed hours rather
than weeks.
