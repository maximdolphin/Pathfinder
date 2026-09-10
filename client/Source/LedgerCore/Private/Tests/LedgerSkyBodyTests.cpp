// A moon's phase, from anywhere. T074.
//
// The acceptance names two computations and asks whether they agree: the phase
// the sun-moon-observer geometry implies, and the phase the moon actually
// shows. So one of them is `(1 + cos a) / 2` and the other counts lit ground on
// a sampled sphere, and they share nothing but the answer.

#include "LedgerSky.h"
#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FVector3d SkyBodyAnchor(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat));
	}

	/// The lit fraction of a target's visible disc, by looking at the target.
	///
	/// Points spread evenly over the target's sphere; each one is lit if it
	/// faces the star and visible if it faces the observer, and it contributes
	/// to the disc in proportion to how square-on it is. No phase angle appears
	/// anywhere in here, which is the point -- this is the geometry, and the
	/// formula under test is a claim about it.
	double SampledLitFraction(const FLedgerSystem& System, int32 TargetIndex,
		const FVector3d& Eye, double SecondsFromEpoch)
	{
		const int32 Star = 0;
		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, SecondsFromEpoch, States);

		const FVector3d Centre = States[TargetIndex].PositionMetres;
		const FVector3d StarAt = States[Star].PositionMetres;
		const double Radius = System.Bodies[TargetIndex].RadiusMetres;

		constexpr int32 Samples = 200000;
		const double Golden = LedgerPi * (3.0 - FMath::Sqrt(5.0));

		double Visible = 0.0;
		double Lit = 0.0;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			// Fibonacci sphere: even coverage without a pole clump, which a
			// latitude-longitude grid would give and which would bias whichever
			// hemisphere happened to hold the poles.
			const double Z = 1.0 - 2.0 * (Index + 0.5) / Samples;
			const double R = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
			const double Angle = Golden * Index;
			const FVector3d Normal(R * FMath::Cos(Angle), R * FMath::Sin(Angle), Z);

			const FVector3d Point = Centre + Normal * Radius;
			const FVector3d ToEye = (Eye - Point).GetSafeNormal();
			const FVector3d ToStar = (StarAt - Point).GetSafeNormal();

			const double Facing = FVector3d::DotProduct(Normal, ToEye);
			if (Facing <= 0.0)
			{
				continue;
			}
			Visible += Facing;
			if (FVector3d::DotProduct(Normal, ToStar) > 0.0)
			{
				Lit += Facing;
			}
		}
		return Visible > 0.0 ? Lit / Visible : 0.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSkyPhaseMatchesGeometry,
	"Ledger.SkyBody.PhaseMatchesTheSunTargetObserverGeometry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSkyPhaseMatchesGeometry::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	AddInfo(FString::Printf(TEXT("system has %d bodies"), System.Bodies.Num()));

	const FVector3d Anchor = SkyBodyAnchor(31.0, 77.0);

	// A month's worth of moments, and one much later, so this is not a claim
	// about a convenient configuration.
	const double Month = System.Bodies.IsValidIndex(2)
		? FMath::Abs(System.Bodies[2].RotationPeriodSeconds) : 2.4e6;
	const double Times[] = {
		0.0, Month * 0.125, Month * 0.25, Month * 0.375, Month * 0.5,
		Month * 0.625, Month * 0.75, Month * 0.875, Month * 11.3, 4.7e8 };

	double WorstGap = 0.0;
	FString WorstWhere;
	int32 Checked = 0;

	// From every body that is not the star, at every one of those moments,
	// looking at every other body that is not the star. "From any body in the
	// system" is the acceptance, so it is iterated rather than chosen.
	for (int32 Observer = 1; Observer < System.Bodies.Num(); ++Observer)
	{
		for (int32 Target = 1; Target < System.Bodies.Num(); ++Target)
		{
			if (Target == Observer)
			{
				continue;
			}
			for (const double At : Times)
			{
				const FVector3d Eye =
					LedgerSky::ObserverPosition(System, Observer, Anchor, At);

				const double Angle = LedgerSky::PhaseAngle(System, Target, Eye, At);
				const double Formula = LedgerSky::IlluminatedFraction(Angle);
				const double Sampled = SampledLitFraction(System, Target, Eye, At);

				const double Gap = FMath::Abs(Formula - Sampled);
				if (Gap > WorstGap)
				{
					WorstGap = Gap;
					WorstWhere = FString::Printf(
						TEXT("body %d seen from body %d at t=%.3g s, phase angle %.1f deg, "
							 "formula %.5f against sampled %.5f"),
						Target, Observer, At, FMath::RadiansToDegrees(Angle),
						Formula, Sampled);
				}
				++Checked;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d observer/target/time combinations checked"), Checked));
	AddInfo(FString::Printf(TEXT("worst disagreement %.6f -- %s"), WorstGap, *WorstWhere));

	// **The residual is real and is not error.** The formula is the far-field
	// one: it assumes the observer sees exactly a hemisphere. A real observer a
	// finite distance away sees slightly less than one, by about the ratio of
	// the target's radius to the distance, and the sampling knows that because
	// it uses the actual positions. A per-cent of the disc is the scale of that
	// effect at this system's distances, and anything much larger would be a
	// mistake rather than a projection.
	TestTrue(*FString::Printf(
		TEXT("the phase formula matches the geometry (worst %.6f of the disc)"), WorstGap),
		WorstGap < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSkyPhaseEnds,
	"Ledger.SkyBody.FullAtOppositionAndNewAtConjunction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSkyPhaseEnds::RunTest(const FString&)
{
	// The two configurations anybody can check by eye, and the direction of
	// travel between them. A phase that ran backwards would still satisfy the
	// agreement test above, because both computations would run backwards.
	TestEqual(TEXT("phase angle zero is full"),
		LedgerSky::IlluminatedFraction(0.0), 1.0, 1e-12);
	TestEqual(TEXT("a right angle is half"),
		LedgerSky::IlluminatedFraction(LedgerPi * 0.5), 0.5, 1e-12);
	TestEqual(TEXT("phase angle pi is new"),
		LedgerSky::IlluminatedFraction(LedgerPi), 0.0, 1e-12);

	double Previous = 2.0;
	for (int32 Step = 0; Step <= 100; ++Step)
	{
		const double Angle = LedgerPi * Step / 100.0;
		const double Fraction = LedgerSky::IlluminatedFraction(Angle);
		TestTrue(*FString::Printf(
			TEXT("the lit fraction falls as the phase angle opens (%.4f at %.1f deg)"),
			Fraction, FMath::RadiansToDegrees(Angle)),
			Fraction <= Previous + 1e-12);
		Previous = Fraction;
	}

	// And a real one: over a month, the moon seen from the planet must pass
	// through both a nearly-full and a nearly-new phase, or the geometry is not
	// producing a cycle at all.
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	if (!System.Bodies.IsValidIndex(2))
	{
		AddInfo(TEXT("no moon in this system; the cycle check is skipped"));
		return true;
	}

	const FVector3d Anchor = SkyBodyAnchor(0.0, 0.0);
	const double Month = FMath::Abs(System.Bodies[2].RotationPeriodSeconds);
	double Brightest = 0.0;
	double Darkest = 1.0;
	for (int32 Step = 0; Step < 400; ++Step)
	{
		const double At = Month * Step / 400.0;
		const FVector3d Eye = LedgerSky::ObserverPosition(System, 1, Anchor, At);
		const double Fraction =
			LedgerSky::IlluminatedFraction(LedgerSky::PhaseAngle(System, 2, Eye, At));
		Brightest = FMath::Max(Brightest, Fraction);
		Darkest = FMath::Min(Darkest, Fraction);
	}
	AddInfo(FString::Printf(
		TEXT("across one month the moon runs from %.4f lit to %.4f lit"),
		Darkest, Brightest));
	TestTrue(*FString::Printf(TEXT("the moon gets nearly full (%.4f)"), Brightest),
		Brightest > 0.9);
	TestTrue(*FString::Printf(TEXT("the moon gets nearly new (%.4f)"), Darkest),
		Darkest < 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSkyApparentSize,
	"Ledger.SkyBody.ApparentSizeAndPlaceAreConsistent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSkyApparentSize::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const FVector3d Anchor = SkyBodyAnchor(-18.0, 204.0);

	TArray<FLedgerSkyBody> Seen;
	LedgerSky::VisibleBodies(System, 1, Anchor, 1.0e6, Seen);

	AddInfo(FString::Printf(TEXT("%d bodies in the sky from the planet"), Seen.Num()));
	TestEqual(TEXT("every body but the observer's own is listed"),
		Seen.Num(), System.Bodies.Num() - 1);

	for (const FLedgerSkyBody& Body : Seen)
	{
		AddInfo(FString::Printf(
			TEXT("body %d: %.4f deg across, %.1f%% lit, %.4g m away, altitude %+.1f deg"),
			Body.BodyIndex, FMath::RadiansToDegrees(Body.AngularRadiusRadians * 2.0),
			Body.IlluminatedFraction * 100.0, Body.DistanceMetres,
			FMath::RadiansToDegrees(FMath::Asin(
				FMath::Clamp(Body.DirectionInSurface.Z, -1.0, 1.0)))));

		TestTrue(*FString::Printf(TEXT("body %d has a direction"), Body.BodyIndex),
			FMath::IsNearlyEqual(Body.DirectionInSurface.Length(), 1.0, 1e-9));

		// The angular size has to be the one the distance implies, or a moon
		// drawn at the right place is drawn the wrong size.
		const double Expected = FMath::Asin(FMath::Clamp(
			System.Bodies[Body.BodyIndex].RadiusMetres / Body.DistanceMetres, 0.0, 1.0));
		TestEqual(*FString::Printf(TEXT("body %d subtends what its distance says"),
			Body.BodyIndex), Body.AngularRadiusRadians, Expected, 1e-12);

		TestTrue(*FString::Printf(TEXT("body %d is somewhere real"), Body.BodyIndex),
			Body.DistanceMetres > 0.0 && Body.IlluminatedFraction >= 0.0
				&& Body.IlluminatedFraction <= 1.0);
	}

	// The order is by apparent lit area, and it says so rather than pretending
	// to be brightness.
	//
	// **It nearly shipped as "the star dominates the sky", which passed.** It
	// passed because the moon happened to be 0.5% lit at the moment chosen --
	// and the moon is the LARGER disc from here, 0.58 degrees against the
	// star's 0.49, so a full one sorts first. That assertion would have been
	// true at that timestamp and false a fortnight later, which is worse than
	// no assertion at all.
	//
	// Real brightness needs the star's luminosity, the inverse square and each
	// body's albedo. That is T076, and until it exists this list is ordered by
	// how much lit disc a thing shows, which is a geometry question this task
	// can actually answer.
	for (int32 Index = 1; Index < Seen.Num(); ++Index)
	{
		const double Before = Seen[Index - 1].AngularRadiusRadians
			* Seen[Index - 1].AngularRadiusRadians * Seen[Index - 1].IlluminatedFraction;
		const double After = Seen[Index].AngularRadiusRadians
			* Seen[Index].AngularRadiusRadians * Seen[Index].IlluminatedFraction;
		TestTrue(*FString::Printf(
			TEXT("body %d shows at least as much lit disc as body %d"),
			Seen[Index - 1].BodyIndex, Seen[Index].BodyIndex),
			Before >= After - 1e-18);
	}

	// And the thing worth noticing in those numbers: from this planet the moon
	// subtends MORE than the star does, so this system can have total eclipses.
	// T075 is where that stops being a coincidence and starts being a date.
	const FLedgerSkyBody* Star = Seen.FindByPredicate(
		[](const FLedgerSkyBody& B) { return B.BodyIndex == 0; });
	const FLedgerSkyBody* Moon = Seen.FindByPredicate(
		[](const FLedgerSkyBody& B) { return B.BodyIndex == 2; });
	if (Star != nullptr && Moon != nullptr)
	{
		AddInfo(FString::Printf(
			TEXT("the moon subtends %.3f of the star's disc, so totality is %s"),
			Moon->AngularRadiusRadians / Star->AngularRadiusRadians,
			Moon->AngularRadiusRadians > Star->AngularRadiusRadians
				? TEXT("possible") : TEXT("not possible from here")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
