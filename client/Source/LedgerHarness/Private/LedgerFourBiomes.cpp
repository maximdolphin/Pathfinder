#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "LedgerFourBiomes.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerBiome.h"
#include "LedgerClimate.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerTerrainMath.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr int32 Wanted = 4;
	constexpr double FourBiomeSettleSeconds = 22.0;
	constexpr double FourBiomeEyeMetres = 1.7;

	FVector3d FourBiomeOnSphere(double LatitudeDegrees, double LongitudeDegrees)
	{
		const double Lat = FMath::DegreesToRadians(LatitudeDegrees);
		const double Lon = FMath::DegreesToRadians(LongitudeDegrees);
		return FVector3d(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat)).GetSafeNormal();
	}
}

bool ULedgerFourBiomes::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerFourBiomes::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerFourBiomes, STATGROUP_Tickables);
}

void ULedgerFourBiomes::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("fourbiomes"));
}

void ULedgerFourBiomes::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	if (!bFound)
	{
		if (!FindSites())
		{
			bRunning = false;
			FPlatformMisc::RequestExit(false);
			return;
		}
		bFound = true;
	}

	if (Shot >= Sites.Num())
	{
		bRunning = false;
		Report();
		FPlatformMisc::RequestExit(false);
		return;
	}

	Place();

	Settle += DeltaSeconds;
	if (Settle < FourBiomeSettleSeconds)
	{
		return;
	}

	if (!bCaptured)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("four-biomes-%d-%s.png"), Shot + 1, *Names[Shot])));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bCaptured = true;
		return;
	}

	// **Then the same view twice more: with no scatter drawn, and with every**
	// **instance taken out of its component and put back.** The savanna column
	// is drawn where no measured instance projects -- the topmost of 3,074 in
	// that rectangle lands at y 459 and the column climbs to 415. Hidden, the
	// frame says whether the column is scatter at all; put back, it says
	// whether what is drawn is what the components hold.
	TArray<UHierarchicalInstancedStaticMeshComponent*> Scatter;
	if (const ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>())
	{
		if (const ALedgerPlanet* Planet = Builder->GetPlanet())
		{
			Planet->GetComponents(Scatter);
		}
	}
	if (Stage == 1 || Stage == 3)
	{
		if (++StageFrames < 30)
		{
			return;
		}
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("four-biomes-%d-%s-%s.png"), Shot + 1, *Names[Shot],
					Stage == 1 ? TEXT("noscatter") : TEXT("rebuilt"))));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		++Stage;
		return;
	}
	if (Stage == 2)
	{
		int32 Instances = 0;
		for (UHierarchicalInstancedStaticMeshComponent* Component : Scatter)
		{
			TArray<FTransform> All;
			All.Reserve(Component->GetInstanceCount());
			for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				Component->GetInstanceTransform(Index, Transform, true);
				All.Add(Transform);
			}
			Component->ClearInstances();
			Component->AddInstances(All, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
			Component->SetVisibility(true);
			Instances += All.Num();
		}
		UE_LOG(LogLedger, Log,
			TEXT("four biomes: %s scatter shown again, %d instances in %d components taken out and put back"),
			*Names[Shot], Instances, Scatter.Num());
		Stage = 3;
		StageFrames = 0;
		return;
	}
	if (Stage == 4)
	{
		Stage = 0;
		++Shot;
		Settle = 0.0;
		bCaptured = false;
		return;
	}

	// **Whether the stones in this frame are on the ground.** The savanna
	// capture had two lines of boulders climbing a hillside into the sky, and
	// a photograph cannot say which surface they were placed on.
	if (const ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>())
	{
		if (const ALedgerPlanet* Planet = Builder->GetPlanet())
		{
			FString Lines;
			TArray<FLedgerFooting> Stones;
			const int32 Off = Planet->MeasureScatterFootings(
				Camera != nullptr ? Camera->GetActorLocation() : FVector::ZeroVector, 1500.0, Lines, &Stones);

			// **The stones in one rectangle of the frame, by what the probe knows**
			// **about each.** The savanna frame has a column of boulders standing
			// in the sky over a hill (x 950-1180, y 380-500 of 1920x1080), and
			// every aggregate measure -- footings, drawn slope, morph off, the
			// stitching fixed -- says every stone is on the ground. So: project
			// every measured stone into the frame and list the ones that land
			// there. If none do, what is drawn there is not where its instance
			// data says it is, which is an answer too.
			if (APlayerController* Viewer = GetWorld()->GetFirstPlayerController())
			{
				// Topmost first: the stones drawn against the sky are the ones in
				// question, and in instance order the first 24 were all at its foot.
				TArray<TPair<FVector2D, const FLedgerFooting*>> InColumn;
				for (const FLedgerFooting& Stone : Stones)
				{
					FVector2D Screen;
					if (Viewer->ProjectWorldLocationToScreen(Stone.Where, Screen, false)
						&& Screen.X >= 950.0 && Screen.X <= 1180.0 && Screen.Y >= 380.0 && Screen.Y <= 500.0)
					{
						InColumn.Emplace(Screen, &Stone);
					}
				}
				InColumn.Sort([](const auto& A, const auto& B) { return A.Key.Y < B.Key.Y; });
				int32 Undrawn = 0;
				for (int32 Index = 0; Index < InColumn.Num(); ++Index)
				{
					const FVector2D& Screen = InColumn[Index].Key;
					const FLedgerFooting& Stone = *InColumn[Index].Value;
					Undrawn += Stone.bGroundRendered ? 0 : 1;
					if (Index < 24)
					{
						Lines += FString::Printf(
							TEXT("  in the column: screen (%.0f, %.0f), %5.0f m away, %+7.2f m above the drawn ground, %+7.2f m above the function, %4.1f deg; ground section %d %s, %s, bounds %s\n"),
							Screen.X, Screen.Y, Stone.RangeMetres, Stone.AboveDrawnMetres,
							Stone.AboveFunctionMetres, Stone.SlopeDegrees, Stone.GroundSection,
							Stone.bGroundActive ? TEXT("live") : TEXT("RELEASED"),
							Stone.bGroundRendered ? TEXT("drawn") : TEXT("NOT DRAWN"),
							Stone.bGroundBoundsHold ? TEXT("hold") : TEXT("MISS"));
					}
				}
				Lines += FString::Printf(
					TEXT("  stones projecting into the column rectangle: %d, %d of them on ground not drawn last frame\n"),
					InColumn.Num(), Undrawn);

				// **Every instance at any range, by where its mesh is drawn.** No
				// measured pivot projects above y 459, the column climbs to 415, and
				// hiding the scatter removes it: so list what is drawn up there, from
				// the components themselves, with no range cut and the mesh bounds.
				int32 Upper = 0;
				TSet<const UStaticMesh*> Described;
				for (UHierarchicalInstancedStaticMeshComponent* Component : Scatter)
				{
					const UStaticMesh* Mesh = Component->GetStaticMesh();
					const FBoxSphereBounds MeshBounds = Mesh != nullptr ? Mesh->GetBounds() : FBoxSphereBounds(ForceInit);
					if (Mesh != nullptr && !Described.Contains(Mesh))
					{
						Described.Add(Mesh);
						Lines += FString::Printf(TEXT("  mesh %s: bounds origin (%.0f, %.0f, %.0f) extent (%.0f, %.0f, %.0f) cm\n"),
							*Mesh->GetName(), MeshBounds.Origin.X, MeshBounds.Origin.Y, MeshBounds.Origin.Z,
							MeshBounds.BoxExtent.X, MeshBounds.BoxExtent.Y, MeshBounds.BoxExtent.Z);
					}
					for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
					{
						FTransform Transform;
						Component->GetInstanceTransform(Index, Transform, true);
						const FVector Drawn = Transform.TransformPosition(MeshBounds.Origin);
						FVector2D Screen;
						if (!Viewer->ProjectWorldLocationToScreen(Drawn, Screen, false)
							|| Screen.X < 950.0 || Screen.X > 1180.0 || Screen.Y < 380.0 || Screen.Y > 455.0)
						{
							continue;
						}
						if (++Upper <= 24)
						{
							Lines += FString::Printf(
								TEXT("  drawn in the upper column: screen (%.0f, %.0f), %.0f m away, scale %.2f, pivot %.2f m from the drawn centre, %s instance %d\n"),
								Screen.X, Screen.Y,
								Camera != nullptr ? FVector::Dist(Drawn, Camera->GetActorLocation()) / 100.0 : -1.0,
								Transform.GetScale3D().X, FVector::Dist(Drawn, Transform.GetLocation()) / 100.0,
								*Component->GetName(), Index);
						}
					}
				}
				Lines += FString::Printf(TEXT("  instances drawn in the upper column at any range: %d\n"), Upper);

				// **And how far the ground is behind them, pixel by pixel.** The four
				// column stones are a metre across, 32-37 m away, and traced onto a
				// drawn section; the frame reads them as climbing a hill 250 m off. A
				// view ray down the column says which: ground at ~35 m just under the
				// stones is a near mound they sit on the ridge line of.
				FCollisionQueryParams RayParams(SCENE_QUERY_STAT(LedgerColumnRay), true);
				for (UHierarchicalInstancedStaticMeshComponent* Component : Scatter)
				{
					RayParams.AddIgnoredComponent(Component);
				}
				for (int32 Row = 400; Row <= 540; Row += 10)
				{
					FVector RayOrigin, RayDirection;
					if (!Viewer->DeprojectScreenPositionToWorld(1045.0f, static_cast<float>(Row), RayOrigin, RayDirection))
					{
						continue;
					}
					FHitResult Hit;
					const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, RayOrigin,
						RayOrigin + RayDirection * 5.0e6, ECC_Visibility, RayParams);
					Lines += bHit
						? FString::Printf(TEXT("  view ray at (1045, %d): ground %.1f m away\n"), Row, Hit.Distance / 100.0)
						: FString::Printf(TEXT("  view ray at (1045, %d): nothing with collision within 50 km\n"), Row);
				}
			}
			UE_LOG(LogLedger, Log, TEXT("four biomes: %s\n%s"), *Names[Shot], *Lines);
			Footings += FString::Printf(TEXT("\n---- %s: %d stones off the ground ----\n"),
				*Names[Shot], Off) + Lines;
		}
	}

	for (UHierarchicalInstancedStaticMeshComponent* Component : Scatter)
	{
		Component->SetVisibility(false);
	}
	UE_LOG(LogLedger, Log, TEXT("four biomes: %s scatter hidden, %d components"),
		*Names[Shot], Scatter.Num());
	Stage = 1;
	StageFrames = 0;
}

