// Two routes to the same sky, and one seed to the same system. T086.
//
// Every other test in M03 checks a quantity against a number somebody worked
// out. These two check the code against ITSELF -- the same fact reached by a
// different road, and the same seed run in a different process. That is a
// weaker claim per test and a much harder one to satisfy by accident, because
// there is nothing to tune towards.

#include "LedgerBody.h"
#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
#include "LedgerSky.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// SplitMix64, because the sample points have to be spread over a cube of
	/// (time, latitude, longitude) and a weaker generator correlates its
	/// streams -- which is exactly the bug the star field found in T077.
	double AgreementRandom(uint32 Seed, int32 Index, int32 Stream)
	{
		uint64 X = static_cast<uint64>(Seed) * 0x9E3779B97F4A7C15ull
			^ static_cast<uint64>(static_cast<uint32>(Index)) * 0xBF58476D1CE4E5B9ull
			^ static_cast<uint64>(static_cast<uint32>(Stream)) * 0x94D049BB133111EBull;
		X ^= X >> 30; X *= 0xBF58476D1CE4E5B9ull;
		X ^= X >> 27; X *= 0x94D049BB133111EBull;
		X ^= X >> 31;
		return static_cast<double>(X >> 11) / 9007199254740992.0;
	}

	FVector3d AgreementAnchor(double LatitudeRadians, double LongitudeRadians)
	{
		return FVector3d(
			FMath::Cos(LatitudeRadians) * FMath::Cos(LongitudeRadians),
			FMath::Cos(LatitudeRadians) * FMath::Sin(LongitudeRadians),
			FMath::Sin(LatitudeRadians));
	}

	/// Where the recorded run's answer lives. Next to the test, because it is
	/// part of the test and not an output.
	FString AgreementGoldenPath()
	{
		return FPaths::ProjectDir()
			/ TEXT("Source/LedgerCore/Private/Tests/EphemerisGolden.txt");
	}

	/// The times the golden records. A negative one is deliberate: an epoch is
	/// a label, not a beginning, and a Kepler solve that only works forwards
	/// would pass every other test in this module.
	const TArray<double>& AgreementTimes()
	{
		static const TArray<double> Times = { 0.0, 1.0e6, 1.0e8, -3.7e7 };
		return Times;
	}

	FString AgreementTable(const FLedgerSystem& System)
	{
		FString Text;
		for (int32 When = 0; When < AgreementTimes().Num(); ++When)
		{
			TArray<FLedgerState> States;
			LedgerEphemeris::StatesAt(System, AgreementTimes()[When], States);
			for (int32 Index = 0; Index < States.Num(); ++Index)
			{
				Text += FString::Printf(TEXT("%d %d %s %.3f %.3f %.3f%s"),
					When, Index, *System.Bodies[Index].Name,
					States[Index].PositionMetres.X,
					States[Index].PositionMetres.Y,
					States[Index].PositionMetres.Z,
					LINE_TERMINATOR);
			}
		}
		return Text;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSkyAgreement,
	"Ledger.Agreement.TheSkyAndTheEphemerisAgreeToTheArcminuteEverywhere",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSkyAgreement::RunTest(const FString&)
{
	// **Two roads, and neither is the other's shortcut.**
	//
	// The first draft of this test was the navigator's identity --
	// sin(alt) = sin(lat)sin(dec) + cos(lat)cos(dec)cos(H) -- against
	// SolarAltitude. It agreed to *zero* arcseconds, and zero was the tell:
	// SolarAltitude, SolarDeclination and HourAngle are all derived from the
	// same SunDirectionInBody vector, so the identity was checking spherical
	// trigonometry against itself. It could not have failed, and a test that
	// cannot fail is not evidence.
	//
	// This one goes the other way round the frames. The sky rotates the star's
	// direction from the system frame INTO the body's (UnrotateVector) and
	// projects it onto an east-north-up basis. This test rotates the observer
	// OUT of the body frame into the system's (ToSystem), and does the whole
	// calculation there: up is the observer's position minus the body's centre,
	// and the sun's direction is the star minus the observer. Nothing is shared
	// but the orbit and the orientation, which is exactly what is being checked.
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);
	const double Year = LedgerEphemeris::PeriodSeconds(
		System.Bodies[0].MassKg, System.Bodies[1].Orbit.SemiMajorAxisMetres);

	constexpr int32 Samples = 100;
	double Worst = 0.0;
	double WorstLatitude = 0.0;
	double WorstAltitude = 0.0;
	int32 Checked = 0;

	for (int32 Sample = 0; Sample < Samples; ++Sample)
	{
		// Over a whole year and both hemispheres, and either side of the epoch.
		const double At = (AgreementRandom(86u, Sample, 1) * 2.0 - 1.0) * Year;
		const double Latitude = FMath::Asin(AgreementRandom(86u, Sample, 2) * 2.0 - 1.0);
		const double Longitude = (AgreementRandom(86u, Sample, 3) * 2.0 - 1.0) * LedgerPi;
		const FVector3d Anchor = AgreementAnchor(Latitude, Longitude);

		const double FromTheSky = LedgerSky::SolarAltitude(System, 1, Anchor, At);

		// The same question asked in the system frame, from the ground.
		TArray<FLedgerState> States;
		LedgerEphemeris::StatesAt(System, At, States);

		FLedgerBodyPoint Standing;
		Standing.BodyIndex = 1;
		Standing.Metres = Anchor * System.Bodies[1].RadiusMetres;
		const FVector3d Observer = LedgerFrames::ToSystem(System, Standing, At).Metres;

		const FVector3d Up =
			(Observer - States[1].PositionMetres).GetSafeNormal();
		const FVector3d ToStar =
			(States[0].PositionMetres - Observer).GetSafeNormal();
		const double FromTheEphemeris =
			FMath::Asin(FMath::Clamp(FVector3d::DotProduct(Up, ToStar), -1.0, 1.0));

		const double Difference = FMath::Abs(FromTheSky - FromTheEphemeris);
		if (Difference > Worst)
		{
			Worst = Difference;
			WorstLatitude = Latitude;
			WorstAltitude = FromTheSky;
		}
		++Checked;
	}

	const double Arcminute = LedgerPi / (180.0 * 60.0);
	AddInfo(FString::Printf(
		TEXT("%d samples over a year of %.0f s and the whole globe; the worst "
			 "disagreement is %.4f arcseconds, at latitude %.1f with the sun %.1f "
			 "degrees up"),
		Checked, Year, Worst / Arcminute * 60.0,
		FMath::RadiansToDegrees(WorstLatitude),
		FMath::RadiansToDegrees(WorstAltitude)));

	TestEqual(TEXT("every sample was taken"), Checked, Samples);
	TestTrue(*FString::Printf(TEXT("sky and ephemeris agree to the arcminute (%.4f arcsec)"),
		Worst / Arcminute * 60.0), Worst < Arcminute);

	// **And the disagreement that is left has a name.** The sky answers with
	// the direction from the body's CENTRE, which is the geocentric convention
	// and the right one for lighting a whole terrain patch with a single sun.
	// This test answers from the ground. The difference between those two is
	// parallax, and parallax is the body's radius over its distance to the
	// star -- so the residual is not slop to be widened away, it is a
	// prediction, and it is checked as one.
	//
	// A residual much SMALLER than parallax would be the interesting failure:
	// it would mean the observer never left the centre and the two roads had
	// quietly merged, which is how the first draft of this test died.
	TArray<FLedgerState> AtEpoch;
	LedgerEphemeris::StatesAt(System, 0.0, AtEpoch);
	const double Distance =
		(AtEpoch[1].PositionMetres - AtEpoch[0].PositionMetres).Length();
	const double Parallax = System.Bodies[1].RadiusMetres / Distance;
	AddInfo(FString::Printf(
		TEXT("the body's radius over its distance to the star is %.4f arcseconds, "
			 "which is what standing on the ground rather than at the centre is "
			 "worth"),
		Parallax / Arcminute * 60.0));
	TestTrue(*FString::Printf(
		TEXT("the residual is parallax-sized (%.4f against %.4f arcsec)"),
		Worst / Arcminute * 60.0, Parallax / Arcminute * 60.0),
		Worst > Parallax * 0.5 && Worst < Parallax * 1.2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerSeedDeterminism,
	"Ledger.Agreement.OneSeedGivesTheSamePositionsInAnotherProcess",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerSeedDeterminism::RunTest(const FString&)
{
	const FLedgerSystem System = LedgerBodies::Generate(20260910u);

	// The cheap half first: the same seed twice in one process, compared bit
	// for bit. This catches anything that leaks between generations -- a static,
	// an uninitialised field, a container whose order depends on an address.
	const FLedgerSystem Again = LedgerBodies::Generate(20260910u);
	TestEqual(TEXT("the same seed builds the same number of bodies"),
		Again.Bodies.Num(), System.Bodies.Num());

	TArray<FLedgerState> A;
	TArray<FLedgerState> B;
	LedgerEphemeris::StatesAt(System, 1.0e8, A);
	LedgerEphemeris::StatesAt(Again, 1.0e8, B);
	bool bIdentical = A.Num() == B.Num();
	for (int32 Index = 0; bIdentical && Index < A.Num(); ++Index)
	{
		bIdentical = A[Index].PositionMetres == B[Index].PositionMetres;
	}
	TestTrue(TEXT("and puts them in bit-identical places"), bIdentical);

	// **The expensive half: a different process.** A committed table is one --
	// it was written by a build that is no longer running, on a day that is
	// over, and every later run is the second process. That makes this the only
	// test here that can fail because of the compiler rather than the code,
	// which is the point: ARCH Rule 5 says the same seed means the same world,
	// and a world that depends on who built it does not obey that.
	const FString Golden = AgreementGoldenPath();
	const FString Current = AgreementTable(System);

	FString Recorded;
	if (!FFileHelper::LoadFileToString(Recorded, *Golden))
	{
		FFileHelper::SaveStringToFile(Current, *Golden);
		AddError(FString::Printf(
			TEXT("no recorded run to compare against; one has been written to %s. "
				 "Re-run, and this compares a process against a process."),
			*Golden));
		return false;
	}

	TArray<FString> Was;
	TArray<FString> Is;
	Recorded.ParseIntoArrayLines(Was);
	Current.ParseIntoArrayLines(Is);

	TestEqual(TEXT("the recorded run has the same number of rows"),
		Is.Num(), Was.Num());
	if (Is.Num() != Was.Num())
	{
		return false;
	}

	double Worst = 0.0;
	FString Where;
	for (int32 Row = 0; Row < Is.Num(); ++Row)
	{
		TArray<FString> Mine;
		TArray<FString> Theirs;
		Is[Row].ParseIntoArrayWS(Mine);
		Was[Row].ParseIntoArrayWS(Theirs);
		if (Mine.Num() != Theirs.Num() || Mine.Num() < 6)
		{
			AddError(FString::Printf(TEXT("row %d is malformed: '%s' against '%s'"),
				Row, *Is[Row], *Was[Row]));
			return false;
		}
		// Names and indices must match exactly: a system that generated a
		// different set of bodies has already failed, whatever the numbers say.
		for (int32 Field = 0; Field < Mine.Num() - 3; ++Field)
		{
			if (Mine[Field] != Theirs[Field])
			{
				AddError(FString::Printf(
					TEXT("row %d describes a different body: '%s' against '%s'"),
					Row, *Is[Row], *Was[Row]));
				return false;
			}
		}
		for (int32 Field = Mine.Num() - 3; Field < Mine.Num(); ++Field)
		{
			const double Difference =
				FMath::Abs(FCString::Atod(*Mine[Field]) - FCString::Atod(*Theirs[Field]));
			if (Difference > Worst)
			{
				Worst = Difference;
				Where = Is[Row];
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d rows against the recorded run; the worst coordinate differs by "
			 "%.4f m"),
		Is.Num(), Worst));
	if (Worst > 0.0)
	{
		AddInfo(FString::Printf(TEXT("worst at: %s"), *Where));
	}
	// A millimetre, which is the precision the table is written to. Anything
	// larger is a real difference and wants looking at rather than widening.
	TestTrue(*FString::Printf(
		TEXT("a different process puts every body in the same place (%.4f m)"), Worst),
		Worst <= 0.001);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
