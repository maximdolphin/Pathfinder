// A system description round-trips through serialisation. T069.
//
// "Rebuilds identically" is the acceptance, and identical means the same bits.
// A tolerance would let this pass while a planet drifted a hundred metres every
// time the file was written -- at one astronomical unit, the fifteenth
// significant digit of a double is about that far.

#include "LedgerBody.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSystemRoundTrips,
	"Ledger.Body.SystemRoundTripsExactly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSystemRoundTrips::RunTest(const FString&)
{
	// Several seeds, because one seed is one shape of number and the digits
	// that fail to survive a round trip are the awkward ones.
	for (const uint32 Seed : { 1u, 20260908u, 0xFFFFFFFFu, 7u })
	{
		const FString Which = FString::Printf(TEXT("seed %u"), Seed);
		const FLedgerSystem Built = LedgerBodies::Generate(Seed);

		FString Error;
		if (!TestTrue(*(Which + TEXT(": generates a valid system")),
			LedgerBodies::Validate(Built, Error)))
		{
			AddError(Error);
			continue;
		}

		const FString Json = LedgerBodies::ToJson(Built);

		FLedgerSystem Rebuilt;
		if (!TestTrue(*(Which + TEXT(": parses")),
			LedgerBodies::FromJson(Json, Rebuilt, Error)))
		{
			AddError(Error);
			continue;
		}

		// The whole point, and it is an exact comparison.
		TestTrue(*(Which + TEXT(": rebuilds identically")), Rebuilt == Built);

		// And writing it again produces the same text, which is the property
		// that makes a system description diffable in version control.
		TestEqual(*(Which + TEXT(": re-serialises byte for byte")),
			LedgerBodies::ToJson(Rebuilt), Json);
	}

	// Determinism: the same seed anywhere, forever.
	TestTrue(TEXT("the same seed is the same system"),
		LedgerBodies::Generate(42u) == LedgerBodies::Generate(42u));
	TestFalse(TEXT("a different seed is a different system"),
		LedgerBodies::Generate(42u) == LedgerBodies::Generate(43u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSystemValidates,
	"Ledger.Body.ValidateRejectsImpossibleSystems",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSystemValidates::RunTest(const FString&)
{
	// A file can be valid JSON describing an impossible system, and saying
	// which of those went wrong is the difference between a five-minute fix
	// and an afternoon.
	auto Rejects = [this](const TCHAR* What, TFunctionRef<void(FLedgerSystem&)> Break)
	{
		FLedgerSystem System = LedgerBodies::Generate(1u);
		Break(System);
		FString Error;
		const bool bValid = LedgerBodies::Validate(System, Error);
		TestFalse(What, bValid);
		if (!bValid)
		{
			TestTrue(*FString::Printf(TEXT("%s: says why"), What), !Error.IsEmpty());
		}
	};

	Rejects(TEXT("no bodies"), [](FLedgerSystem& S) { S.Bodies.Reset(); });
	Rejects(TEXT("a body with no mass"), [](FLedgerSystem& S) { S.Bodies[1].MassKg = 0.0; });
	Rejects(TEXT("a body with no radius"), [](FLedgerSystem& S) { S.Bodies[1].RadiusMetres = 0.0; });
	Rejects(TEXT("an escape trajectory"), [](FLedgerSystem& S) { S.Bodies[1].Orbit.Eccentricity = 1.0; });
	Rejects(TEXT("a body orbiting itself"), [](FLedgerSystem& S) { S.Bodies[1].ParentIndex = 1; });
	Rejects(TEXT("a parent that does not exist"), [](FLedgerSystem& S) { S.Bodies[1].ParentIndex = 99; });
	Rejects(TEXT("two primaries"), [](FLedgerSystem& S) { S.Bodies[1].ParentIndex = INDEX_NONE; });
	Rejects(TEXT("a cycle"), [](FLedgerSystem& S)
	{
		S.Bodies[0].ParentIndex = 2;
		S.Bodies[1].ParentIndex = 0;
		S.Bodies[2].ParentIndex = 1;
	});

	// And a valid one still passes, so the checks above are not just refusing
	// everything.
	FString Error;
	TestTrue(TEXT("a generated system is valid"),
		LedgerBodies::Validate(LedgerBodies::Generate(1u), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSystemRejectsRubbish,
	"Ledger.Body.FromJsonRefusesMalformedText",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSystemRejectsRubbish::RunTest(const FString&)
{
	auto Refuses = [this](const TCHAR* What, const FString& Json)
	{
		FLedgerSystem Out;
		FString Error;
		TestFalse(What, LedgerBodies::FromJson(Json, Out, Error));
	};

	Refuses(TEXT("empty"), FString());
	Refuses(TEXT("not JSON"), TEXT("Home, 1 AU, tilted a bit"));
	Refuses(TEXT("truncated"), LedgerBodies::ToJson(LedgerBodies::Generate(1u)).Left(120));
	Refuses(TEXT("an unknown body kind"),
		LedgerBodies::ToJson(LedgerBodies::Generate(1u)).Replace(TEXT("\"planet\""), TEXT("\"ringworld\"")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
