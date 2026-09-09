#include "LedgerBiomeSite.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerBiome.h"
#include "LedgerClimate.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/// Long enough for the terrain to stream in at the closest framing. The
	/// patch cache starts empty and a photograph taken before it fills is a
	/// photograph of the coarse LOD, which has none of the detail the
	/// acceptance is about.
	constexpr double SettleSeconds = 6.0;

	/// Three framings of the same boundary, in metres of look-ahead. Close
	/// enough to see whether the two grounds interlock, far enough to see
	/// whether the transition reads as a band.
	struct FSiteShot
	{
		const TCHAR* Name;
		double EyeHeightMetres;
		double LookAheadMetres;

		/// True for the sharpest boundary on the planet, false for the
		/// three-way meeting point.
		bool bEdge;
	};

	const FSiteShot SiteShots[] =
	{
		{ TEXT("biomes-close.png"),        2.0,    12.0, false },
		{ TEXT("biomes-field.png"),       30.0,   400.0, false },
		{ TEXT("biomes-wide.png"),       600.0,  6000.0, false },
		{ TEXT("biomes-edge-close.png"),   2.0,    12.0, true  },
		{ TEXT("biomes-edge-field.png"),  30.0,   400.0, true  },
		{ TEXT("biomes-edge-wide.png"),  600.0,  6000.0, true  },
	};

	/// The three heaviest weights at a point, descending.
	void TopThree(const TArray<double>& Weights, double& First, double& Second, double& Third)
	{
		First = Second = Third = 0.0;
		for (const double Weight : Weights)
		{
			if (Weight > First) { Third = Second; Second = First; First = Weight; }
			else if (Weight > Second) { Third = Second; Second = Weight; }
			else if (Weight > Third) { Third = Weight; }
		}
	}

	FVector3d OnSphere(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat)).GetSafeNormal();
	}
}

bool ULedgerBiomeSite::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerBiomeSite::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!FParse::Param(FCommandLine::Get(), TEXT("biomesite")))
	{
		return;
	}
	bRunning = true;
}