bool ULedgerFourBiomes::FindSites()
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
	if (Biomes.Num() < Wanted)
	{
		UE_LOG(LogLedger, Error, TEXT("four biomes: only %d loaded"), Biomes.Num());
		return false;
	}

	const FLedgerTerrainParams Params = Planet->TerrainParams();
	const FVector3d SunDirection = Builder->GetSunFacing().GetSafeNormal();

	// Every fixture in this repository that forgot this photographed the night
	// side at least once. Well clear of the terminator, and on the raking side
	// of it, which is the light the surface study found reads ground best.
	auto IsLit = [&SunDirection](const FVector3d& Point)
	{
		return FVector3d::DotProduct(Point, SunDirection) > 0.35;
	};

	// The best example of each biome rather than the first: for every biome,
	// keep the lit, dry-land point where that biome's weight is highest. A
	// first-match search finds four places that all sit on the same boundary.
	TArray<double> BestWeight;
	TArray<FVector3d> BestPoint;
	BestWeight.Init(-1.0, Biomes.Num());
	BestPoint.Init(FVector3d::ZeroVector, Biomes.Num());

	TArray<double> Weights;
	for (double Latitude = -84.0; Latitude <= 84.0; Latitude += 1.0)
	{
		for (double Longitude = 0.0; Longitude < 360.0; Longitude += 1.0)
		{
			const FVector3d Point = FourBiomeOnSphere(Latitude, Longitude);
			if (!IsLit(Point))
			{
				continue;
			}
			// Above the waterline and clear of the shore: a beach is a
			// boundary, and this wants the middle of somewhere.
			if (LedgerTerrain::Elevation(Point, Params) / 100.0 <= 40.0)
			{
				continue;
			}

			const FLedgerClimate Climate = LedgerClimate::At(Point, Params);
			LedgerBiomes::Weigh(Biomes, Climate, 0.0, Weights);
			for (int32 Index = 0; Index < Weights.Num(); ++Index)
			{
				if (Weights[Index] > BestWeight[Index])
				{
					BestWeight[Index] = Weights[Index];
					BestPoint[Index] = Point;
				}
			}
		}
	}

	// The four most convincingly-itself biomes on the planet.
	TArray<int32> Order;
	for (int32 Index = 0; Index < Biomes.Num(); ++Index)
	{
		if (BestWeight[Index] > 0.0)
		{
			Order.Add(Index);
		}
	}
	Order.Sort([&BestWeight](int32 A, int32 B) { return BestWeight[A] > BestWeight[B]; });

	for (int32 Rank = 0; Rank < Order.Num() && Sites.Num() < Wanted; ++Rank)
	{
		const int32 Index = Order[Rank];
		Sites.Add(BestPoint[Index]);
		Names.Add(Biomes[Index].Name.Replace(TEXT(" "), TEXT("-")).ToLower());
		UE_LOG(LogLedger, Log, TEXT("four biomes: %s at weight %.2f"),
			*Biomes[Index].Name, BestWeight[Index]);
	}

	if (Sites.Num() < Wanted)
	{
		UE_LOG(LogLedger, Error,
			TEXT("four biomes: only %d lit land biomes found, need %d"),
			Sites.Num(), Wanted);
		return false;
	}
	return true;
}

