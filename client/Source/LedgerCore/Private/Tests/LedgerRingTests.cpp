// Where a ring can be, and where its shadow falls. T080.

#include "LedgerRings.h"
#include "LedgerSky.h"
#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FVector3d RingAnchor(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerRingsRoche,
	"Ledger.Rings.RingsLieWhereAMoonCannotForm",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerRingsRoche::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const int32 Giant = LedgerBodies::FirstOfKind(System, ELedgerBodyKind::GasGiant);

	const FLedgerRings Rings = LedgerRings::For(System, Giant);
	const FLedgerBody& Body = System.Bodies[Giant];
	const double Roche = LedgerRings::RocheLimitMetres(Body, 920.0);

	AddInfo(FString::Printf(
		TEXT("gas giant radius %.0f km; Roche limit for ice %.0f km (%.2f radii); "
			 "rings from %.0f to %.0f km"),
		Body.RadiusMetres / 1000.0, Roche / 1000.0, Roche / Body.RadiusMetres,
		Rings.InnerRadiusMetres / 1000.0, Rings.OuterRadiusMetres / 1000.0));

	TestTrue(TEXT("the giant has rings"),
		Rings.OuterRadiusMetres > Rings.InnerRadiusMetres);

	// **The outer edge is the Roche limit, not a number.** Beyond it the rubble
	// would have collected into a moon long ago, which is why no ring in the
	// solar system has a substantial one outside its own.
	TestEqual(TEXT("the outer edge is where ice stops being torn apart"),
		Rings.OuterRadiusMetres, Roche, 1.0);

	// Saturn's rings end at about 2.3 of its radius and its Roche limit for ice
	// is about 2.5, so this is the right neighbourhood rather than a coincidence
	// of units.
	const double Radii = Rings.OuterRadiusMetres / Body.RadiusMetres;
	TestTrue(*FString::Printf(TEXT("that is %.2f body radii, as Saturn's is 2.3"), Radii),
		Radii > 1.8 && Radii < 3.2);

	// **A rocky body gets none, and NOT for the reason this test first
	// asserted.** It claimed there was nowhere to put them -- that a rocky
	// body's Roche limit sits barely outside its surface. The opposite is
	// true, and the table below is what said so: the limit scales with the cube
	// root of the PRIMARY's density, so this rocky planet's limit for icy
	// rubble is 4.40 of its radii while the gas giant's is 1.96. Rock has more
	// room for rings than gas does.
	//
	// The real reason is supply. Ring material is ice; ice is not solid inside
	// the frost line; what little reaches an inner planet is swept up or
	// dragged down long before anybody looks. So the condition is about where
	// the body formed, and the Roche limit does the job it actually does --
	// setting the outer edge.
	FString Table;
	for (int32 Index = 1; Index < System.Bodies.Num(); ++Index)
	{
		const FLedgerRings Each = LedgerRings::For(System, Index);
		const double Limit = LedgerRings::RocheLimitMetres(System.Bodies[Index], 920.0);
		Table += FString::Printf(TEXT("  body %d (%s): Roche %.2f radii -> %s\n"),
			Index, LexToString(System.Bodies[Index].Kind),
			Limit / System.Bodies[Index].RadiusMetres,
			Each.OuterRadiusMetres > Each.InnerRadiusMetres ? TEXT("rings") : TEXT("none"));
	}
	AddInfo(FString::Printf(TEXT("who gets rings:\n%s"), *Table));

	TestFalse(TEXT("the inner rocky planet has no rings, for want of ice"),
		LedgerRings::For(System, 1).OuterRadiusMetres
			> LedgerRings::For(System, 1).InnerRadiusMetres);

	// And the Roche limit is bigger there, in radii, which is the fact that
	// corrected the reasoning.
	const double RockyRoche = LedgerRings::RocheLimitMetres(System.Bodies[1], 920.0)
		/ System.Bodies[1].RadiusMetres;
	const double GiantRoche = Roche / Body.RadiusMetres;
	AddInfo(FString::Printf(
		TEXT("the rocky planet's Roche limit is %.2f radii against the giant's %.2f, "
			 "because it is the denser body"), RockyRoche, GiantRoche));
	TestTrue(TEXT("a denser body has the wider Roche limit in its own radii"),
		RockyRoche > GiantRoche);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerRingsShadow,
	"Ledger.Rings.TheShadowSweepsAcrossTheYear",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerRingsShadow::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const int32 Giant = LedgerBodies::FirstOfKind(System, ELedgerBodyKind::GasGiant);
	const FLedgerRings Rings = LedgerRings::For(System, Giant);

	const double Year = LedgerEphemeris::PeriodSeconds(
		System.Bodies[0].MassKg, System.Bodies[Giant].Orbit.SemiMajorAxisMetres);
	const double Tilt = FMath::RadiansToDegrees(System.Bodies[Giant].AxialTiltRadians);
	AddInfo(FString::Printf(TEXT("the giant is tilted %.1f degrees; its year is %.2f "
		"of this system's"), Tilt, Year / LedgerEphemeris::PeriodSeconds(
			System.Bodies[0].MassKg, System.Bodies[1].Orbit.SemiMajorAxisMetres)));

	// Walk a year, and at each point find the band of latitudes the ring
	// shadows. The claim is that it moves -- and that it moves to the winter
	// side, because the ring shades the hemisphere the star is not over.
	FString Table;
	double WidestSpan = 0.0;
	double NarrowestSpan = 1000.0;
	double MostNorthern = -1000.0;
	double MostSouthern = 1000.0;
	FString Dump;

	for (int32 Step = 0; Step < 12; ++Step)
	{
		const double At = Year * Step / 12.0;
		const double Declination = FMath::RadiansToDegrees(
			LedgerSky::SolarDeclination(System, Giant, At));

		double Lowest = 1000.0;
		double Highest = -1000.0;
		double Shadowed = 0.0;
		for (int32 Row = 0; Row <= 360; ++Row)
		{
			const double Latitude = -90.0 + 180.0 * Row / 360.0;
			// Sampled at the longitude facing the star, which is where a ring
			// shadow is deepest and where anybody standing would see it.
			double Best = 0.0;
			for (int32 Column = 0; Column < 24; ++Column)
			{
				Best = FMath::Max(Best, LedgerRings::ShadowAt(
					Rings, System, RingAnchor(Latitude, Column * 15.0), At));
			}
			Dump += FString::Printf(TEXT("%d %.3f %.5f" LINE_TERMINATOR),
				Step, Latitude, Best);
			if (Best > 0.05)
			{
				Lowest = FMath::Min(Lowest, Latitude);
				Highest = FMath::Max(Highest, Latitude);
				Shadowed += 1.0;
			}
		}

		const double Span = Shadowed > 0.0 ? Highest - Lowest : 0.0;
		Table += FString::Printf(
			TEXT("  %4.2f of a year: declination %+6.2f, shadow from %+7.2f to %+7.2f "
				 "(%6.2f degrees wide)\n"),
			Step / 12.0, Declination,
			Shadowed > 0.0 ? Lowest : 0.0, Shadowed > 0.0 ? Highest : 0.0, Span);

		if (Shadowed > 0.0)
		{
			WidestSpan = FMath::Max(WidestSpan, Span);
			NarrowestSpan = FMath::Min(NarrowestSpan, Span);
			MostNorthern = FMath::Max(MostNorthern, Highest);
			MostSouthern = FMath::Min(MostSouthern, Lowest);
		}
	}
	AddInfo(FString::Printf(TEXT("the ring shadow across one year:\n%s"), *Table));
	AddInfo(FString::Printf(
		TEXT("widest %.2f degrees, narrowest %.2f, reaching from %+.2f to %+.2f"),
		WidestSpan, NarrowestSpan, MostSouthern, MostNorthern));

	FFileHelper::SaveStringToFile(Dump,
		*FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("t080-ring-shadow.txt"))));

	// **It moves.** A shadow that sat still would be a decal.
	TestTrue(*FString::Printf(
		TEXT("the shadow reaches both hemispheres (%+.2f to %+.2f)"),
		MostSouthern, MostNorthern),
		MostNorthern > 5.0 && MostSouthern < -5.0);

	// **And it changes width.** Wide when the star is far out of the ring
	// plane, narrow when it is in it, which is why Saturn's shadow is a band in
	// its winter and a line at its equinox.
	TestTrue(*FString::Printf(
		TEXT("the shadow's width changes across the year (%.2f to %.2f degrees)"),
		NarrowestSpan, WidestSpan),
		WidestSpan > NarrowestSpan * 1.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerRingsGaps,
	"Ledger.Rings.ResonancesWithAMoonClearGaps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerRingsGaps::RunTest(const FString&)
{
	// A gap has to be somewhere a moon put it, or it is a stripe.
	//
	// The generated giant has no moon of its own, so this builds one: a body
	// orbiting the giant close enough that its resonances fall inside the ring.
	FLedgerSystem System = LedgerBodies::Generate(20260908u);
	const int32 Giant = LedgerBodies::FirstOfKind(System, ELedgerBodyKind::GasGiant);
	const FLedgerRings Rings = LedgerRings::For(System, Giant);

	FLedgerBody Shepherd;
	Shepherd.Kind = ELedgerBodyKind::Moon;
	Shepherd.MassKg = 1.0e19;
	Shepherd.RadiusMetres = 8.0e4;
	Shepherd.ParentIndex = Giant;
	// Placed so its 2:1 resonance lands inside the ring: a 2:1 in period is a
	// radius ratio of 2^(2/3), so the moon goes that much further out than the
	// gap it will clear.
	const double Middle =
		(Rings.InnerRadiusMetres + Rings.OuterRadiusMetres) * 0.5;
	Shepherd.Orbit.SemiMajorAxisMetres = Middle * FMath::Pow(2.0, 2.0 / 3.0);
	System.Bodies.Add(Shepherd);

	const int32 Samples = 4000;
	int32 InGaps = 0;
	TArray<double> GapCentres;
	bool bWasGap = false;
	for (int32 Step = 0; Step <= Samples; ++Step)
	{
		const double Radius = Rings.InnerRadiusMetres
			+ (Rings.OuterRadiusMetres - Rings.InnerRadiusMetres) * Step / Samples;
		const bool bGap = LedgerRings::InGap(Rings, System, Radius);
		if (bGap)
		{
			++InGaps;
			if (!bWasGap)
			{
				GapCentres.Add(Radius);
			}
		}
		bWasGap = bGap;
	}

	AddInfo(FString::Printf(
		TEXT("%d gaps across the ring, covering %.2f%% of its width"),
		GapCentres.Num(), 100.0 * InGaps / (Samples + 1)));
	for (const double Centre : GapCentres)
	{
		AddInfo(FString::Printf(TEXT("  a gap starting at %.0f km (%.3f of the way out)"),
			Centre / 1000.0,
			(Centre - Rings.InnerRadiusMetres)
				/ (Rings.OuterRadiusMetres - Rings.InnerRadiusMetres)));
	}

	TestTrue(TEXT("the shepherd clears at least one gap"), GapCentres.Num() >= 1);
	TestTrue(*FString::Printf(
		TEXT("the gaps are gaps rather than most of the ring (%.2f%%)"),
		100.0 * InGaps / (Samples + 1)),
		InGaps < (Samples + 1) / 3);

	// And the optical depth really is zero there, since a gap that still blocks
	// light is a change of shade.
	for (const double Centre : GapCentres)
	{
		TestEqual(TEXT("a gap passes light"),
			LedgerRings::OpticalDepthAt(Rings, System, Centre + 1.0), 0.0, 1e-12);
	}

	// Without the moon there are no gaps at all, which is the check that they
	// come from it rather than from a pattern.
	const FLedgerSystem Bare = LedgerBodies::Generate(20260908u);
	int32 BareGaps = 0;
	for (int32 Step = 0; Step <= Samples; ++Step)
	{
		const double Radius = Rings.InnerRadiusMetres
			+ (Rings.OuterRadiusMetres - Rings.InnerRadiusMetres) * Step / Samples;
		if (LedgerRings::InGap(Rings, Bare, Radius)) { ++BareGaps; }
	}
	AddInfo(FString::Printf(TEXT("with no moon of its own, the ring has %d gap samples"),
		BareGaps));
	TestEqual(TEXT("no moon, no gaps"), BareGaps, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
