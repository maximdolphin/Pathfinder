// The simulation's own clock. T085.
//
// **A year has to be inspectable in a lunch break.** Seasons take a year, an
// eclipse cycle takes longer, and a bug in either is not a bug anybody can find
// at one second per second. So the clock runs at a rate, and everything that
// takes its time from here comes along.
//
// The one thing acceleration must not do is change the answer. The ephemeris is
// analytic -- a function of time, not an integration -- so asking it about next
// August costs the same as asking it about now and gives the same answer
// whatever route the clock took to get there. That is what makes this a knob
// rather than a second simulation.

#pragma once

#include "CoreMinimal.h"

struct FLedgerClock
{
	/// Where the simulation is, seconds from the system's epoch.
	double SecondsFromEpoch = 0.0;

	/// Simulated seconds per real second. 1 is a wristwatch; 525600 puts a year
	/// in a minute.
	double Rate = 1.0;

	/// The largest single step anything downstream will be asked to take.
	///
	/// **Not a performance knob -- a correctness one.** At a rate high enough
	/// to be useful, one frame's worth of real time is weeks of simulated time,
	/// and anything that integrates rather than solving would be handed a step
	/// it cannot survive. Capping it turns a big jump into many small ones,
	/// which costs time and keeps the answer.
	double MaxStepSeconds = 3600.0;

	/// Advance by a real interval, calling back once per bounded step.
	///
	/// Returns how many steps were taken. The callback receives the simulated
	/// time at the END of each step, because that is where anything sampling
	/// the world wants to look.
	LEDGERCORE_API int32 Advance(
		double RealDeltaSeconds, TFunctionRef<void(double)> OnStep);

	/// Advance without telling anybody, for the case where nothing needs to see
	/// the intermediate states. This is what makes a year cheap: the ephemeris
	/// does not care how it got here.
	LEDGERCORE_API void Skip(double SimulatedSeconds);
};
