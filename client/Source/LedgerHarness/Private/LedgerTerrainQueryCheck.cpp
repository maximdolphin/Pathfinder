#include "LedgerTerrainQueryCheck.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerTerrainSample.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "LedgerMath.h"

namespace
{
	/// How long to let the terrain stream before asking it anything.
	///
	/// The patch under the ship arrives in a frame; the ring of collision
	/// patches out to the edge takes longer, and a query into a hole is not a
	/// disagreement between the API and the trace -- it is a question asked too
	/// early. Both are supposed to say "nothing here", and this waits so that
	/// the run measures the interesting case.
	constexpr double QuerySettleSeconds = 12.0;

	constexpr int32 Queries = 1000;

	/// Deterministic, so a failure can be looked at again.
	uint64 Hash(uint64 Value)
	{
		Value ^= Value >> 33;
		Value *= 0xFF51AFD7ED558CCDull;
		Value ^= Value >> 33;
		Value *= 0xC4CEB9FE1A85EC53ull;
		Value ^= Value >> 33;
		return Value;
	}

	double Uniform(uint64 Value)
	{
		return static_cast<double>(Hash(Value) >> 11) / static_cast<double>(1ull << 53);
	}
}

bool ULedgerTerrainQueryCheck::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerTerrainQueryCheck::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("terrainquery"));
}

void ULedgerTerrainQueryCheck::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// Park the ship on the ground and keep it there.
	//
	// Not just "disable flight": the scripted flight is also running, and it
	// teleports the ship through its phases. Left alone, twelve seconds in it
	// is mid-descent at altitude and the collision ring is nowhere near the
	// ground -- the first run of this fixture traced into empty sky a thousand
	// times and reported a clean sweep of nothing. Re-placed every frame,
	// because the flight will keep moving it.
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr
		? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	if (ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn()))
	{
		Ship->SetFlightEnabled(false);
		const FVector3d Site = Builder->GetSiteDirection().GetSafeNormal();
		Ship->SetActorLocation(FVector(FVector3d(Planet->GetActorLocation())
			+ Site * (Planet->SurfaceRadiusAt(Site) + 4000.0)));
	}

	Waited += DeltaSeconds;
	if (Waited < QuerySettleSeconds)
	{
		return;
	}

	bRunning = false;
	const bool bHolds = Compare();
	UE_LOG(LogLedger, Log, TEXT("terrain query: %s"),
		bHolds ? TEXT("VERDICT PASS") : TEXT("VERDICT FAIL"));
	FPlatformMisc::RequestExit(false);
}

bool ULedgerTerrainQueryCheck::Compare()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr
		? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Planet == nullptr || Pawn == nullptr)
	{
		UE_LOG(LogLedger, Error, TEXT("terrain query: no planet or no pawn"));
		return false;
	}

	const FVector3d Origin = FVector3d(Planet->GetActorLocation());
	const FVector3d Centre = (FVector3d(Pawn->GetActorLocation()) - Origin).GetSafeNormal();

	FVector3d East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Centre);
	if (East.IsNearlyZero())
	{
		East = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), Centre);
	}
	East.Normalize();
	const FVector3d North = FVector3d::CrossProduct(Centre, East).GetSafeNormal();

	FString Body;
	Body += TEXT("The terrain sampling API against a physics trace (T061).\n\n");

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(LedgerTerrainQuery), true);
	TraceParams.AddIgnoredActor(Pawn);

	int32 Traced = 0;
	int32 Agreed = 0;
	int32 NoPatch = 0;
	int32 NoHit = 0;
	double WorstMm = 0.0;
	double TotalMm = 0.0;
	double WorstAt = 0.0;

	for (int32 Query = 0; Query < Queries; ++Query)
	{
		// Uniformly over a disc two kilometres across, which is inside the
		// collision ring and outside the patch the ship is standing on.
		const double Angle = Uniform(Query * 3 + 1) * LedgerTwoPi;
		const double Reach = FMath::Sqrt(Uniform(Query * 3 + 2)) * 100000.0;
		const FVector3d Direction = (Centre
			+ East * (Reach * FMath::Cos(Angle) / Planet->Radius)
			+ North * (Reach * FMath::Sin(Angle) / Planet->Radius)).GetSafeNormal();

		// Straight down the radial, from well above to well below.
		const double Ground = Planet->SurfaceRadiusAt(Direction);
		const FVector Start = FVector(Origin + Direction * (Ground + 50000.0));
		const FVector End = FVector(Origin + Direction * (Ground - 50000.0));

		// Every hit down the radial, and the first one that is the planet.
		//
		// A single trace takes whatever is nearest, and the ship is parked at
		// the settlement: four of the first thousand queries hit a rooftop and
		// reported the API as 29.5 m wrong about ground it was right about. The
		// buildings are real collision and the trace was correct; the question
		// being asked was not.
		TArray<FHitResult> Hits;
		World->LineTraceMultiByChannel(Hits, Start, End, ECC_WorldStatic, TraceParams);

		const FHitResult* Ground2 = nullptr;
		for (const FHitResult& Candidate : Hits)
		{
			if (Candidate.GetActor() == Planet)
			{
				Ground2 = &Candidate;
				break;
			}
		}
		if (Ground2 == nullptr)
		{
			++NoHit;
			continue;
		}
		const FHitResult& Hit = *Ground2;

		FLedgerTerrainSample Sample;
		if (!Planet->SampleTerrain(FVector3d(Hit.ImpactPoint), Sample))
		{
			// The trace hit something the API cannot answer about. That is a
			// disagreement, not an excuse, and it is counted as one.
			++NoPatch;
			continue;
		}

		++Traced;
		const double HitRadius = (FVector3d(Hit.ImpactPoint) - Origin).Length();
		const double ErrorMm = FMath::Abs(HitRadius - Sample.RadiusCm) * 10.0;
		TotalMm += ErrorMm;
		if (ErrorMm > WorstMm)
		{
			WorstMm = ErrorMm;
			WorstAt = Reach / 100.0;
		}
		if (ErrorMm <= 1.0)
		{
			++Agreed;
		}
	}

	Body += FString::Printf(
		TEXT("%d queries over a 2 km disc: %d traced and sampled, %d hit nothing, "
		     "%d hit ground the API could not answer about\n"),
		Queries, Traced, NoHit, NoPatch);
	Body += FString::Printf(
		TEXT("agreement: %d of %d within a millimetre (%.1f%%)\n"),
		Agreed, Traced, Traced > 0 ? 100.0 * Agreed / Traced : 0.0);
	Body += FString::Printf(
		TEXT("error: mean %.4f mm, worst %.4f mm (at %.0f m from the ship)\n"),
		Traced > 0 ? TotalMm / Traced : 0.0, WorstMm, WorstAt);

	// Every query that found ground must agree, and the run has to have found
	// ground -- a fixture that traced into empty sky a thousand times would
	// otherwise report a clean sweep of nothing.
	const bool bHolds = Traced >= Queries / 2 && NoPatch == 0 && Agreed == Traced;
	Body += FString::Printf(TEXT("\nVERDICT: %s\n"), bHolds ? TEXT("PASS") : TEXT("FAIL"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("terrain-query.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("terrain query -> %s"), *Path);
	return bHolds;
}

TStatId ULedgerTerrainQueryCheck::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerTerrainQueryCheck, STATGROUP_Tickables);
}