bool ULedgerBiomeSite::FindSite()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr
		? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Planet == nullptr)
	{
		return false;
	}

	TArray<FString> Errors;
	const TArray<FLedgerBiome> Biomes =
		LedgerBiomes::Load(LedgerBiomes::DefaultDirectory(), Errors);
	if (Biomes.Num() < 3)
	{
		UE_LOG(LogLedger, Error, TEXT("biome site: %d biomes loaded, need three to meet"),
			Biomes.Num());
		return false;
	}

	const FLedgerTerrainParams Params = Planet->TerrainParams();

	// Only ground the sun is on.
	//
	// The first site this found by sharpness was at -30, -61, which is the
	// night side: the photograph came back as a black frame with a moonlit
	// cloud deck in the bottom third. A correct search and a useless picture.
	// The sun is fixed, so this is a constraint on the search rather than
	// something to fix in the camera.
	const FVector3d SunDirection = Builder->GetSunFacing().GetSafeNormal();
	auto IsLit = [&SunDirection](const FVector3d& Point)
	{
		// Well clear of the terminator: 0.35 is about twenty degrees of sun
		// elevation, which is the raking light the surface study found reads
		// ground best.
		return FVector3d::DotProduct(Point, SunDirection) > 0.35;
	};

	// Maximise the *third* weight. Maximising evenness of the top two finds a
	// two-way boundary, of which there are thousands and none of which is the
	// acceptance; the third weight is the one that is hard to come by, so it is
	// the one to search on.
	FVector3d Best = FVector3d::ZeroVector;
	double BestThird = -1.0;
	TArray<double> Weights;

	for (double Latitude = -84.0; Latitude <= 84.0; Latitude += 0.5)
	{
		for (double Longitude = 0.0; Longitude < 360.0; Longitude += 0.5)
		{
			const FVector3d Point = OnSphere(Latitude, Longitude);
			if (!IsLit(Point))
			{
				continue;
			}
			const double Height = LedgerTerrain::Elevation(Point, Params) / 100.0;
			if (Height <= 20.0)
			{
				// Above water, and clear of the shoreline: a beach is a
				// boundary between land and sea, which is not the boundary
				// being demonstrated.
				continue;
			}

			FLedgerClimate Climate = LedgerClimate::At(Point, Params);
			LedgerBiomes::Weigh(Biomes, Climate, 0.0, Weights);

			double First = 0.0;
			double Second = 0.0;
			double Third = 0.0;
			TopThree(Weights, First, Second, Third);
			if (Third > BestThird)
			{
				BestThird = Third;
				Best = Point;
			}
		}
	}

	if (BestThird <= 0.0)
	{
		UE_LOG(LogLedger, Error, TEXT("biome site: no land found"));
		return false;
	}

	Site = Best;

	// Which way the ground changes fastest, so the camera looks across the
	// boundary rather than along it. Four directions, and the one whose weights
	// differ most from the site's.
	//
	// **Over the distance the camera can see, which is the whole point.** This
	// compared points 500 km apart at first -- `500000 cm / (Radius_cm / 100)`
	// is an arc in metres-over-metres only if the numerator is metres too, and
	// it was centimetres. The direction it picked was the right way to a
	// rainforest five hundred kilometres off and told the camera nothing, which
	// is exactly what the photographs showed: eight kilometres of ground with
	// no boundary in it, and a report claiming one at "5 km".
	LedgerBiomes::Weigh(Biomes, LedgerClimate::At(Site, Params), 0.0, Weights);
	const TArray<double> Here = Weights;

	FVector3d East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Site);
	if (East.IsNearlyZero())
	{
		East = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), Site);
	}
	East.Normalize();
	const FVector3d North = FVector3d::CrossProduct(Site, East).GetSafeNormal();

	const double Arc = 300000.0 / Params.Radius; // 3 km, the widest framing
	const FVector3d Directions[4] = { East, -East, North, -North };
	double BestChange = -1.0;
	for (const FVector3d& Direction : Directions)
	{
		const FVector3d There =
			(Site * FMath::Cos(Arc) + Direction * FMath::Sin(Arc)).GetSafeNormal();
		LedgerBiomes::Weigh(Biomes, LedgerClimate::At(There, Params), 0.0, Weights);

		double Change = 0.0;
		for (int32 Biome = 0; Biome < Weights.Num(); ++Biome)
		{
			Change += FMath::Abs(Weights[Biome] - Here[Biome]);
		}
		if (Change > BestChange)
		{
			BestChange = Change;
			Across = Direction;
		}
	}

	// ---- how sharp is the sharpest boundary on the planet? ----------------
	//
	// The photographs of this site show no band, and "no visible blend band" is
	// half the acceptance -- so the obvious question is whether that is the
	// blend working or whether there is simply no boundary anywhere to see. A
	// picture cannot answer it. This does: the largest change in biome weights
	// over three kilometres, anywhere on land.
	double SharpestChange = 0.0;
	FVector3d Sharpest = Site;
	FVector3d SharpestAcross = Across;
	{
		TArray<double> There2;
		for (double Latitude2 = -84.0; Latitude2 <= 84.0; Latitude2 += 1.0)
		{
			for (double Longitude2 = 0.0; Longitude2 < 360.0; Longitude2 += 1.0)
			{
				const FVector3d Point = OnSphere(Latitude2, Longitude2);
				if (!IsLit(Point)
					|| LedgerTerrain::Elevation(Point, Params) / 100.0 <= 20.0)
				{
					continue;
				}
				LedgerBiomes::Weigh(Biomes, LedgerClimate::At(Point, Params), 0.0, Weights);

				FVector3d LocalEast = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Point);
				if (LocalEast.IsNearlyZero())
				{
					continue;
				}
				LocalEast.Normalize();
				const FVector3d LocalNorth = FVector3d::CrossProduct(Point, LocalEast).GetSafeNormal();

				for (const FVector3d& Direction : { LocalEast, LocalNorth })
				{
					const FVector3d Neighbour =
						(Point * FMath::Cos(Arc) + Direction * FMath::Sin(Arc)).GetSafeNormal();
					if (LedgerTerrain::Elevation(Neighbour, Params) / 100.0 <= 0.0)
					{
						// The coastline is a real boundary and not this one.
						continue;
					}
					LedgerBiomes::Weigh(Biomes, LedgerClimate::At(Neighbour, Params), 0.0, There2);

					double Change = 0.0;
					for (int32 Biome = 0; Biome < Weights.Num(); ++Biome)
					{
						Change += FMath::Abs(There2[Biome] - Weights[Biome]);
					}
					if (Change > SharpestChange)
					{
						SharpestChange = Change;
						Sharpest = Point;
						SharpestAcross = Direction;
					}
				}
			}
		}
	}

	// ---- the report ------------------------------------------------------
	const double Latitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Site.Z, -1.0, 1.0)));
	const double Longitude = FMath::RadiansToDegrees(FMath::Atan2(Site.Y, Site.X));
	const FLedgerClimate SiteClimate = LedgerClimate::At(Site, Params);

	FString Body;
	Body += TEXT("Where three biomes meet (T053).\n\n");
	Body += FString::Printf(TEXT("site: %.2f lat, %.2f lon, %.0f m altitude\n"),
		Latitude, Longitude, LedgerTerrain::Elevation(Site, Params) / 100.0);
	Body += FString::Printf(TEXT("climate there: %.1f C surface, %.1f C at sea level, "
		"moisture %.3f\n\n"), SiteClimate.TemperatureC,
		SiteClimate.SeaLevelTemperatureC, SiteClimate.Moisture);

	Body += TEXT("weights at the site, and 3 km across the boundary:\n");
	Body += TEXT("  biome                   here   across\n");

	const FVector3d Far = (Site * FMath::Cos(Arc) + Across * FMath::Sin(Arc)).GetSafeNormal();
	TArray<double> There;
	LedgerBiomes::Weigh(Biomes, LedgerClimate::At(Far, Params), 0.0, There);
	for (int32 Biome = 0; Biome < Biomes.Num(); ++Biome)
	{
		if (Here[Biome] < 0.005 && There[Biome] < 0.005)
		{
			continue;
		}
		Body += FString::Printf(TEXT("  %-22s %6.3f   %6.3f\n"),
			*Biomes[Biome].Name, Here[Biome], There[Biome]);
	}

	// What the camera is actually looking down.
	//
	// The first version of this reported the site and a point five kilometres
	// across and left the rest to the photograph -- and the photograph showed
	// one uniform ground, which could mean the blend is broken or could mean
	// the boundary was never in frame. A table cannot be argued with.
	Body += TEXT("\nalong the camera\'s line, every 500 m:\n");
	Body += TEXT("     km   alt m   dominant biome         weight\n");
	for (int32 Step = 0; Step <= 16; ++Step)
	{
		const double Metres = Step * 500.0;
		const double StepArc = (Metres * 100.0) / (Params.Radius);
		const FVector3d Point =
			(Site * FMath::Cos(StepArc) + Across * FMath::Sin(StepArc)).GetSafeNormal();
		const double Altitude = LedgerTerrain::Elevation(Point, Params) / 100.0;
		LedgerBiomes::Weigh(Biomes, LedgerClimate::At(Point, Params), 0.0, Weights);

		int32 Winner = 0;
		for (int32 Biome = 1; Biome < Weights.Num(); ++Biome)
		{
			if (Weights[Biome] > Weights[Winner])
			{
				Winner = Biome;
			}
		}
		Body += FString::Printf(TEXT("  %5.1f  %6.0f   %-22s %6.3f\n"),
			Metres / 1000.0, Altitude, *Biomes[Winner].Name, Weights[Winner]);
	}

	double First = 0.0;
	double Second = 0.0;
	double Third = 0.0;
	TopThree(Here, First, Second, Third);
	Body += FString::Printf(TEXT("\nthe three heaviest here: %.3f, %.3f, %.3f\n"),
		First, Second, Third);

	Edge = Sharpest;
	EdgeAcross = SharpestAcross;

	// ---- how many biomes does one palette cell actually need? -------------
	//
	// The material carries three ground slots and the palette is chosen per
	// cell of a cube face (LedgerPatchGenerator.cpp), so a cell holding more
	// than three biomes loses the rest -- and the loss shows up as a hard line
	// on the cell boundary, which is what the captures show. Three is a guess
	// until this is counted.
	int32 CellHistogram[9] = {};
	int32 CellsCounted = 0;
	{
		constexpr double CellExtent = 1.0 / 16.0;
		TArray<double> CellWeights;
		TArray<double> CellTotals;
		for (int32 Face = 0; Face < 6; ++Face)
		{
			for (int32 CellY = 0; CellY < 16; ++CellY)
			{
				for (int32 CellX = 0; CellX < 16; ++CellX)
				{
					CellTotals.Reset();
					CellTotals.SetNumZeroed(Biomes.Num());
					int32 LandSamples = 0;
					for (int32 SampleY = 0; SampleY <= 4; ++SampleY)
					{
						for (int32 SampleX = 0; SampleX <= 4; ++SampleX)
						{
							const FVector3d Point = LedgerTerrain::CubeToSphere(
								LedgerTerrain::FaceToCube(static_cast<ELedgerCubeFace>(Face),
									(CellX + SampleX / 4.0) * CellExtent,
									(CellY + SampleY / 4.0) * CellExtent));
							if (LedgerTerrain::Elevation(Point, Params) <= 0.0)
							{
								continue;
							}
							++LandSamples;
							LedgerBiomes::Weigh(
								Biomes, LedgerClimate::At(Point, Params), 0.0, CellWeights);
							for (int32 Biome = 0; Biome < CellTotals.Num(); ++Biome)
							{
								CellTotals[Biome] += CellWeights[Biome];
							}
						}
					}
					if (LandSamples == 0)
					{
						continue;
					}

					int32 Present = 0;
					for (const double Weight : CellTotals)
					{
						// Five per cent of the cell, averaged. Below that a
						// biome is a rounding error on the ground.
						if (Weight / LandSamples >= 0.05)
						{
							++Present;
						}
					}
					CellHistogram[FMath::Clamp(Present, 0, 8)] += 1;
					++CellsCounted;
				}
			}
		}
	}

	Body += FString::Printf(
		TEXT("\nbiomes present in one palette cell (1/16 of a cube face, ~600 km),\n"
			 "counted over %d cells with land in them, at 5%% of the cell or more:\n"),
		CellsCounted);
	int32 OverThree = 0;
	for (int32 Count = 1; Count <= 8; ++Count)
	{
		if (CellHistogram[Count] == 0)
		{
			continue;
		}
		Body += FString::Printf(TEXT("  %d biome%s  %5d cells  %5.1f%%\n"),
			Count, Count == 1 ? TEXT(" ") : TEXT("s"), CellHistogram[Count],
			CellsCounted > 0 ? 100.0 * CellHistogram[Count] / CellsCounted : 0.0);
		if (Count > 3)
		{
			OverThree += CellHistogram[Count];
		}
	}
	Body += FString::Printf(
		TEXT("  cells needing more than the three slots the mesh can carry: %.1f%%\n"),
		CellsCounted > 0 ? 100.0 * OverThree / CellsCounted : 0.0);

	const double SharpLat = FMath::RadiansToDegrees(
		FMath::Asin(FMath::Clamp(Sharpest.Z, -1.0, 1.0)));
	const double SharpLon = FMath::RadiansToDegrees(FMath::Atan2(Sharpest.Y, Sharpest.X));
	Body += FString::Printf(
		TEXT("\nsharpest boundary anywhere on land: %.3f of the weight moves over 3 km\n"
			 "  (at %.1f lat, %.1f lon; 2.0 would be one biome replaced by another)\n"),
		SharpestChange, SharpLat, SharpLon);

	// The claim the picture is evidence for, stated so it can fail.
	const bool bThreeWay = Third >= 0.10;
	Body += FString::Printf(TEXT("\nthree biomes each above 10%%: %s\n"),
		bThreeWay ? TEXT("yes") : TEXT("NO"));
	Body += FString::Printf(TEXT("\nVERDICT: %s\n"), bThreeWay ? TEXT("PASS") : TEXT("FAIL"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("biome-site.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("biome site -> %s (third weight %.3f)"), *Path, Third);
	return true;
}

void ULedgerBiomeSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr
		? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Planet == nullptr || Controller == nullptr || Controller->GetPawn() == nullptr)
	{
		return;
	}

	if (!bFound)
	{
		if (!FindSite())
		{
			bRunning = false;
			FPlatformMisc::RequestExit(false);
			return;
		}
		bFound = true;
	}

	if (Index >= UE_ARRAY_COUNT(SiteShots))
	{
		UE_LOG(LogLedger, Log, TEXT("biome site: done"));
		bRunning = false;
		FPlatformMisc::RequestExit(false);
		return;
	}

	// Re-placed every frame, for the reason the surface study is: the ship is
	// a physics actor and will drift out from under a camera that was placed
	// once six seconds ago.
	Place();

	Settle += DeltaSeconds;
	if (Settle < SettleSeconds)
	{
		return;
	}

	if (!bCaptured)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString(SiteShots[Index].Name)));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogLedger, Log, TEXT("biome site -> %s"), *Path);
		bCaptured = true;
		return;
	}

	++Index;
	Settle = 0.0;
	bCaptured = false;
}

