#include "LedgerClock.h"

int32 FLedgerClock::Advance(double RealDeltaSeconds, TFunctionRef<void(double)> OnStep)
{
	if (!(RealDeltaSeconds > 0.0) || !(Rate > 0.0))
	{
		return 0;
	}

	// **Computed from the start, never accumulated.** Adding a step to the
	// clock eight thousand times is an integration, and an integration drifts;
	// the whole point of an analytic ephemeris is that the route taken to a
	// moment cannot change the moment. So the destination is fixed up front and
	// every step is measured from the origin, which makes a year in hourly
	// steps land on precisely the instant one step would have.
	const double Start = SecondsFromEpoch;
	const double Target = Start + RealDeltaSeconds * Rate;
	const double Cap = MaxStepSeconds > 0.0 ? MaxStepSeconds : Target - Start;

	int32 Steps = 0;
	while (SecondsFromEpoch < Target)
	{
		++Steps;
		SecondsFromEpoch = FMath::Min(Start + Cap * Steps, Target);
		OnStep(SecondsFromEpoch);

		// A guard, not a limit anybody should reach: a rate high enough to need
		// more than this in one frame is a rate that should be using Skip.
		if (Steps > 1000000)
		{
			SecondsFromEpoch = Target;
			break;
		}
	}
	return Steps;
}

void FLedgerClock::Skip(double SimulatedSeconds)
{
	SecondsFromEpoch += SimulatedSeconds;
}
