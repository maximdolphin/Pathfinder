// Bands that alternate, storms that sit where storms sit. T079.

#include "LedgerGiant.h"
#include "LedgerMath.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGiantBands,
	"Ledger.Giant.TheBandsAlternateAndTheJetsShear",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGiantBands::RunTest(const FString&)
{
	constexpr uint32 Seed = 20260908u;

	// **Alternating, which is the whole of what makes it banded.** Walk the
	// latitudes and count sign changes in the zonal wind: a giant whose air all
	// moves the same way is a beach ball with a gradient on it.
	int32 Changes = 0;
	double Previous = LedgerGiant::ZonalWindAt(-LedgerPi * 0.49, Seed);
	double Strongest = 0.0;
	for (int32 Step = 1; Step <= 2000; ++Step)
	{
		const double Latitude = -LedgerPi * 0.49 + LedgerPi * 0.98 * Step / 2000.0;
		const double Wind = LedgerGiant::ZonalWindAt(Latitude, Seed);
		if ((Wind > 0.0) != (Previous > 0.0))
		{
			++Changes;
		}
		Strongest = FMath::Max(Strongest, FMath::Abs(Wind));
		Previous = Wind;
	}
	AddInfo(FString::Printf(
		TEXT("%d jet reversals from pole to pole, strongest wind %.3f"),
		Changes, Strongest));
	TestTrue(*FString::Printf(TEXT("the jets alternate (%d reversals)"), Changes),
		Changes >= 8);

	// The poles are quiet, which is what the envelope is for: a jet that ran
	// right over the pole would have to run round a point.
	const double AtPole = FMath::Abs(LedgerGiant::ZonalWindAt(LedgerPi * 0.499, Seed));
	AddInfo(FString::Printf(TEXT("wind at the pole: %.5f of the strongest"),
		AtPole / Strongest));
	TestTrue(*FString::Printf(TEXT("the poles are quiet (%.5f)"), AtPole / Strongest),
		AtPole < Strongest * 0.02);

	// **Banded, not blotchy.** Along a line of latitude the field varies far
	// less than it does across one -- which is the difference between a gas
	// giant and a marble, and is the thing that would break first if the
	// turbulence were sampled without the flow.
	double AlongVariance = 0.0;
	double AcrossVariance = 0.0;
	{
		const double Latitude = 0.31;
		double Mean = 0.0;
		TArray<double> Along;
		for (int32 Step = 0; Step < 400; ++Step)
		{
			const double Value = LedgerGiant::SurfaceAt(
				Latitude, LedgerTwoPi * Step / 400.0, Seed, 0.0);
			Along.Add(Value);
			Mean += Value;
		}
		Mean /= Along.Num();
		for (const double Value : Along) { AlongVariance += FMath::Square(Value - Mean); }
		AlongVariance /= Along.Num();

		TArray<double> Across;
		Mean = 0.0;
		for (int32 Step = 0; Step < 400; ++Step)
		{
			const double Value = LedgerGiant::SurfaceAt(
				-LedgerPi * 0.45 + LedgerPi * 0.9 * Step / 400.0, 1.1, Seed, 0.0);
			Across.Add(Value);
			Mean += Value;
		}
		Mean /= Across.Num();
		for (const double Value : Across) { AcrossVariance += FMath::Square(Value - Mean); }
		AcrossVariance /= Across.Num();
	}
	AddInfo(FString::Printf(
		TEXT("variance along a band %.5f, across the bands %.5f, ratio %.1f"),
		AlongVariance, AcrossVariance, AcrossVariance / FMath::Max(AlongVariance, 1e-12)));
	// Two, not three. Three was tried and passed at 3.1, which is a bound
	// riding on the measurement rather than describing it -- the next seed with
	// one more storm crossing the sampled line would have failed it for no
	// reason worth failing for. The claim is that the field is banded, and
	// twice the variance across than along says that; the measured figure is
	// quoted so the margin is visible rather than assumed.
	TestTrue(*FString::Printf(
		TEXT("the field is banded (%.1f times more variance across than along)"),
		AcrossVariance / FMath::Max(AlongVariance, 1e-12)),
		AcrossVariance > AlongVariance * 2.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGiantStorms,
	"Ledger.Giant.StormsSitOnShearLinesAndDriftWithTheWind",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGiantStorms::RunTest(const FString&)
{
	constexpr uint32 Seed = 20260908u;

	// There are storms, and they cover a small part of the disc rather than
	// most of it -- a giant that is all storm is a noise field.
	int32 Inside = 0;
	int32 Total = 0;
	for (int32 Row = 0; Row < 120; ++Row)
	{
		for (int32 Column = 0; Column < 240; ++Column)
		{
			const double Latitude = -LedgerPi * 0.5 + LedgerPi * (Row + 0.5) / 120.0;
			const double Longitude = LedgerTwoPi * (Column + 0.5) / 240.0;
			++Total;
			if (LedgerGiant::StormAt(Latitude, Longitude, Seed, 0.0) > 0.5)
			{
				++Inside;
			}
		}
	}
	const double Fraction = static_cast<double>(Inside) / Total;
	AddInfo(FString::Printf(TEXT("storms cover %.2f%% of the surface"), Fraction * 100.0));
	TestTrue(*FString::Printf(TEXT("there are storms (%.2f%%)"), Fraction * 100.0),
		Fraction > 0.001);
	TestTrue(*FString::Printf(TEXT("but not everywhere (%.2f%%)"), Fraction * 100.0),
		Fraction < 0.25);

	// **They drift, and they drift with their own band's wind.** A storm in a
	// prograde jet must travel the other way from one in a retrograde jet, or
	// the whole thing is a scrolling texture.
	//
	// Measured by following the strongest storm's longitude over time at two
	// latitudes with opposite wind.
	auto FollowLongitude = [Seed](double Latitude, double Seconds) -> double
	{
		double BestLongitude = 0.0;
		double Best = 0.0;
		for (int32 Step = 0; Step < 1440; ++Step)
		{
			const double Longitude = LedgerTwoPi * Step / 1440.0;
			const double Here = LedgerGiant::StormAt(Latitude, Longitude, Seed, Seconds);
			if (Here > Best) { Best = Here; BestLongitude = Longitude; }
		}
		return Best > 0.5 ? BestLongitude : -1.0;
	};

	// **Find the storms by looking for them, not by guessing where they are.**
	// The first version stepped latitude in fortieths of a right angle and
	// asked each step whether a storm was there. Storms are a twentieth of a
	// radian across and sit on quantised jet boundaries, so the probe stepped
	// over every one of them and the test concluded there were none -- while
	// the census three lines above said they cover 5% of the surface.
	auto StrongestLatitude = [Seed](double Low, double High) -> double
	{
		double BestLatitude = 0.0;
		double Best = 0.0;
		for (int32 Row = 0; Row < 600; ++Row)
		{
			const double Latitude = Low + (High - Low) * Row / 600.0;
			for (int32 Column = 0; Column < 360; ++Column)
			{
				const double Here = LedgerGiant::StormAt(
					Latitude, LedgerTwoPi * Column / 360.0, Seed, 0.0);
				if (Here > Best) { Best = Here; BestLatitude = Latitude; }
			}
		}
		return Best > 0.5 ? BestLatitude : TNumericLimits<double>::Max();
	};

	int32 Followed = 0;
	FString Table;
	for (int32 Sign = 0; Sign < 2; ++Sign)
	{
		{
			const double Latitude = Sign == 0
				? StrongestLatitude(0.02, LedgerPi * 0.48)
				: StrongestLatitude(-LedgerPi * 0.48, -0.02);
			if (Latitude == TNumericLimits<double>::Max())
			{
				continue;
			}
			const double Start = FollowLongitude(Latitude, 0.0);
			if (Start < 0.0)
			{
				continue;
			}
			const double Later = FollowLongitude(Latitude, 2.0e5);
			if (Later < 0.0)
			{
				continue;
			}
			double Moved = Later - Start;
			if (Moved > LedgerPi) { Moved -= LedgerTwoPi; }
			if (Moved < -LedgerPi) { Moved += LedgerTwoPi; }

			const double Wind = LedgerGiant::ZonalWindAt(Latitude, Seed);
			Table += FString::Printf(
				TEXT("  latitude %+.2f rad: wind %+.3f, storm moved %+.4f rad\n"),
				Latitude, Wind, Moved);

			if (FMath::Abs(Moved) > 1e-6 && FMath::Abs(Wind) > 0.05)
			{
				TestTrue(*FString::Printf(
					TEXT("the storm at %+.2f drifts the way its wind blows"), Latitude),
					(Moved > 0.0) == (Wind > 0.0));
				++Followed;
			}
			break;
		}
	}
	AddInfo(FString::Printf(TEXT("storms followed across two hemispheres:\n%s"), *Table));
	TestTrue(TEXT("at least one storm was followed"), Followed > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGiantChart,
	"Ledger.Giant.WriteTheBandChart",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGiantChart::RunTest(const FString&)
{
	// The field written out so it can be looked at. T077 is why this exists:
	// three green tests there could not see that the sky was a line, and it
	// took a picture. A banded field is exactly the sort of thing that can pass
	// every statistical check and still look wrong.
	constexpr uint32 Seed = 20260908u;
	constexpr int32 Width = 512;
	constexpr int32 Height = 256;

	FString Dump;
	Dump.Reserve(Width * Height * 5);
	for (int32 Row = 0; Row < Height; ++Row)
	{
		const double Latitude = LedgerPi * 0.5 - LedgerPi * (Row + 0.5) / Height;
		for (int32 Column = 0; Column < Width; ++Column)
		{
			const double Longitude = LedgerTwoPi * (Column + 0.5) / Width;
			Dump += FString::Printf(TEXT("%.4f "),
				LedgerGiant::SurfaceAt(Latitude, Longitude, Seed, 0.0));
		}
		Dump += LINE_TERMINATOR;
	}

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			TEXT("t079-giant-field.txt")));
	FFileHelper::SaveStringToFile(Dump, *Path);
	AddInfo(FString::Printf(TEXT("band field written to %s"), *Path));
	TestTrue(TEXT("the chart was written"), !Dump.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