void ULedgerBiomeSite::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder->GetPlanet();
	APlayerController* Controller = World->GetFirstPlayerController();

	const FSiteShot& Current = SiteShots[Index];
	const FVector3d& From = Current.bEdge ? Edge : Site;
	const FVector3d& Toward = Current.bEdge ? EdgeAcross : Across;

	// Stand back a quarter of the look-ahead, so the site sits a quarter of the
	// way into frame and the ground beyond it -- which is what it is a boundary
	// *to* -- fills the rest. Half put the far biome on the horizon, where the
	// atmosphere washes every colour difference out and the picture shows one
	// ground under haze.
	const double BackArc =
		-(Current.LookAheadMetres * 25.0) / (Planet->Radius);
	const FVector3d Eye3d =
		(From * FMath::Cos(BackArc) + Toward * FMath::Sin(BackArc)).GetSafeNormal();

	const double Ground = Planet->SurfaceRadiusAt(Eye3d);
	const FVector Eye = FVector(Planet->GetActorLocation())
		+ FVector(Eye3d * (Ground + Current.EyeHeightMetres * 100.0));

	const double ForwardArc = (Current.LookAheadMetres * 100.0) / Planet->Radius;
	const FVector3d Target3d =
		(Eye3d * FMath::Cos(ForwardArc) + Toward * FMath::Sin(ForwardArc)).GetSafeNormal();
	const FVector Target = FVector(Planet->GetActorLocation())
		+ FVector(Target3d * Planet->SurfaceRadiusAt(Target3d));

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		// Hidden, not moved aside. Parking it four kilometres overhead put a
		// hull-shaped shadow across half the first frame -- a straight-edged
		// black wedge that reads exactly like a terrain LOD bug, and is not
		// one. Moving something out of frame does not move its shadow.
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(Eye + FVector(Eye3d * 400000.0));
	}

	// Built from the local up, not from the world's.
	//
	// FVector::Rotation() gives a rotator with zero roll, which means the
	// camera's up is world Z projected -- and on a sphere world Z is only
	// vertical at one longitude. Looking east from fourteen degrees north put
	// the horizon down the side of the frame at sixty degrees. The biome
	// captures escaped this by looking north, where the projection happens to
	// land on the local up; that was luck, not correctness.
	const FRotator Look = FRotationMatrix::MakeFromXZ(
		Target - Eye, FVector(Eye3d)).Rotator();

	if (Camera == nullptr)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, Look, Params);
		if (Camera != nullptr)
		{
			// Pinned exposure, for the same reason the surface study pins it:
			// three framings of the same ground, and auto exposure exists to
			// cancel exactly the differences being compared.
			if (UCameraComponent* Component = Camera->GetCameraComponent())
			{
				FPostProcessSettings& Post = Component->PostProcessSettings;
				Post.bOverride_AutoExposureMinBrightness = true;
				Post.bOverride_AutoExposureMaxBrightness = true;
				Post.AutoExposureMinBrightness = 1.0f;
				Post.AutoExposureMaxBrightness = 1.0f;
				Post.bOverride_AutoExposureBias = true;
				Post.AutoExposureBias = 0.0f;
			}
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

TStatId ULedgerBiomeSite::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerBiomeSite, STATGROUP_Tickables);
}
