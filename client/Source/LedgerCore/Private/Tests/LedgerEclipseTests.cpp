// An eclipse the ephemeris predicts. T075.
//
// The acceptance is that a predicted eclipse is observable at the predicted
// time. So the prediction and the observation have to be separate things: the
// prediction is a search over the ephemeris, and the observation samples the
// star's disc and counts how much of it is behind something.

#include "LedgerSky.h"
#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FVector3d EclipseAnchor(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat));
	}

	/// How much of the star is hidden, by looking at the star.
	///
	/// Points spread over the star's disc; each one is hidden if the direction
	/// to it falls inside another body's disc. No overlap formula anywhere in
	/// here -- this is what the sky looks like, and the formula is a claim
	/// about it.
	double SampledCoverage(const FLedgerSystem& System, int32 Observer,
		const FVector3d& Anchor, double At)
	{
		TArray<FLedgerSkyBody> Seen;
		LedgerSky::VisibleBodies(System, Observer, Anchor, At, Seen);

		const FLedgerSkyBody* Star = Seen.FindByPredicate(
			[](const FLedgerSkyBody& B) { return B.BodyIndex == 0; });
		if (Star == nullptr || Star->DirectionInSurface.Z <= 0.0)
		{
			return 0.0;
		}

		// A basis across the star's disc.
		const FVector3d Centre = Star->DirectionInSurface;
		FVector3d Across = FVector3d::CrossProduct(Centre, FVector3d::UnitZ());
		if (Across.SquaredLength() < 1e-12)
		{
			Across = FVector3d::CrossProduct(Centre, FVector3d::UnitX());
		}
		Across = Across.GetSafeNormal();
		const FVector3d Up = FVector3d::CrossProduct(Centre, Across).GetSafeNormal();

		constexpr int32 Rings = 200;
		constexpr int32 PerRing = 400;
		int32 Total = 0;
		int32 Hidden = 0;
		for (int32 Ring = 0; Ring < Rings; ++Ring)
		{
			// Equal-area rings, so the middle of the disc is not oversampled.
			const double R = Star->AngularRadiusRadians
				* FMath::Sqrt((Ring + 0.5) / Rings);
			for (int32 Step = 0; Step < PerRing; ++Step)
			{
				const double Angle = LedgerTwoPi * Step / PerRing;
				const FVector3d Point = (Centre
					+ Across * (R * FMath::Cos(Angle))
					+ Up * (R * FMath::Sin(Angle))).GetSafeNormal();
				++Total;

				for (const FLedgerSkyBody& Body : Seen)
				{
					if (Body.BodyIndex == 0 || Body.DistanceMetres >= Star->DistanceMetres)
					{
						continue;
					}
					const double Separation = FMath::Acos(FMath::Clamp(
						FVector3d::DotProduct(Point, Body.DirectionInSurface), -1.0, 1.0));
					if (Separation < Body.AngularRadiusRadians)
					{
						++Hidden;
						break;
					}
				}
			}
		}
		return Total > 0 ? static_cast<double>(Hidden) / Total : 0.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerEclipsePredicted,
	"Ledger.Eclipse.APredictedEclipseIsThereAtThePredictedTime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerEclipsePredicted::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const FVector3d Anchor = EclipseAnchor(12.0, 40.0);

	const double Year = LedgerEphemeris::PeriodSeconds(
		System.Bodies[0].MassKg, System.Bodies[Planet].Orbit.SemiMajorAxisMetres);

	double Coverage = 0.0;
	const double When = LedgerSky::NextEclipse(
		System, Planet, Anchor, 0.0, Year * 2.0, Coverage);

	if (When < 0.0)
	{
		AddError(TEXT("no eclipse found from this site in two years, which for a moon "
					  "that subtends 1.18 of the star means the search is wrong"));
		return false;
	}

	const int32 What = LedgerSky::EclipsingBody(System, Planet, Anchor, When);
	AddInfo(FString::Printf(
		TEXT("predicted: body %d covers %.1f%% of the star at t = %.0f s (%.3f years)"),
		What, Coverage * 100.0, When, When / Year));

	// **The observation.** Sampled across the star's disc, 80,000 points, with
	// no overlap formula anywhere in it.
	const double Sampled = SampledCoverage(System, Planet, Anchor, When);
	AddInfo(FString::Printf(
		TEXT("observed at that moment: %.4f of the disc, against a predicted %.4f"),
		Sampled, Coverage));
	TestTrue(*FString::Printf(
		TEXT("the prediction matches what is in the sky (%.4f against %.4f)"),
		Sampled, Coverage),
		FMath::Abs(Sampled - Coverage) < 0.01);

	// And it is an eclipse rather than a graze. How deep the FIRST one happens
	// to be is an accident of the site and the epoch, so this asks only that it
	// is worth the name; how deep they get is reported below.
	TestTrue(*FString::Printf(TEXT("it is a real eclipse (%.1f%% covered)"),
		Coverage * 100.0), Coverage > 0.05);

	// The deepest in two years, which is the one worth standing outside for.
	{
		double Deepest = 0.0;
		double DeepestAt = -1.0;
		double Cursor = 0.0;
		int32 Found = 0;
		while (Cursor >= 0.0 && Cursor < Year * 2.0)
		{
			double Peak = 0.0;
			const double At = LedgerSky::NextEclipse(
				System, Planet, Anchor, Cursor, Year * 2.0 - Cursor, Peak);
			if (At < 0.0)
			{
				break;
			}
			++Found;
			if (Peak > Deepest)
			{
				Deepest = Peak;
				DeepestAt = At;
			}
			// Past the end of this one before looking for the next.
			Cursor = At + 4.0 * 3600.0;
		}
		AddInfo(FString::Printf(
			TEXT("%d eclipses visible from this site in two years; the deepest covers "
				 "%.1f%% at t = %.0f s"), Found, Deepest * 100.0, DeepestAt));

		// **Not a total one, and that is right.** A fixed point on a planet
		// sees partial eclipses often and totality almost never: the shadow's
		// dark core is a track a few hundred kilometres wide crossing a globe
		// forty thousand round, so most of the planet is beside it every time.
		// Earth's figure for one spot is centuries. Asserting a total eclipse
		// at one site in two years would be asserting something false about
		// orbital mechanics and would have been "fixed" by breaking the search.
		TestTrue(*FString::Printf(
			TEXT("the deepest of %d eclipses is a substantial one (%.1f%%)"),
			Found, Deepest * 100.0),
			Deepest > 0.2);

		// What CAN be asserted cheaply is that totality exists somewhere: the
		// occulter has to be able to cover the star, which is a comparison of
		// two angular radii and not a search.
		TArray<FLedgerSkyBody> Seen;
		LedgerSky::VisibleBodies(System, Planet, Anchor, DeepestAt, Seen);
		const FLedgerSkyBody* Star = Seen.FindByPredicate(
			[](const FLedgerSkyBody& B) { return B.BodyIndex == 0; });
		// The body that actually did the eclipsing, not "body 2". With T084's
		// systems the occulter varies, and asking for a fixed index measured
		// something else entirely: it reported a subtended ratio of 0.006 for a
		// body that was nowhere near the sun.
		const int32 Occulter = LedgerSky::EclipsingBody(System, Planet, Anchor, DeepestAt);
		const FLedgerSkyBody* Moon = Seen.FindByPredicate(
			[Occulter](const FLedgerSkyBody& B) { return B.BodyIndex == Occulter; });
		if (Star != nullptr && Moon != nullptr)
		{
			const double Ratio = Moon->AngularRadiusRadians / Star->AngularRadiusRadians;
			AddInfo(FString::Printf(
				TEXT("at that moment the moon subtends %.3f of the star, so the shadow "
					 "has a dark core and totality exists somewhere along it"), Ratio));
			TestTrue(*FString::Printf(
				TEXT("the moon can cover the star (%.3f)"), Ratio), Ratio > 1.0);
		}
	}

	// It is over within a few hours, which is what makes the prediction worth
	// anything: a thing that lasted a week would not need predicting.
	const double Before = LedgerSky::StarCoveredFraction(
		System, Planet, Anchor, When - 6.0 * 3600.0);
	const double After = LedgerSky::StarCoveredFraction(
		System, Planet, Anchor, When + 6.0 * 3600.0);
	AddInfo(FString::Printf(
		TEXT("six hours either side: %.4f before, %.4f after"), Before, After));
	TestTrue(TEXT("the eclipse is brief"), Before < 0.01 && After < 0.01);

	// And the peak really is a peak.
	for (const double Offset : { -1800.0, -600.0, 600.0, 1800.0 })
	{
		const double Near =
			LedgerSky::StarCoveredFraction(System, Planet, Anchor, When + Offset);
		TestTrue(*FString::Printf(
			TEXT("%+.0f s from the prediction is no darker than the prediction "
				 "(%.4f against %.4f)"), Offset, Near, Coverage),
			Near <= Coverage + 1e-9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerEclipseGeometry,
	"Ledger.Eclipse.CoverageMatchesTheDiscGeometry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerEclipseGeometry::RunTest(const FString&)
{
	// The overlap formula against sampling, right through an eclipse rather
	// than at its peak: a formula can be right at totality and wrong on the way
	// in, and the way in is most of what anybody sees.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const FVector3d Anchor = EclipseAnchor(12.0, 40.0);
	const double Year = LedgerEphemeris::PeriodSeconds(
		System.Bodies[0].MassKg, System.Bodies[Planet].Orbit.SemiMajorAxisMetres);

	double Coverage = 0.0;
	const double When =
		LedgerSky::NextEclipse(System, Planet, Anchor, 0.0, Year * 2.0, Coverage);
	if (When < 0.0)
	{
		AddError(TEXT("no eclipse to walk through"));
		return false;
	}

	double Worst = 0.0;
	FString Table;
	for (int32 Step = -6; Step <= 6; ++Step)
	{
		const double At = When + Step * 900.0;
		const double Formula = LedgerSky::StarCoveredFraction(System, Planet, Anchor, At);
		const double Sampled = SampledCoverage(System, Planet, Anchor, At);
		Worst = FMath::Max(Worst, FMath::Abs(Formula - Sampled));
		Table += FString::Printf(TEXT("  %+5.0f min: formula %.4f, sampled %.4f\n"),
			Step * 15.0, Formula, Sampled);
	}
	AddInfo(FString::Printf(TEXT("through the eclipse:\n%s"), *Table));
	AddInfo(FString::Printf(TEXT("worst disagreement %.5f of the disc"), Worst));

	TestTrue(*FString::Printf(
		TEXT("the lens formula matches the sampled disc (worst %.5f)"), Worst),
		Worst < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerEclipseNotAtNight,
	"Ledger.Eclipse.NothingIsEclipsedAtNight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerEclipseNotAtNight::RunTest(const FString&)
{
	// A moon passes between a site and the star once a month whatever the site
	// is doing, and half the time that site is facing away. Counting those
	// would put an eclipse on the far side of the planet and darken ground that
	// is already dark.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	constexpr int32 Planet = 1;
	const FVector3d Anchor = EclipseAnchor(12.0, 40.0);

	const double Month = FMath::Abs(System.Bodies[
		LedgerBodies::FirstChildOfKind(System, 1, ELedgerBodyKind::Moon)]
			.RotationPeriodSeconds);
	int32 Night = 0;
	int32 Eclipsed = 0;
	for (int32 Step = 0; Step < 20000; ++Step)
	{
		const double At = Month * 3.0 * Step / 20000.0;
		const double Altitude = LedgerSky::SolarAltitude(System, Planet, Anchor, At);
		if (Altitude > 0.0)
		{
			continue;
		}
		++Night;
		if (LedgerSky::StarCoveredFraction(System, Planet, Anchor, At) > 0.0)
		{
			++Eclipsed;
		}
	}
	AddInfo(FString::Printf(
		TEXT("%d night samples, %d of them reporting an eclipse"), Night, Eclipsed));
	TestEqual(TEXT("nothing is eclipsed while the star is down"), Eclipsed, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
