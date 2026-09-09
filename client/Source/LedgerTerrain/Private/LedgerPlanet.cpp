#include "LedgerPlanet.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LedgerLog.h"
#include "LedgerPatchGenerator.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

// ---------------------------------------------------------------- actor

ALedgerPlanet::ALedgerPlanet()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

FLedgerTerrainParams ALedgerPlanet::TerrainParams() const
{
	FLedgerTerrainParams Params;
	Params.Seed = static_cast<uint32>(Seed);
	Params.Radius = Radius;
	Params.MaxElevation = MaxElevation;
	Params.SeaLevel = SeaLevel;
	return Params;
}

void ALedgerPlanet::BeginPlay()
{
	Super::BeginPlay();

	// Materials arrive from whoever spawned this actor rather than being built
	// here. That is one line of wiring at the composition root against a module
	// dependency for the whole terrain — and it is the right way round anyway:
	// a quadtree's business is geometry, and what it is painted with is not its
	// decision to make.
	if (SurfaceMaterial == nullptr)
	{
		SurfaceMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly"));
		UE_LOG(LogLedger, Warning,
			TEXT("planet spawned with no surface material; falling back to vertex colour"));
	}

	if (SurfaceMaterial != nullptr)
	{
		SurfaceInstance = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
	}

	UE_LOG(LogLedger, Log, TEXT("materials: terrain %s, water %s"),
		SurfaceMaterial != nullptr ? *SurfaceMaterial->GetName() : TEXT("<none>"),
		WaterMaterial != nullptr ? *WaterMaterial->GetName() : TEXT("<none>"));

	PatchCache.SetBudget(static_cast<int64>(PatchCacheBudgetMB * 1024.0 * 1024.0));

	BuildRoots();

	// Sized against the measured visible-leaf count with headroom for a fast
	// turn. Undersizing does not degrade gracefully on its own — the parent-hold
	// rule in `UpdateTree` is what stops a starved pool from punching holes.
	constexpr int32 PoolSize = 3600;
	ComponentKind = LedgerTerrain::PatchComponentKind();
	UE_LOG(LogLedger, Log, TEXT("terrain component type: %s (pool %d)"),
		LedgerTerrain::PatchComponentName(ComponentKind), PoolSize);

	MeshPool.Reserve(PoolSize);
	FreeSections.Reserve(PoolSize);
	for (int32 Index = 0; Index < PoolSize; ++Index)
	{
		SectionMeta.AddDefaulted();
		MeshPool.Add(LedgerTerrain::MakePatchComponent(*this, ComponentKind));
		FreeSections.Add(Index);
	}

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Ledger.Terrain.ResetPeaks"),
		TEXT("Clear the worst-case counters, so the next phase is measured on its own."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]
		{
			ALedgerPlanet* Self = const_cast<ALedgerPlanet*>(this);
			Self->Stats.WorstUnfilled = 0;
			Self->Stats.WorstUnfilledAt = 0.0;
			Self->Stats.WorstFrameUploadMs = 0.0;
			Self->Stats.WorstFrameCollisionMs = 0.0;
		}),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Ledger.Terrain.Stats"),
		TEXT("Report terrain LOD, generation and collision timings."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this] { LogStats(); }),
		ECVF_Default));

	UE_LOG(LogLedger, Log,
		TEXT("planet ready: radius %.0f km, relief %.1f km, sea level %.2f, max depth %d (~%.1f m quads)"),
		Radius / 100000.0,
		MaxElevation / 100000.0,
		SeaLevel,
		MaxDepth,
		(Radius * PI * 0.5) / FMath::Pow(2.0, static_cast<double>(MaxDepth)) / (GridResolution - 1) / 100.0);
}

void ALedgerPlanet::EndPlay(const EEndPlayReason::Type Reason)
{
	// Jobs hold a shared reference to their own state, so they finish harmlessly
	// after teardown — but nothing should be waiting on them.
	for (TPair<uint64, FLedgerPatchJobRef>& Entry : InFlight)
	{
		if (Entry.Value.IsValid())
		{
			Entry.Value->bAbandoned.store(true, std::memory_order_relaxed);
		}
	}
	InFlight.Empty();
	PatchCache.Empty();

	for (IConsoleObject* Command : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Command);
	}
	ConsoleCommands.Empty();

	Super::EndPlay(Reason);
}

