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
	constexpr int32 Samples = 361;

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

	if (!FParse::Param(FCommandLine::Get(), TEXT("climate")))
	{
		return;
	}

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
		TArray<double> Ridges, Provinces, Lands, MountainTerm;
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
		Body += TEXT("height terms over land (mountains = ridges * land^1.4 * province):\n");
		Report(TEXT("ridges"), Ridges);
		Report(TEXT("province"), Provinces);
		Report(TEXT("land"), Lands);
		Report(TEXT("mountains"), MountainTerm);
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

	for (const double Longitude : Meridians)
	{
		TArray<FSample> Walk;
		Walk.Reserve(Samples);
		for (int32 Index = 0; Index < Samples; ++Index)
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
		Heights.Reserve(Samples);
		for (int32 Index = 0; Index < Samples; ++Index)
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

			++RangesFound;

			const double Latitude = 90.0 - static_cast<double>(Index) * LatitudeStep;
			const FVector3d Point = OnMeridian(Latitude, Longitude);
			const FVector3d Wind = LedgerClimate::PrevailingWind(Point);
			const double Windward = LedgerClimate::At(Along(Point, -Wind, 2), Params).Moisture;
			const double Lee = LedgerClimate::At(Along(Point, Wind, 2), Params).Moisture;

			// The claim is specifically that the LEE side is the dry one. A
			// difference in either direction would not be a rain shadow, it
			// would be a coincidence with a sign.
			const bool bShadow = (Windward - Lee) > 0.02;
			if (bShadow)
			{
				++RangesWithShadow;
			}
			else
			{
				Body += FString::Printf(
					TEXT("  %5d  %5.1f  %6.0f m   %6.3f  %6.3f   NO SHADOW\n"),
					Longitude, Latitude, Peak, Windward, Lee);
			}
		}
	}
	Body += FString::Printf(
		TEXT("  (only the ones without a shadow are listed; %d had one)\n\n"),
		RangesWithShadow);

	// ---- the ridge statistic -------------------------------------------
	//
	// The range search above finds a handful of features; this asks the same
	// question of every wind-crossing ridge on the planet, which is the number
	// that says whether the rain shadow is a real property of the field or two
	// lucky landmarks.
	double RidgeShadowFraction = 0.0;
	{
		int32 Ridges = 0;
		int32 Shadowed = 0;
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
				if (Peak <= LedgerTerrain::Elevation(WindwardPoint, Params) / 100.0
					|| Peak <= LedgerTerrain::Elevation(LeePoint, Params) / 100.0)
				{
					continue;
				}
				++Ridges;
				if (LedgerClimate::At(LeePoint, Params).Moisture
					< LedgerClimate::At(WindwardPoint, Params).Moisture)
				{
					++Shadowed;
				}
			}
		}
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
