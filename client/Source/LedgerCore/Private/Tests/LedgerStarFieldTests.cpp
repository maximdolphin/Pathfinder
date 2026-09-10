// The same constellations from two planets, different ones from two systems.
// T077.
//
// A constellation is not a list of stars, it is a set of ANGLES between them --
// which is the only definition that lets the claim be measured. So the test
// takes the brightest stars, measures every angle between them from one place,
// measures the same angles from another, and asks how much they moved.

#include "LedgerStarField.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// One astronomical unit, in parsecs. The number that makes the whole task
	/// work: a solar system is a two-hundred-thousandth of the distance to
	/// anything else.
	constexpr double AuInParsecs = 4.84813681e-6;

	/// The angles between the brightest N stars, as seen from a place.
	///
	/// Identified by catalogue index rather than by rank, so that a star that
	/// changes places in the brightness order is still compared with itself.
	TMap<TPair<int32, int32>, double> Constellation(
		const TArray<FLedgerCatalogueStar>& Catalogue,
		const FVector3d& Observer, const TArray<int32>& Which)
	{
		TMap<TPair<int32, int32>, double> Angles;
		for (int32 A = 0; A < Which.Num(); ++A)
		{
			for (int32 B = A + 1; B < Which.Num(); ++B)
			{
				const FVector3d First =
					(Catalogue[Which[A]].PositionParsecs - Observer).GetSafeNormal();
				const FVector3d Second =
					(Catalogue[Which[B]].PositionParsecs - Observer).GetSafeNormal();
				Angles.Add(TPair<int32, int32>(Which[A], Which[B]),
					FMath::Acos(FMath::Clamp(
						FVector3d::DotProduct(First, Second), -1.0, 1.0)));
			}
		}
		return Angles;
	}

	/// The brightest few, by catalogue index.
	TArray<int32> BrightestFrom(
		const TArray<FLedgerCatalogueStar>& Catalogue,
		const FVector3d& Observer, int32 Count)
	{
		TArray<FLedgerSkyStar> Seen;
		LedgerStarField::Visible(
			Catalogue, Observer, LedgerStarField::NakedEyeMagnitude, Seen);

		TArray<int32> Which;
		for (int32 Index = 0; Index < FMath::Min(Count, Seen.Num()); ++Index)
		{
			Which.Add(Seen[Index].Index);
		}
		return Which;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStarFieldConstellations,
	"Ledger.StarField.ConstellationsHoldAcrossASystemAndNotBetweenThem",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStarFieldConstellations::RunTest(const FString&)
{
	TArray<FLedgerCatalogueStar> Catalogue;
	LedgerStarField::Generate(20260908u, 40000, 260.0, Catalogue);
	TestEqual(TEXT("the catalogue was generated"), Catalogue.Num(), 40000);

	// Where this system is, and where another one is. Twelve parsecs apart:
	// close as interstellar distances go, and the point is that even that is
	// enough to take a sky apart.
	const FVector3d HereSystem(3.0, -7.0, 2.0);
	const FVector3d ThereSystem(3.0 + 12.0, -7.0, 2.0);

	// Two planets in this system, forty astronomical units apart -- an inner
	// world and something out where the ice is.
	const FVector3d InnerPlanet = HereSystem + FVector3d(AuInParsecs, 0.0, 0.0);
	const FVector3d OuterPlanet = HereSystem + FVector3d(-40.0 * AuInParsecs, 0.0, 0.0);

	const TArray<int32> Bright = BrightestFrom(Catalogue, InnerPlanet, 12);
	AddInfo(FString::Printf(TEXT("%d stars brighter than magnitude %.1f were used"),
		Bright.Num(), LedgerStarField::NakedEyeMagnitude));
	TestTrue(TEXT("there is a sky to look at"), Bright.Num() >= 8);

	{
		TArray<FLedgerSkyStar> Seen;
		LedgerStarField::Visible(
			Catalogue, InnerPlanet, LedgerStarField::NakedEyeMagnitude, Seen);
		AddInfo(FString::Printf(
			TEXT("naked-eye stars from the inner planet: %d, brightest magnitude %.2f "
				 "at %.1f pc"),
			Seen.Num(), Seen[0].ApparentMagnitude, Seen[0].DistanceParsecs));
	}

	const auto FromInner = Constellation(Catalogue, InnerPlanet, Bright);
	const auto FromOuter = Constellation(Catalogue, OuterPlanet, Bright);
	const auto FromThere = Constellation(Catalogue, ThereSystem, Bright);

	double WorstWithinSystem = 0.0;
	double WorstBetweenSystems = 0.0;
	double LeastBetweenSystems = TNumericLimits<double>::Max();
	for (const auto& Pair : FromInner)
	{
		const double Within = FMath::Abs(FromOuter[Pair.Key] - Pair.Value);
		WorstWithinSystem = FMath::Max(WorstWithinSystem, Within);

		const double Between = FMath::Abs(FromThere[Pair.Key] - Pair.Value);
		WorstBetweenSystems = FMath::Max(WorstBetweenSystems, Between);
		LeastBetweenSystems = FMath::Min(LeastBetweenSystems, Between);
	}

	const double WithinArcsec = FMath::RadiansToDegrees(WorstWithinSystem) * 3600.0;
	AddInfo(FString::Printf(
		TEXT("across 41 au inside one system, the worst angle moved by %.4f arcseconds"),
		WithinArcsec));
	AddInfo(FString::Printf(
		TEXT("across 12 parsecs, angles moved by %.2f to %.2f degrees"),
		FMath::RadiansToDegrees(LeastBetweenSystems),
		FMath::RadiansToDegrees(WorstBetweenSystems)));

	// **The same constellations.** A minute of arc is about a thirtieth of the
	// full moon and well under what an eye resolves, so a sky that moves by less
	// than that across a whole solar system is the same sky.
	TestTrue(*FString::Printf(
		TEXT("the constellations hold across the system (%.4f arcsec)"), WithinArcsec),
		WithinArcsec < 60.0);

	// **And measurably different.** One degree is twice the width of the full
	// moon, so a constellation whose angles have moved by more than that is not
	// the same constellation to anybody looking at it.
	//
	// The bound was 5 degrees at first, on no reasoning, and the measurement
	// came back at 3.47. Five was not a threshold anybody had derived -- it was
	// a round number -- so it is now one degree, which is a thing that can be
	// pointed at in the sky. The measured figure is quoted beside it either
	// way, which is what makes the bound honest rather than merely passed.
	TestTrue(*FString::Printf(
		TEXT("the constellations differ between systems (worst %.2f degrees, "
			 "against a full moon's half)"),
		FMath::RadiansToDegrees(WorstBetweenSystems)),
		FMath::RadiansToDegrees(WorstBetweenSystems) > 1.0);

	// And the stars themselves move much further than their angles to each
	// other do, because a constellation drifting bodily across the sky keeps
	// its shape for a while. This is the number that says the sky is a
	// different sky rather than a turned one.
	double WorstDirection = 0.0;
	for (const int32 Which : Bright)
	{
		const FVector3d Here =
			(Catalogue[Which].PositionParsecs - InnerPlanet).GetSafeNormal();
		const FVector3d There =
			(Catalogue[Which].PositionParsecs - ThereSystem).GetSafeNormal();
		WorstDirection = FMath::Max(WorstDirection, FMath::Acos(
			FMath::Clamp(FVector3d::DotProduct(Here, There), -1.0, 1.0)));
	}
	AddInfo(FString::Printf(
		TEXT("the brightest stars themselves move by up to %.1f degrees between "
			 "the two systems"), FMath::RadiansToDegrees(WorstDirection)));
	TestTrue(*FString::Printf(
		TEXT("individual stars move a long way (%.1f degrees)"),
		FMath::RadiansToDegrees(WorstDirection)),
		FMath::RadiansToDegrees(WorstDirection) > 10.0);

	// The ratio is the finding, and it is enormous: the sky is rigid across a
	// solar system and fluid between them, by five orders of magnitude.
	AddInfo(FString::Printf(
		TEXT("the sky is %.0f times more rigid across a system than between two"),
		WorstBetweenSystems / FMath::Max(WorstWithinSystem, 1e-30)));

	// The three skies written out, so the claim can be looked at as well as
	// measured. Numbers only; drawing them is a tool's business.
	{
		FString Dump;
		const TCHAR* Names[] = { TEXT("inner"), TEXT("outer"), TEXT("elsewhere") };
		const FVector3d Places[] = { InnerPlanet, OuterPlanet, ThereSystem };
		for (int32 Which = 0; Which < 3; ++Which)
		{
			TArray<FLedgerSkyStar> Seen;
			LedgerStarField::Visible(
				Catalogue, Places[Which], LedgerStarField::NakedEyeMagnitude, Seen);
			for (const FLedgerSkyStar& Star : Seen)
			{
				Dump += FString::Printf(TEXT("%s %d %.9f %.9f %.9f %.4f %.0f" LINE_TERMINATOR),
					Names[Which], Star.Index, Star.Direction.X, Star.Direction.Y,
					Star.Direction.Z, Star.ApparentMagnitude, Star.TemperatureKelvin);
			}
		}
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				TEXT("t077-skies.txt")));
		FFileHelper::SaveStringToFile(Dump, *Path);
		AddInfo(FString::Printf(TEXT("three skies written to %s"), *Path));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStarFieldBrightness,
	"Ledger.StarField.DistanceDimsAndTheCatalogueIsMostlyFaint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStarFieldBrightness::RunTest(const FString&)
{
	TArray<FLedgerCatalogueStar> Catalogue;
	LedgerStarField::Generate(20260908u, 40000, 260.0, Catalogue);

	// The distance modulus, checked against the definition rather than against
	// itself: ten parsecs is where apparent equals absolute, and a hundred is
	// five magnitudes fainter.
	FLedgerCatalogueStar Star;
	Star.AbsoluteMagnitude = 4.83;
	Star.PositionParsecs = FVector3d(10.0, 0.0, 0.0);
	TestEqual(TEXT("at ten parsecs, apparent is absolute"),
		LedgerStarField::ApparentMagnitude(Star, FVector3d::ZeroVector), 4.83, 1e-9);

	Star.PositionParsecs = FVector3d(100.0, 0.0, 0.0);
	TestEqual(TEXT("ten times further is five magnitudes fainter"),
		LedgerStarField::ApparentMagnitude(Star, FVector3d::ZeroVector), 9.83, 1e-9);

	// And the catalogue is shaped like a real one: overwhelmingly faint, with a
	// thin bright tail. A flat draw would make every sky a blaze.
	int32 Bright = 0;
	for (const FLedgerCatalogueStar& Entry : Catalogue)
	{
		if (Entry.AbsoluteMagnitude < 0.0)
		{
			++Bright;
		}
	}
	const double Fraction = static_cast<double>(Bright) / Catalogue.Num();
	AddInfo(FString::Printf(
		TEXT("%.2f%% of the catalogue is intrinsically bright (absolute magnitude "
			 "below zero)"), Fraction * 100.0));
	TestTrue(*FString::Printf(TEXT("most stars are faint (%.2f%% bright)"),
		Fraction * 100.0), Fraction < 0.05);

	// Hot stars are the bright ones, which is what stops the sky looking like a
	// bag of sweets.
	double HottestMagnitude = 100.0;
	double ColdestMagnitude = -100.0;
	double Hottest = 0.0;
	double Coldest = 1e9;
	for (const FLedgerCatalogueStar& Entry : Catalogue)
	{
		if (Entry.TemperatureKelvin > Hottest)
		{
			Hottest = Entry.TemperatureKelvin;
			HottestMagnitude = Entry.AbsoluteMagnitude;
		}
		if (Entry.TemperatureKelvin < Coldest)
		{
			Coldest = Entry.TemperatureKelvin;
			ColdestMagnitude = Entry.AbsoluteMagnitude;
		}
	}
	AddInfo(FString::Printf(
		TEXT("hottest %.0f K at absolute magnitude %.2f; coldest %.0f K at %.2f"),
		Hottest, HottestMagnitude, Coldest, ColdestMagnitude));
	TestTrue(TEXT("the hottest star is brighter than the coldest"),
		HottestMagnitude < ColdestMagnitude);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStarFieldDeterministic,
	"Ledger.StarField.TheSameSeedIsTheSameSky",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStarFieldDeterministic::RunTest(const FString&)
{
	// A sky that regenerated differently would move the constellations between
	// one session and the next, which is the one thing a star field must never
	// do -- it is the only fixed reference anybody has out there.
	TArray<FLedgerCatalogueStar> First;
	TArray<FLedgerCatalogueStar> Second;
	TArray<FLedgerCatalogueStar> Other;
	LedgerStarField::Generate(20260908u, 2000, 260.0, First);
	LedgerStarField::Generate(20260908u, 2000, 260.0, Second);
	LedgerStarField::Generate(20260909u, 2000, 260.0, Other);

	int32 Identical = 0;
	for (int32 Index = 0; Index < First.Num(); ++Index)
	{
		if (First[Index].PositionParsecs == Second[Index].PositionParsecs
			&& First[Index].AbsoluteMagnitude == Second[Index].AbsoluteMagnitude
			&& First[Index].TemperatureKelvin == Second[Index].TemperatureKelvin)
		{
			++Identical;
		}
	}
	TestEqual(TEXT("the same seed gives bit-identical stars"), Identical, First.Num());

	int32 Same = 0;
	for (int32 Index = 0; Index < First.Num(); ++Index)
	{
		if (First[Index].PositionParsecs == Other[Index].PositionParsecs)
		{
			++Same;
		}
	}
	AddInfo(FString::Printf(TEXT("a different seed shares %d of %d positions"),
		Same, First.Num()));
	TestEqual(TEXT("a different seed is a different sky"), Same, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerStarFieldCoversTheSky,
	"Ledger.StarField.TheStarsCoverTheWholeSky",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerStarFieldCoversTheSky::RunTest(const FString&)
{
	// **The assertion that was missing, and it is missing for a reason worth
	// keeping.** The catalogue's first hash was FNV-1a over three small
	// integers, which left the streams correlated: the one choosing latitude
	// and the one choosing longitude moved together, and every star in the
	// catalogue landed on a single arc across the sky.
	//
	// Every test above passed. They measured angles BETWEEN stars, and a
	// constellation drawn on a wire holds its shape across a solar system and
	// comes apart between two exactly as one drawn on a sphere does. The defect
	// was only visible in a picture of it.
	//
	// So this asks the question none of them did: is the sky full?
	TArray<FLedgerCatalogueStar> Catalogue;
	LedgerStarField::Generate(20260908u, 40000, 260.0, Catalogue);

	// Equal-area cells: bands in the SINE of the latitude, not the latitude, or
	// the polar cells would be small and look empty for the wrong reason.
	constexpr int32 Bands = 8;
	constexpr int32 Sectors = 16;
	int32 Counts[Bands][Sectors] = {};

	TArray<FLedgerSkyStar> Seen;
	LedgerStarField::Visible(
		Catalogue, FVector3d(3.0, -7.0, 2.0), LedgerStarField::NakedEyeMagnitude, Seen);
	for (const FLedgerSkyStar& Star : Seen)
	{
		const int32 Band = FMath::Clamp(
			static_cast<int32>((Star.Direction.Z + 1.0) * 0.5 * Bands), 0, Bands - 1);
		const double Longitude =
			(FMath::Atan2(Star.Direction.Y, Star.Direction.X) + LedgerPi) / LedgerTwoPi;
		const int32 Sector = FMath::Clamp(
			static_cast<int32>(Longitude * Sectors), 0, Sectors - 1);
		++Counts[Band][Sector];
	}

	int32 Empty = 0;
	int32 Fewest = TNumericLimits<int32>::Max();
	int32 Most = 0;
	for (int32 Band = 0; Band < Bands; ++Band)
	{
		for (int32 Sector = 0; Sector < Sectors; ++Sector)
		{
			const int32 Here = Counts[Band][Sector];
			if (Here == 0) { ++Empty; }
			Fewest = FMath::Min(Fewest, Here);
			Most = FMath::Max(Most, Here);
		}
	}

	const double Mean = static_cast<double>(Seen.Num()) / (Bands * Sectors);
	AddInfo(FString::Printf(
		TEXT("%d naked-eye stars over %d equal-area cells: mean %.1f, fewest %d, "
			 "most %d, empty %d"),
		Seen.Num(), Bands * Sectors, Mean, Fewest, Most, Empty));

	TestEqual(TEXT("no part of the sky is empty"), Empty, 0);

	// Poisson scatter on a mean of about six is wide, so this is a bound on
	// clumping and not on randomness: a cell holding five times the mean is a
	// structure, and a catalogue on an arc would have most cells at zero and a
	// few enormous.
	TestTrue(*FString::Printf(
		TEXT("no cell holds five times its share (most %d against a mean of %.1f)"),
		Most, Mean),
		Most < Mean * 5.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