void ALedgerPlanet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FVector CameraWorld = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	double Fov = FMath::DegreesToRadians(90.0);
	double ViewportWidth = 1920.0;

	if (const APlayerController* Controller = World->GetFirstPlayerController())
	{
		Controller->GetPlayerViewPoint(CameraWorld, CameraRotation);
		if (Controller->PlayerCameraManager != nullptr)
		{
			Fov = FMath::DegreesToRadians(Controller->PlayerCameraManager->GetFOVAngle());
		}
		int32 SizeX = 0;
		int32 SizeY = 0;
		Controller->GetViewportSize(SizeX, SizeY);
		if (SizeX > 0)
		{
			ViewportWidth = static_cast<double>(SizeX);
		}
	}

	const FVector3d CameraLocal = FVector3d(CameraWorld - GetActorLocation());

	// Velocity is what makes the collision cook *predictive* rather than
	// reactive. §6.8: cook ahead along the velocity vector, never on demand.
	if (DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		CameraVelocityLocal = (CameraLocal - LastCameraLocal) / static_cast<double>(DeltaSeconds);
	}
	LastCameraLocal = CameraLocal;

	// Where the camera will be in a few seconds, for the LOD to aim at.
	//
	// Clamped twice, and both clamps were learned the hard way.
	//
	// Against altitude, because a lead point below the surface asks for detail
	// underground. Against an absolute distance, because scaling the lead with
	// altitude means that at 300 km up, three seconds of climb is a lead point
	// 150 km away — and the LOD then subdivides a region nobody is flying to,
	// at a depth chosen by proximity to a point in empty space. That produced
	// fifteen hundred patches of demand that never fell, on a pool of 3,600,
	// growing for as long as the climb continued.
	//
	// Prefetch earns its keep near the ground, where a patch is metres across
	// and arriving before it exists is the whole problem. Five kilometres is
	// sixteen seconds at cruise and more than the deepest LOD ever needs.
	constexpr double MaxLeadDistance = 500000.0;   // 5 km
	const double AltitudeAbove = FMath::Max(
		1.0, CameraLocal.Length() - SurfaceRadiusAt(CameraLocal.GetSafeNormal()));

	FVector3d LeadOffset = CameraVelocityLocal * GeometryLeadSeconds;
	const double LeadLength = LeadOffset.Length();
	const double LeadLimit = FMath::Min(MaxLeadDistance, AltitudeAbove * 0.5);
	if (LeadLength > LeadLimit)
	{
		LeadOffset *= LeadLimit / LeadLength;
	}
	const FVector3d GeometryLead = CameraLocal + LeadOffset;

	// Before the tree walks: the blend has to be told the same thing the LOD
	// decision is about to be told, in the same frame.
	const double TickStarted = FPlatformTime::Seconds();
	UpdateMorphParameters(ViewportWidth, Fov);

	// After streaming, so a patch that arrived this frame is scattered this
	// frame rather than next. Returns immediately unless the near set changed.
	RebuildScatter();
	double PhaseStarted = FPlatformTime::Seconds();

	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		UpdateTree(*RootNode, CameraLocal, GeometryLead, ViewportWidth, Fov, false);
	}

	Stats.WorstTreeMs = FMath::Max(Stats.WorstTreeMs,
		(FPlatformTime::Seconds() - PhaseStarted) * 1000.0);

	PhaseStarted = FPlatformTime::Seconds();
	HarvestCompletedPatches();
	Stats.WorstHarvestMs = FMath::Max(Stats.WorstHarvestMs,
		(FPlatformTime::Seconds() - PhaseStarted) * 1000.0);

	PhaseStarted = FPlatformTime::Seconds();

	TArray<const FLedgerQuadNode*> Leaves;
	Leaves.Reserve(2048);
	Stats.UnfilledNodes = 0;
	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		CollectLeaves(*RootNode, Leaves, /*bAncestorHasGeometry*/ false);
	}

	Stats.VisibleNodes = Leaves.Num();
	if (Stats.UnfilledNodes > Stats.WorstUnfilled)
	{
		Stats.WorstUnfilled = Stats.UnfilledNodes;
		Stats.WorstUnfilledAt = World->GetTimeSeconds();
	}
	Stats.DeepestVisibleDepth = 0;

	// Where the camera will be shortly, so the cook has landed by the time we
	// arrive rather than starting when we get there.
	const FVector3d PredictedLocal = CameraLocal + CameraVelocityLocal * CollisionLeadSeconds;

	// Nearest first: if the queue runs out, it runs out on the far nodes.
	Stats.WorstCollectMs = FMath::Max(Stats.WorstCollectMs,
		(FPlatformTime::Seconds() - PhaseStarted) * 1000.0);

	PhaseStarted = FPlatformTime::Seconds();
	Leaves.Sort([&CameraLocal](const FLedgerQuadNode& A, const FLedgerQuadNode& B)
	{
		return FVector3d::DistSquared(A.Centre, CameraLocal) < FVector3d::DistSquared(B.Centre, CameraLocal);
	});

	int32 Pending = 0;
	int32 WithCollision = 0;

	// A cache hit is cheap next to generating a patch and expensive next to
	// doing nothing: SetProcMeshSection still rebuilds render resources on the
	// game thread. Unbudgeted, a turn that brings several hundred cached
	// patches back into view would serve all of them in one frame, and the
	// cache would have turned a smooth stream of work into a single stall.
	const double CacheUploadStart = FPlatformTime::Seconds();
	const double CacheUploadBudgetMs = FMath::Max(0.0, UploadBudgetMs - Stats.LastFrameUploadMs);

	for (const FLedgerQuadNode* Leaf : Leaves)
	{
		Stats.DeepestVisibleDepth = FMath::Max(Stats.DeepestVisibleDepth, Leaf->Depth);

		const uint64 Key = NodeKey(*Leaf);
		const double NearNow = FVector3d::Distance(Leaf->Centre, CameraLocal);
		const double NearSoon = FVector3d::Distance(Leaf->Centre, PredictedLocal);
		const bool bWantsCollision = FMath::Min(NearNow, NearSoon) < CollisionRadius;
		if (bWantsCollision)
		{
			++WithCollision;
		}

		if (ActiveSections.Contains(Key) || InFlight.Contains(Key))
		{
			continue;
		}

		// Cache first. A hit costs an upload; a miss costs a worker thread and
		// several milliseconds of sampling.
		if ((FPlatformTime::Seconds() - CacheUploadStart) * 1000.0 < CacheUploadBudgetMs
			&& UploadFromCache(*Leaf, bWantsCollision))
		{
			continue;
		}

		if (!LaunchPatch(*Leaf, bWantsCollision))
		{
			++Pending;
		}
	}

	Stats.LastFrameUploadMs += (FPlatformTime::Seconds() - CacheUploadStart) * 1000.0;
	Stats.WorstFrameUploadMs = FMath::Max(Stats.WorstFrameUploadMs, Stats.LastFrameUploadMs);

	Stats.PendingBuilds = Pending;
	// Measure the imbalance before building anything to fix it. The stitching
	// handles a one-level difference; whether a larger one ever arises is a
	// question with an answer, and the answer decides whether the tree needs a
	// balancing pass at all.
	Stats.WorstSortMs = FMath::Max(Stats.WorstSortMs,
		(FPlatformTime::Seconds() - PhaseStarted) * 1000.0);

	PhaseStarted = FPlatformTime::Seconds();
	Stats.ImbalancedEdges = 0;
	Stats.WorstDepthDifference = 0;
	for (const FLedgerQuadNode* Leaf : Leaves)
	{
		// Only nodes that are actually drawn. The collected set contains a split
		// parent *and* its children while the children stream, so comparing
		// every entry against its neighbour compares nodes that are not both on
		// screen — which is how this counter first read 43% of all edges
		// imbalanced on terrain with no visible crack in it.
		if (!ActiveSections.Contains(NodeKey(*Leaf)))
		{
			continue;
		}

		const double Probes[4][2] = { {-0.02, 0.5}, {1.02, 0.5}, {0.5, -0.02}, {0.5, 1.02} };
		for (const double(&Probe)[2] : Probes)
		{
			const int32 Difference = Leaf->Depth - LeafDepthAtFace(Leaf->Face, Leaf->U + Probe[0] * Leaf->Extent, Leaf->V + Probe[1] * Leaf->Extent);
			if (Difference > 1)
			{
				++Stats.ImbalancedEdges;
				Stats.WorstDepthDifference = FMath::Max(Stats.WorstDepthDifference, Difference);
			}
		}
	}

	Stats.WorstImbalanceMs = FMath::Max(Stats.WorstImbalanceMs,
		(FPlatformTime::Seconds() - PhaseStarted) * 1000.0);
	Stats.LastTickMs = (FPlatformTime::Seconds() - TickStarted) * 1000.0;
	Stats.WorstTickMs = FMath::Max(Stats.WorstTickMs, Stats.LastTickMs);

	Stats.SectionsActive = ActiveSections.Num();
	Stats.SectionsFree = FreeSections.Num();
	Stats.SectionsPending = InFlight.Num();
	Stats.NodesWithCollision = WithCollision;
	Stats.JobsInFlight = InFlight.Num();

	// Trace every few seconds for the first minute. Under motion this is the
	// only way to see whether the pipeline is keeping up (§6.8).
	const double Now = World->GetTimeSeconds();
	if (Now >= NextTraceAt)
	{
		NextTraceAt = Now + 5.0;
		LogStats();
	}
}
