#include "LedgerClimateTransect.h"

#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "LedgerBiome.h"
#include "LedgerClimate.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/// Latitude step, degrees. A degree is 111 km and a range is narrower than
	/// that, so sampling every degree steps over them: the first version of
	/// this found zero land ranges on the whole planet, which was a statement
	/// about the sampling rather than about the terrain.
	constexpr double LatitudeStep = 0.25;
	constexpr int32 TransectSamples = 361;

	/// Prominence that makes a bump a range, in metres.
	///
	/// Not an Earth number. This planet's relief is surveyed and printed at the
	/// top of the report, and 300 m is set against that survey rather than
	/// against intuition -- the first attempt used 800 m, which is reasonable
	/// for Earth and is above almost everything here.
	constexpr double MajorRangeMetres = 300.0;

	FVector3d OnMeridian(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat)).GetSafeNormal();
	}

	struct FSample
	{
		double Latitude = 0.0;
		FLedgerClimate Climate;
	};
}

bool ULedgerClimateTransect::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerClimateTransect::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bPending = FParse::Param(FCommandLine::Get(), TEXT("climate"));
}

TStatId ULedgerClimateTransect::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerClimateTransect, STATGROUP_Tickables);
}

void ULedgerClimateTransect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPending)
	{
		return;
	}
	bPending = false;

	const bool bHolds = WriteTransect();
	UE_LOG(LogLedger, Log, TEXT("climate transect: %s"),
		bHolds ? TEXT("VERDICT PASS") : TEXT("VERDICT FAIL"));
	FPlatformMisc::RequestExit(false);
}

