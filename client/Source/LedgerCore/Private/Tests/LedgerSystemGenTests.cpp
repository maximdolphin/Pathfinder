// Twenty systems, all plausible, none a rearrangement of another. T084.
//
// "Plausible" is not an opinion here. Every rule it checks was built and
// measured by an earlier task in this milestone: the frost line from T079, the
// Roche limit from T080, the Hill sphere from T083, the mass-luminosity
// relation from T076. This task is where they stop being descriptions of one
// system and become constraints on all of them.

#include "LedgerBody.h"
#include "LedgerEphemeris.h"
#include "LedgerSky.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 HowMany = 20;

	// Prefixed for the same reason LedgerGas.cpp's constants are: the module
	// compiles as one translation unit, so a bare SystemGenAstronomicalUnit here would
	// be a second definition of one that already exists in the blob.
	constexpr double SystemGenAstronomicalUnit = 1.495978707e11;

	/// A description of a system compact enough to compare two of.
	FString Fingerprint(const FLedgerSystem& System)
	{
		FString Out;
		for (const FLedgerBody& Body : System.Bodies)
		{
			Out += FString::Printf(TEXT("%d:%.4g:%.4g:%.4g|"),
				static_cast<int32>(Body.Kind), Body.MassKg, Body.RadiusMetres,
				Body.Orbit.SemiMajorAxisMetres);
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSystemGenPlausible,
	"Ledger.SystemGen.TwentySystemsAreAllPhysicallyPlausible",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSystemGenPlausible::RunTest(const FString&)
{
	FString Table;
	int32 Failed = 0;

	for (int32 Which = 0; Which < HowMany; ++Which)
	{
		const uint32 Seed = 20260908u + Which * 7919u;
		const FLedgerSystem System = LedgerBodies::Generate(Seed);

		FString Why;
		const bool bPlausible = LedgerBodies::Plausible(System, Why);

		int32 Planets = 0;
		int32 Giants = 0;
		int32 Moons = 0;
		for (const FLedgerBody& Body : System.Bodies)
		{
			Planets += Body.Kind == ELedgerBodyKind::Planet ? 1 : 0;
			Giants += Body.Kind == ELedgerBodyKind::GasGiant ? 1 : 0;
			Moons += Body.Kind == ELedgerBodyKind::Moon ? 1 : 0;
		}

		const double Luminosity =
			LedgerBodies::StarLuminosityRelative(System.Bodies[0]);
		Table += FString::Printf(
			TEXT("  %2d: %2d bodies, star %.2f solar (%.3f L), %d rocky %d giants "
				 "%d moons, home at %.3f au%s\n"),
			Which, System.Bodies.Num(), System.Bodies[0].MassKg / 1.98847e30,
			Luminosity, Planets, Giants, Moons,
			System.Bodies[1].Orbit.SemiMajorAxisMetres / SystemGenAstronomicalUnit,
			bPlausible ? TEXT("") : *FString::Printf(TEXT("  <-- %s"), *Why));

		if (!bPlausible)
		{
			++Failed;
			AddError(FString::Printf(TEXT("system %d (seed %u): %s"), Which, Seed, *Why));
		}
	}

	AddInfo(FString::Printf(TEXT("twenty systems from twenty seeds:\n%s"), *Table));
	TestEqual(TEXT("every generated system is one that could exist"), Failed, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSystemGenDistinct,
	"Ledger.SystemGen.NoneIsARearrangementOfAnother",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSystemGenDistinct::RunTest(const FString&)
{
	// **Not merely different: differently SHAPED.** Two systems with the same
	// bodies in a different order would pass a naive comparison, which is why
	// the acceptance says "rearrangement" -- so the check is on the multiset of
	// what is there, not on the sequence.
	TArray<FLedgerSystem> Systems;
	TArray<FString> Prints;
	TArray<FString> Sorted;

	for (int32 Which = 0; Which < HowMany; ++Which)
	{
		const FLedgerSystem System = LedgerBodies::Generate(20260908u + Which * 7919u);
		Systems.Add(System);
		Prints.Add(Fingerprint(System));

		TArray<FString> Parts;
		for (const FLedgerBody& Body : System.Bodies)
		{
			Parts.Add(FString::Printf(TEXT("%d:%.4g:%.4g"),
				static_cast<int32>(Body.Kind), Body.MassKg,
				Body.Orbit.SemiMajorAxisMetres));
		}
		Parts.Sort();
		Sorted.Add(FString::Join(Parts, TEXT("|")));
	}

	int32 Identical = 0;
	int32 Rearranged = 0;
	int32 SameSize = 0;
	for (int32 A = 0; A < HowMany; ++A)
	{
		for (int32 B = A + 1; B < HowMany; ++B)
		{
			if (Prints[A] == Prints[B]) { ++Identical; }
			if (Sorted[A] == Sorted[B]) { ++Rearranged; }
			if (Systems[A].Bodies.Num() == Systems[B].Bodies.Num()) { ++SameSize; }
		}
	}

	AddInfo(FString::Printf(
		TEXT("across %d pairs: %d identical, %d rearrangements, %d sharing only a "
			 "body count"),
		HowMany * (HowMany - 1) / 2, Identical, Rearranged, SameSize));

	TestEqual(TEXT("no two systems are the same"), Identical, 0);
	TestEqual(TEXT("and none is a rearrangement of another"), Rearranged, 0);

	// Sharing a body count is expected and is not a rearrangement -- there are
	// only so many counts. Recorded so the number above is not mistaken for a
	// failure.
	TestTrue(TEXT("some systems do share a body count, which is fine"), SameSize > 0);

	// And the variation is structural rather than a jitter on one layout: the
	// counts and the star masses have to actually spread.
	int32 Fewest = TNumericLimits<int32>::Max();
	int32 Most = 0;
	double LightestStar = TNumericLimits<double>::Max();
	double HeaviestStar = 0.0;
	for (const FLedgerSystem& System : Systems)
	{
		Fewest = FMath::Min(Fewest, System.Bodies.Num());
		Most = FMath::Max(Most, System.Bodies.Num());
		LightestStar = FMath::Min(LightestStar, System.Bodies[0].MassKg);
		HeaviestStar = FMath::Max(HeaviestStar, System.Bodies[0].MassKg);
	}
	AddInfo(FString::Printf(
		TEXT("body counts run %d to %d; star masses %.2f to %.2f solar, a "
			 "luminosity range of %.2f to %.2f"),
		Fewest, Most, LightestStar / 1.98847e30, HeaviestStar / 1.98847e30,
		LedgerBodies::StarLuminosityRelative(Systems[0].Bodies[0]),
		LedgerBodies::StarLuminosityRelative(Systems[0].Bodies[0])));
	TestTrue(*FString::Printf(TEXT("the systems differ in size (%d to %d bodies)"),
		Fewest, Most), Most > Fewest);
	TestTrue(TEXT("and in what kind of star they have"),
		HeaviestStar > LightestStar * 1.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSystemGenRules,
	"Ledger.SystemGen.ThePlausibilityRulesActuallyRefuseThings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSystemGenRules::RunTest(const FString&)
{
	// **A checker that says yes to everything is not a checker.** Each rule is
	// shown to bite by breaking a real system in exactly one way and watching
	// it refuse -- otherwise "twenty systems are plausible" is a statement
	// about the function's return value and nothing else.
	FString Why;
	const FLedgerSystem Good = LedgerBodies::Generate(20260908u);
	TestTrue(TEXT("the unbroken system passes"), LedgerBodies::Plausible(Good, Why));

	FString Table;
	// **And refused for the RIGHT reason.** Checking only that a broken system
	// is rejected lets a case pass on somebody else's rule: moving the home
	// world out to four astronomical units was meant to test the habitable
	// zone, and it was refused for landing next to a gas giant instead. True,
	// and not the thing being tested.
	auto Refuses = [this, &Table](
		const TCHAR* What, const TCHAR* Expect, FLedgerSystem Broken) -> bool
	{
		FString Reason;
		const bool bRefused = !LedgerBodies::Plausible(Broken, Reason);
		const bool bRightRule = bRefused && Reason.Contains(Expect);
		Table += FString::Printf(TEXT("  %-34s %s") LINE_TERMINATOR, What,
			!bRefused ? TEXT("ACCEPTED, which is the bug")
				: (bRightRule ? *Reason
					: *FString::Printf(
						TEXT("refused, but for the wrong reason: %s"), *Reason)));
		return bRightRule;
	};

	{
		FLedgerSystem Broken = Good;
		const int32 Giant = LedgerBodies::FirstOfKind(Broken, ELedgerBodyKind::GasGiant);
		Broken.Bodies[Giant].Orbit.SemiMajorAxisMetres = 0.6 * 1.495978707e11;
		TestTrue(TEXT("a gas giant inside the frost line is refused"),
			Refuses(TEXT("gas giant inside the frost line"),
				TEXT("inside the frost line"), Broken));
	}
	{
		FLedgerSystem Broken = Good;
		// Inwards rather than outwards: moving it out past the giants makes it
		// collide with one, and this case is about the zone and not the gaps.
		//
		// **And the moon comes with it.** The stricter check caught this at
		// once: a Hill sphere scales with the orbit, so hauling the home world
		// in to a tenth of an astronomical unit leaves its companion outside a
		// sphere that shrank by seven -- and the moon rule fires before the
		// habitable-zone rule is ever reached. Breaking one thing means
		// breaking exactly one thing.
		const double Shrink = 0.12 * 1.495978707e11
			/ Broken.Bodies[1].Orbit.SemiMajorAxisMetres;
		Broken.Bodies[1].Orbit.SemiMajorAxisMetres *= Shrink;
		for (FLedgerBody& Body : Broken.Bodies)
		{
			if (Body.ParentIndex == 1)
			{
				Body.Orbit.SemiMajorAxisMetres *= Shrink;
			}
		}
		TestTrue(TEXT("a home world outside the habitable zone is refused"),
			Refuses(TEXT("home world too close to the star"),
				TEXT("habitable zone"), Broken));
	}
	{
		FLedgerSystem Broken = Good;
		const int32 Moon = LedgerBodies::FirstChildOfKind(
			Broken, 1, ELedgerBodyKind::Moon);
		Broken.Bodies[Moon].Orbit.SemiMajorAxisMetres = Broken.Bodies[1].RadiusMetres * 1.2;
		TestTrue(TEXT("a moon inside the Roche limit is refused"),
			Refuses(TEXT("moon inside the Roche limit"),
				TEXT("Roche limit"), Broken));
	}
	{
		FLedgerSystem Broken = Good;
		const int32 Moon = LedgerBodies::FirstChildOfKind(
			Broken, 1, ELedgerBodyKind::Moon);
		Broken.Bodies[Moon].Orbit.SemiMajorAxisMetres = 8.0e9;
		TestTrue(TEXT("a moon outside the Hill sphere is refused"),
			Refuses(TEXT("moon the star would steal"),
				TEXT("Hill sphere"), Broken));
	}
	{
		FLedgerSystem Broken = Good;
		Broken.Bodies[1].MassKg *= 400.0;
		TestTrue(TEXT("an impossible density is refused"),
			Refuses(TEXT("a rocky planet denser than iron"),
				TEXT("density"), Broken));
	}
	{
		FLedgerSystem Broken = Good;
		Broken.Bodies[1].Orbit.Eccentricity = 0.7;
		TestTrue(TEXT("a wild eccentricity is refused"),
			Refuses(TEXT("a home world on a comet's orbit"),
				TEXT("eccentricity"), Broken));
	}
	AddInfo(FString::Printf(TEXT("what the rules refuse, and why:\n%s"), *Table));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