void ULedgerFourBiomes::Place()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder->GetPlanet();
	APlayerController* Controller = World->GetFirstPlayerController();
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	const FVector3d Up = Sites[Shot];
	const FVector3d Origin = FVector3d(Planet->GetActorLocation());
	const double Ground = Planet->SurfaceRadiusAt(Up);

	// Hidden and parked above: the ship is the streaming anchor and not the
	// subject.
	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		Ship->SetVelocity(FVector::ZeroVector);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(FVector(Origin + Up * (Ground + 400000.0)));
	}

	// Looking twenty metres out, which is where a standing person looks and
	// what T429's pixel criterion is measured at. Same framing at every site,
	// because four photographs taken differently cannot be compared.
	FVector3d Along = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Up);
	if (Along.IsNearlyZero())
	{
		Along = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), Up);
	}
	Along.Normalize();

	const FVector Eye = FVector(Origin + Up * (Ground + FourBiomeEyeMetres * 100.0));
	const FVector3d TargetDirection =
		(Up + Along * (2000.0 / Planet->Radius)).GetSafeNormal();
	const FVector Target = FVector(Origin
		+ TargetDirection * Planet->SurfaceRadiusAt(TargetDirection));
	const FRotator Look = FRotationMatrix::MakeFromXZ(Target - Eye, FVector(Up)).Rotator();

	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Eye, Look, SpawnParams);
		if (Camera != nullptr)
		{
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

void ULedgerFourBiomes::Report()
{
	FString Body;
	Body += TEXT("Four biomes, at eye height, same framing (T437).\n\n");
	Body += TEXT("The verdict on whether these look like a video game heightfield is a\n");
	Body += TEXT("person's and is not in this file. What is in this file is the check\n");
	Body += TEXT("that they are four different places, because four photographs of the\n");
	Body += TEXT("same desert would satisfy a careless reading of the acceptance.\n\n");

	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		Body += FString::Printf(TEXT("  %d  %-28s  four-biomes-%d-%s.png\n"),
			Index + 1, *Names[Index], Index + 1, *Names[Index]);
	}
	Body += Footings;
	Body += TEXT("\nCompare them with tools/compare_captures.py.\n");

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
			TEXT("four-biomes.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("four biomes -> %s"), *Path);
}