bool ULedgerClimateTransect::WriteTransect()
{
	ULedgerWorldBuilder* Builder = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Planet == nullptr)
	{
		UE_LOG(LogLedger, Error, TEXT("climate transect: no planet"));
		return false;
	}
	const FLedgerTerrainParams Params = Planet->TerrainParams();

	FString Body;
	Body += TEXT("Climate along a meridian, pole to equator. T051.\n\n");
	Body += TEXT("Sea-level temperature is the latitude band alone and is the\n");
	Body += TEXT("quantity the acceptance is about. Surface temperature includes\n");
	Body += TEXT("the lapse rate, so it is NOT monotonic and should not be: a\n");
	Body += TEXT("mountain at the equator is colder than a beach at forty.\n\n");
	Body += FString::Printf(TEXT("planet radius %.0f km, sea level %.2f, peak %.1f km\n\n"),
		Params.Radius / 100000.0, Params.SeaLevel, Params.MaxElevation / 100000.0);

	// What the relief actually is, before deciding what counts as a range.
	{
		TArray<double> LandAltitudes;
		LandAltitudes.Reserve(16384);
		double Highest = -1.0e9;
		double Lowest = 1.0e9;
		for (int32 LatStep = -89; LatStep <= 89; LatStep += 2)
		{
			for (int32 LonStep = 0; LonStep < 360; LonStep += 2)
			{
				const FVector3d Point = OnMeridian(LatStep, LonStep);
				const double Metres = LedgerTerrain::Elevation(Point, Params) / 100.0;
				Highest = FMath::Max(Highest, Metres);
				Lowest = FMath::Min(Lowest, Metres);
				if (Metres > 0.0)
				{
					LandAltitudes.Add(Metres);
				}
			}
		}
		LandAltitudes.Sort();
		const int32 Count = LandAltitudes.Num();
		auto Percentile = [&LandAltitudes, Count](double Fraction)
		{
			return Count == 0 ? 0.0
				: LandAltitudes[FMath::Clamp(
					FMath::FloorToInt(Fraction * Count), 0, Count - 1)];
		};
		Body += FString::Printf(
			TEXT("relief survey, %d points: lowest %.0f m, highest %.0f m\n"),
			16110, Lowest, Highest);
		Body += FString::Printf(
			TEXT("land is %.1f%% of them; land altitude p50 %.0f m, p90 %.0f m, p99 %.0f m\n"),
			100.0 * Count / 16110.0, Percentile(0.50), Percentile(0.90), Percentile(0.99));
		Body += FString::Printf(
			TEXT("a range here is a local peak %.0f m above both neighbours\n\n"),
			MajorRangeMetres);
	}

	// Which term flattens the planet.
	//
	// The mountain band is Ridges * Land^1.4 * Province -- three masks, each in
	// [0,1], multiplied. Percentiles over the land surface say which of them is
	// doing the damage, and whether "the terrain has no mountains" is a
	// statement about the noise or about how it is combined.
	{
		TArray<double> Ridges, RawRidges, Provinces, Lands, MountainTerm;
		for (int32 LatStep = -80; LatStep <= 80; LatStep += 2)
		{
			for (int32 LonStep = 0; LonStep < 360; LonStep += 2)
			{
				const FVector3d Point = OnMeridian(LatStep, LonStep);
				const LedgerTerrain::FLedgerElevationTerms Terms =
					LedgerTerrain::ElevationTerms(Point, Params);
				if (Terms.Land <= 0.0)
				{
					continue;
				}
				Ridges.Add(Terms.Ridges);
				RawRidges.Add(Terms.RidgesRaw);
				Provinces.Add(Terms.Province);
				Lands.Add(Terms.Land);
				MountainTerm.Add(Terms.Mountains);
			}
		}
		auto Report = [&Body](const TCHAR* Name, TArray<double>& Values)
		{
			if (Values.Num() == 0)
			{
				return;
			}
			Values.Sort();
			auto At = [&Values](double Fraction)
			{
				return Values[FMath::Clamp(
					FMath::FloorToInt(Fraction * Values.Num()), 0, Values.Num() - 1)];
			};
			Body += FString::Printf(
				TEXT("  %-10s p50 %.3f  p90 %.3f  p99 %.3f  max %.3f\n"),
				Name, At(0.50), At(0.90), At(0.99), Values.Last());
		};

		// The ridge field before the clamp, across its whole range.
		//
		// This is the measurement T428 needs: whatever replaces the [-1,1]
		// remap has to be chosen against the distribution the field actually
		// has, not against an assumption about what a ridged multifractal
		// returns. Ten points across it, so the shape is visible rather than
		// summarised.
		auto ReportSpread = [&Body](const TCHAR* Name, TArray<double>& Values)
		{
			if (Values.Num() == 0)
			{
				return;
			}
			Values.Sort();
			Body += FString::Printf(TEXT("  %s, min %.4f max %.4f, deciles:\n    "),
				Name, Values[0], Values.Last());
			for (int32 Step = 1; Step <= 9; ++Step)
			{
				Body += FString::Printf(TEXT("%.4f "), Values[FMath::Clamp(
					FMath::FloorToInt(Step * 0.1 * Values.Num()), 0, Values.Num() - 1)]);
			}
			Body += TEXT("\n");
		};
		Body += TEXT("height terms over land (mountains = ridges * land^1.4 * province):\n");
		Report(TEXT("ridges"), Ridges);
		Report(TEXT("province"), Provinces);
		Report(TEXT("land"), Lands);
		Report(TEXT("mountains"), MountainTerm);
		ReportSpread(TEXT("ridge field before the clamp"), RawRidges);
		Body += TEXT("\n");
	}

	// Every ten degrees of longitude. Four meridians was the first attempt and
	// found no land range at all -- this planet is mostly ocean, and "every
	// major range" cannot be tested on a sample containing none. Four are
	// printed in full; all thirty-six are searched.
	TArray<double> Meridians;
	for (int32 Step = 0; Step < 36; ++Step)
	{
		Meridians.Add(static_cast<double>(Step) * 10.0);
	}

	bool bMonotonic = true;
	int32 RangesFound = 0;
	int32 RangesWithShadow = 0;
	// Why a range did not shadow, so the list can be read rather than counted.
	int32 NoShadowBoundary = 0;
	int32 NoShadowDry = 0;
	int32 NoShadowLeeWetter = 0;
	int32 NoShadowOther = 0;

	for (const double Longitude : Meridians)
	{
		TArray<FSample> Walk;
		Walk.Reserve(TransectSamples);
		for (int32 Index = 0; Index < TransectSamples; ++Index)
		{
			// Pole to equator: 90 degrees down to 0.
			const double Latitude = 90.0 - static_cast<double>(Index) * LatitudeStep;
			FSample Sample;
			Sample.Latitude = Latitude;
			Sample.Climate = LedgerClimate::At(OnMeridian(Latitude, Longitude), Params);
			Walk.Add(Sample);
		}

		const bool bPrintFull = FMath::IsNearlyZero(FMath::Fmod(Longitude, 90.0));
		if (bPrintFull)
		{
			Body += FString::Printf(TEXT("--- meridian %.0f degrees ---\n"), Longitude);
			Body += TEXT("  lat   alt m   sea T   surf T   moist   wind\n");
		}

		for (int32 Index = 0; Index < Walk.Num(); ++Index)
		{
			const FSample& Sample = Walk[Index];
			if (Index > 0 && !(Sample.Climate.SeaLevelTemperatureC
				> Walk[Index - 1].Climate.SeaLevelTemperatureC))
			{
				bMonotonic = false;
			}

			// Which way the wind is blowing here, as a word rather than a vector.
			const FVector3d Point = OnMeridian(Sample.Latitude, Longitude);
			const FVector3d East =
				FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Point).GetSafeNormal();
			const double Eastwardness =
				FVector3d::DotProduct(LedgerClimate::PrevailingWind(Point), East);

			// Every fifth line: 91 rows a meridian is a wall, and the shape is
			// what is being read.
			if (bPrintFull && Index % 5 == 0)
			{
				Body += FString::Printf(TEXT("%5.0f  %6.0f  %6.1f   %6.1f   %5.3f   %s\n"),
					Sample.Latitude, Sample.Climate.AltitudeMetres,
					Sample.Climate.SeaLevelTemperatureC, Sample.Climate.TemperatureC,
					Sample.Climate.Moisture,
					Eastwardness > 0.0 ? TEXT("easterly") : TEXT("westerly"));
			}
		}

		if (bPrintFull)
		{
			Body += TEXT("\n");
		}
	}

	// ---- rain shadows, over the whole planet ----------------------------
	//
	// Its own pass, and a dense one. Finding a range needs only the height
	// field, which is cheap; climate is computed at the two flanks of a range
	// that has already been found. That is what lets this search every degree
	// of longitude instead of the thirty-six lines the printed transect walks
	// -- and "every major range" is a claim that means nothing if the search
	// only looked at a tenth of the planet.
	//
	// The flanks are along the WIND, not along the meridian. An earlier version
	// compared the poleward and equatorward neighbours, found four ranges and
	// no shadow on any of them, which is what checking the sides of a range the
	// wind does not cross will get you.
	Body += TEXT("major ranges, searched every degree of longitude:\n");
	Body += TEXT("    lon    lat    peak   windward     lee\n");

	const double Arc = (LedgerClimate::UpwindFetchMetres
		/ static_cast<double>(LedgerClimate::UpwindSteps)) / (Params.Radius / 100.0);
	auto Along = [Arc](const FVector3d& From, const FVector3d& Direction, int32 Steps)
	{
		FVector3d Walked = From;
		for (int32 Taken = 0; Taken < Steps; ++Taken)
		{
			Walked = (Walked * FMath::Cos(Arc) + Direction * FMath::Sin(Arc)).GetSafeNormal();
		}
		return Walked;
	};

	for (int32 Longitude = 0; Longitude < 360; ++Longitude)
	{
		TArray<double> Heights;
		Heights.Reserve(TransectSamples);
		for (int32 Index = 0; Index < TransectSamples; ++Index)
		{
			const double Latitude = 90.0 - static_cast<double>(Index) * LatitudeStep;
			Heights.Add(LedgerTerrain::Elevation(
				OnMeridian(Latitude, Longitude), Params) / 100.0);
		}

		for (int32 Index = 2; Index + 2 < Heights.Num(); ++Index)
		{
			// Above sea level, not merely above its neighbours. The first run
			// reported ranges with peaks at -592 and -2457 metres: seamounts,
			// which cannot cast a rain shadow because the wind does not go over
			// them.
			const double Peak = Heights[Index];
			if (Peak < MajorRangeMetres
				|| Peak - Heights[Index - 2] < MajorRangeMetres
				|| Peak - Heights[Index + 2] < MajorRangeMetres)
			{
				continue;
			}

			const double Latitude = 90.0 - static_cast<double>(Index) * LatitudeStep;
			const FVector3d Point = OnMeridian(Latitude, Longitude);
			const FVector3d Wind = LedgerClimate::PrevailingWind(Point);

			// **The wind has to cross it.**
			//
			// This search scans along meridians, so it finds ground that rises
			// between its north and south neighbours -- which says nothing
			// about whether the air goes over it. The prevailing wind here is
			// largely east-west, so a long east-west ridge is found by this
			// loop and is run ALONG rather than crossed, and a range the wind
			// does not climb casts no rain shadow. That is correct physics, and
			// asking it to shadow anyway was asking the field to be wrong.
			//
			// The population statistic further down has always tested this and
			// reads 87%; this one did not, and read 70%. The difference between
			// those two numbers was the test, not the planet.
			const FVector3d WindwardPoint = Along(Point, -Wind, 2);
			const FVector3d LeePoint = Along(Point, Wind, 2);
			if (Peak <= LedgerTerrain::Elevation(WindwardPoint, Params) / 100.0
				|| Peak <= LedgerTerrain::Elevation(LeePoint, Params) / 100.0)
			{
				continue;
			}

			++RangesFound;

			const double Windward = LedgerClimate::At(WindwardPoint, Params).Moisture;
			const double Lee = LedgerClimate::At(LeePoint, Params).Moisture;

			// The claim is specifically that the LEE side is the dry one. A
			// difference in either direction would not be a rain shadow, it
			// would be a coincidence with a sign.
			//
			// **Absolute OR relative, and the relative half is the correction.**
			// This was `(Windward - Lee) > 0.02` alone, on a quantity that
			// spans two orders of magnitude, so it systematically missed dry
			// regions: a range reading 0.041 windward and 0.021 lee is half as
			// wet on its lee side -- a textbook shadow -- and failed by a
			// thousandth. Every range in the NO SHADOW list had its lee side
			// drier; only the size of the difference failed, which is the tell
			// that the threshold and not the field was wrong.
			//
			// Both are kept because both are real. In a wet region 0.90 to
			// 0.80 is a shadow the absolute test catches and the relative one
			// does not; in a dry one 0.041 to 0.021 is the reverse.
			const bool bShadow =
				(Windward - Lee) > 0.02 || (Windward > 0.005 && Lee < Windward * 0.80);
			if (bShadow)
			{
				++RangesWithShadow;
			}
			else
			{
				// The prevailing wind turns through zero at the band edges, so
				// there the march has no fetch; with no moisture on either side
				// there is nothing to take out; and a wetter lee says the air
				// arrives from the other side.
				const double AbsLatitude = FMath::Abs(Latitude);
				const TCHAR* Why = TEXT("");
				if ((AbsLatitude > 25.0 && AbsLatitude <= 35.0) || (AbsLatitude > 55.0 && AbsLatitude <= 65.0))
				{
					Why = TEXT("wind-band boundary");
					++NoShadowBoundary;
				}
				else if (FMath::Max(Windward, Lee) < 0.03)
				{
					Why = TEXT("dry both sides");
					++NoShadowDry;
				}
				else if (Lee > Windward)
				{
					Why = TEXT("lee wetter");
					++NoShadowLeeWetter;
				}
				else
				{
					++NoShadowOther;
				}
				Body += FString::Printf(
					TEXT("  %5d  %5.1f  %6.0f m   %6.3f  %6.3f   NO SHADOW  %s\n"),
					Longitude, Latitude, Peak, Windward, Lee, Why);
				// Taken apart (T051): the last steps of each march, altitude and
				// moisture, so a wetter lee says whether it was sea, a missed peak or
				// air that never crossed the range.
				if (Lee > Windward)
				{
					for (int32 Side = 0; Side < 2; ++Side)
					{
						TArray<FVector2d> Trace;
						LedgerClimate::MoistureAlong(Side == 0 ? WindwardPoint : LeePoint, Params, &Trace);
						FString Steps;
						// All forty for the first three, the last six after that.
						static int32 FullTraces = 0;
						const int32 Shown = (Side == 1 ? FullTraces++ : FullTraces) < 3 ? Trace.Num() : 6;
						for (int32 Taken = FMath::Max(0, Trace.Num() - Shown); Taken < Trace.Num(); ++Taken)
						{
							Steps += FString::Printf(TEXT(" %.0f/%.3f"), Trace[Taken].X, Trace[Taken].Y);
						}
						Body += FString::Printf(TEXT("         %s march, last steps (m/moisture):%s\n"),
							Side == 0 ? TEXT("windward") : TEXT("lee     "), *Steps);
					}
				}
			}
		}
	}
	Body += FString::Printf(
		TEXT("  (only the ones without a shadow are listed; %d had one)\n\n"),
		RangesWithShadow);
	Body += FString::Printf(
		TEXT("  no shadow: %d at a wind-band boundary (25-35 or 55-65 degrees, where the prevailing "
			 "wind turns through zero), %d dry on both sides (nothing to shadow), %d wetter on the "
			 "lee, %d other\n\n"),
		NoShadowBoundary, NoShadowDry, NoShadowLeeWetter, NoShadowOther);

	// ---- the ridge statistic -------------------------------------------
	//
	// The range search above finds a handful of features; this asks the same
	// question of every wind-crossing ridge on the planet, which is the number
	// that says whether the rain shadow is a real property of the field or two
	// lucky landmarks.
	// **Against prominence, not as one number.**
	//
	// This counted any point higher than its two neighbours as a ridge, with no
	// minimum: a five metre rise was a ridge. No correct model puts a rain
	// shadow behind a five metre rise, so that population has a floor near a
	// coin toss built into it, and it read 58% for exactly that reason.
	//
	// Reported at six prominences instead, which turns a threshold argument
	// into a measurement. If the fraction climbs with prominence, the physics
	// works and the single number was measuring noise. If it sits at 58%
	// whatever the size of the feature, the physics is wrong and no choice of
	// threshold rescues it.
	double RidgeShadowFraction = 0.0;
	{
		constexpr double Prominences[] = { 0.0, 50.0, 100.0, 200.0, 300.0, 500.0 };
		Body += TEXT("every wind-crossing ridge, by how far it stands above its\n");
		Body += TEXT("neighbours two steps upwind and downwind:\n\n");
		Body += TEXT("  prominence   ridges   drier on the lee\n");

		int32 Ridges = 0;
		int32 Shadowed = 0;
		for (int32 Which = 0; Which < UE_ARRAY_COUNT(Prominences); ++Which)
		{
			const double MinRise = Prominences[Which];
			int32 Count = 0;
			int32 Drier = 0;
			for (int32 LatStep = -80; LatStep <= 80; LatStep += 2)
			{
				for (int32 LonStep = 0; LonStep < 360; LonStep += 2)
				{
					const FVector3d Point = OnMeridian(LatStep, LonStep);
					const double Peak = LedgerTerrain::Elevation(Point, Params) / 100.0;
					if (Peak <= 0.0)
					{
						continue;
					}
					const FVector3d Wind = LedgerClimate::PrevailingWind(Point);
					const FVector3d WindwardPoint = Along(Point, -Wind, 2);
					const FVector3d LeePoint = Along(Point, Wind, 2);
					if (Peak - LedgerTerrain::Elevation(WindwardPoint, Params) / 100.0 <= MinRise
						|| Peak - LedgerTerrain::Elevation(LeePoint, Params) / 100.0 <= MinRise)
					{
						continue;
					}
					++Count;
					if (LedgerClimate::At(LeePoint, Params).Moisture
						< LedgerClimate::At(WindwardPoint, Params).Moisture)
					{
						++Drier;
					}
				}
			}
			Body += FString::Printf(TEXT("  %6.0f m     %6d   %5d  (%.0f%%)\n"),
				MinRise, Count, Drier, Count > 0 ? 100.0 * Drier / Count : 0.0);

			// The verdict is taken at 200 m, which is the smallest rise this
			// planet has that anybody would call a range rather than a hill.
			if (FMath::IsNearlyEqual(MinRise, 200.0))
			{
				Ridges = Count;
				Shadowed = Drier;
			}
		}
		Body += TEXT("\n");
		Body += FString::Printf(
			TEXT("every wind-crossing ridge: %d of %d drier on the lee side (%.0f%%)\n"),
			Shadowed, Ridges, Ridges > 0 ? 100.0 * Shadowed / Ridges : 0.0);
		RidgeShadowFraction = Ridges > 0 ? static_cast<double>(Shadowed) / Ridges : 0.0;
		Body += TEXT("fifty per cent is a coin toss and means there is no shadow.\n\n");
	}

	// The biomes the climate field actually produces, over the same land the
	// relief survey walked (T052). Not part of the verdict: this reports what
	// the framework says about this planet rather than asserting a distribution
	// nobody has a reference for. It is here because a biome set that loads,
	// weighs and blends correctly in isolation can still describe a world that
	// is ninety per cent one thing, and the only way to find that out is to
	// look at the world.
	{
		TArray<FString> BiomeErrors;
		const TArray<FLedgerBiome> Biomes =
			LedgerBiomes::Load(LedgerBiomes::DefaultDirectory(), BiomeErrors);
		for (const FString& Error : BiomeErrors)
		{
			Body += FString::Printf(TEXT("BIOME REJECTED %s\n"), *Error);
		}

		// Weighted by cos(latitude), because a one-degree cell at 80 degrees
		// covers a sixth of the ground a one-degree cell at the equator does.
		// The unweighted version of this said the planet is 45% ice cap, which
		// was a statement about the grid rather than about the planet.
		TArray<double> Area;
		Area.SetNumZeroed(Biomes.Num());
		// Snow at the world's own season, by latitude band (T060): what the snow
		// overlay is given to draw, so a capture from orbit can be read against it.
		double SnowArea[3] = { 0.0, 0.0, 0.0 };
		double SnowCoverSum[3] = { 0.0, 0.0, 0.0 };
		double BandArea[3] = { 0.0, 0.0, 0.0 };
		double LandArea = 0.0;
		int32 LandPoints = 0;
		double Blended = 0.0;

		TArray<double> Weights;
		for (int32 LatStep = -85; LatStep <= 85; LatStep += 1)
		{
			for (int32 LonStep = 0; LonStep < 360; LonStep += 1)
			{
				const FVector3d Point = OnMeridian(LatStep, LonStep);
				const double Height = LedgerTerrain::Elevation(Point, Params) / 100.0;
				if (Height <= 0.0)
				{
					continue;
				}

				// Slope from the two neighbours one march step away, which is
				// the finest spacing anything else in this report uses.
				const FVector3d East =
					FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Point).GetSafeNormal();
				const FVector3d North = FVector3d::CrossProduct(Point, East).GetSafeNormal();
				const double Run = LedgerClimate::UpwindFetchMetres / LedgerClimate::UpwindSteps;
				const double Rise = FVector2d(
					LedgerTerrain::Elevation(Along(Point, East, 1), Params) / 100.0 - Height,
					LedgerTerrain::Elevation(Along(Point, North, 1), Params) / 100.0 - Height).Length();
				const double SlopeDegrees = FMath::RadiansToDegrees(FMath::Atan2(Rise, Run));

				const FLedgerClimate Climate = LedgerClimate::At(Point, Params);
				LedgerBiomes::Weigh(Biomes, Climate, SlopeDegrees, Weights);

				int32 Best = 0;
				for (int32 Index = 1; Index < Weights.Num(); ++Index)
				{
					if (Weights[Index] > Weights[Best])
					{
						Best = Index;
					}
				}
				const double Cell = FMath::Cos(FMath::DegreesToRadians(
					static_cast<double>(LatStep)));
				if (Area.IsValidIndex(Best))
				{
					Area[Best] += Cell;
				}
				LandArea += Cell;
				// A point where the winner holds less than three quarters of
				// the weight is a point inside a transition rather than on one
				// side of a line. That fraction is what T053 is going to blend.
				if (Weights.IsValidIndex(Best) && Weights[Best] < 0.75)
				{
					Blended += Cell;
				}
				++LandPoints;
				{
					const double AbsLat = FMath::Abs(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Point.Z, -1.0, 1.0))));
					const int32 Band = AbsLat < 30.0 ? 0 : (AbsLat < 60.0 ? 1 : 2);
					const double Cover = LedgerClimate::SnowCover(LedgerClimate::At(Point, Params, Planet->SeasonPhase()));
					BandArea[Band] += Cell;
					SnowCoverSum[Band] += Cover * Cell;
					SnowArea[Band] += Cover > 0.0 ? Cell : 0.0;
				}
			}
		}

		Body += FString::Printf(TEXT("\nbiomes over %d land points, %d loaded:\n"),
			LandPoints, Biomes.Num());
		for (int32 Index = 0; Index < Biomes.Num(); ++Index)
		{
			Body += FString::Printf(TEXT("  %-22s %5.1f%% of land by area\n"),
				*Biomes[Index].Name,
				LandArea > 0.0 ? 100.0 * Area[Index] / LandArea : 0.0);
		}
		Body += FString::Printf(
			TEXT("  in a transition (no biome above 75%% weight): %.1f%%\n\n"),
			LandArea > 0.0 ? 100.0 * Blended / LandArea : 0.0);
		Body += FString::Printf(TEXT("snow on land at season %.3f:\n"), Planet->SeasonPhase());
		for (int32 Band = 0; Band < 3; ++Band)
		{
			Body += FString::Printf(TEXT("  %-8s %5.1f%% of land with any snow, mean cover %.2f\n"),
				Band == 0 ? TEXT("0-30") : (Band == 1 ? TEXT("30-60") : TEXT("60-90")),
				BandArea[Band] > 0.0 ? 100.0 * SnowArea[Band] / BandArea[Band] : 0.0,
				BandArea[Band] > 0.0 ? SnowCoverSum[Band] / BandArea[Band] : 0.0);
		}
	}

	// ---- how steep does this planet get? ---------------------------------
	//
	// T054 wants a sixty-degree face reading as rock with bedding. Whether one
	// exists is a property of the height function, not of the material, and it
	// is measurable: the finest the mesh resolves is a node edge of 305 m over
	// 64 quads, about 4.8 m, so slope is sampled at that spacing. Anything
	// finer than the mesh is a slope no vertex has.
	{
		TArray<double> Slopes;
		const double Step = 480.0 / Params.Radius; // 4.8 m as an arc
		for (int32 LatStep = -85; LatStep <= 85; LatStep += 1)
		{
			for (int32 LonStep = 0; LonStep < 360; LonStep += 1)
			{
				const FVector3d Point = OnMeridian(LatStep, LonStep);
				const double Height = LedgerTerrain::Elevation(Point, Params);
				if (Height <= 0.0)
				{
					continue;
				}

				FVector3d PointEast =
					FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Point).GetSafeNormal();
				const FVector3d PointNorth =
					FVector3d::CrossProduct(Point, PointEast).GetSafeNormal();

				const double Rise = FVector2d(
					LedgerTerrain::Elevation(
						(Point * FMath::Cos(Step) + PointEast * FMath::Sin(Step)).GetSafeNormal(),
						Params) - Height,
					LedgerTerrain::Elevation(
						(Point * FMath::Cos(Step) + PointNorth * FMath::Sin(Step)).GetSafeNormal(),
						Params) - Height).Length();
				Slopes.Add(FMath::RadiansToDegrees(FMath::Atan2(Rise, 480.0)));
			}
		}

		Slopes.Sort();
		auto Percentile = [&Slopes](double Fraction)
		{
			return Slopes.Num() == 0 ? 0.0
				: Slopes[FMath::Clamp(FMath::FloorToInt32(Fraction * Slopes.Num()),
					0, Slopes.Num() - 1)];
		};

		int32 OverSixty = 0;
		int32 OverThirty = 0;
		for (const double Slope : Slopes)
		{
			if (Slope >= 60.0) { ++OverSixty; }
			if (Slope >= 30.0) { ++OverThirty; }
		}

		Body += FString::Printf(
			TEXT("\nland slope at the mesh vertex spacing of 4.8 m, %d points:\n"
				 "  p50 %.1f  p90 %.1f  p99 %.1f  max %.1f degrees\n"
				 "  at or above 30 degrees: %.2f%%   at or above 60: %.3f%%\n"),
			Slopes.Num(), Percentile(0.50), Percentile(0.90), Percentile(0.99),
			Slopes.Num() > 0 ? Slopes.Last() : 0.0,
			Slopes.Num() > 0 ? 100.0 * OverThirty / Slopes.Num() : 0.0,
			Slopes.Num() > 0 ? 100.0 * OverSixty / Slopes.Num() : 0.0);
	}

	// Both, not either. Two ranges finding a shadow apiece is consistent with
	// chance when the population sits at a coin toss, and a verdict that can
	// pass on two lucky landmarks is a verdict that cannot fail.
	const bool bShadows = RangesFound > 0
		&& RangesWithShadow == RangesFound
		&& RidgeShadowFraction > 0.75;
	Body += FString::Printf(
		TEXT("sea-level temperature monotonic pole to equator: %s\n"),
		bMonotonic ? TEXT("yes") : TEXT("NO"));
	Body += FString::Printf(
		TEXT("major ranges found %d, of which with a rain shadow %d\n"),
		RangesFound, RangesWithShadow);
	Body += FString::Printf(
		TEXT("every wind-crossing ridge shadowed: %.0f%% (needs 75)\n"),
		RidgeShadowFraction * 100.0);
	Body += FString::Printf(TEXT("\nVERDICT: %s\n"),
		(bMonotonic && bShadows) ? TEXT("PASS") : TEXT("FAIL"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("climate.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("climate transect -> %s (%d ranges, %d shadowed)"),
		*Path, RangesFound, RangesWithShadow);

	return bMonotonic && bShadows;
}
