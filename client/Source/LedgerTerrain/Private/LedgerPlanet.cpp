#include "LedgerPlanet.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LedgerLog.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "LedgerPatchGenerator.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "LedgerMath.h"

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
	Params.AxialTiltRadians = AxialTiltRadians;
	Params.Delta = Delta;
	return Params;
}

void ALedgerPlanet::LevelPad(const FVector3d& Direction, double RadiusMetres,
	double FalloffMetres, double TargetAltitudeMetres)
{
	if (!Delta.IsValid())
	{
		Delta = MakeShared<FLedgerTerrainDelta>();
	}

	FLedgerTerrainEdit Edit;
	Edit.Centre = Direction.GetSafeNormal();
	Edit.RadiusMetres = RadiusMetres;
	Edit.FalloffMetres = FalloffMetres;
	Edit.TargetAltitudeMetres = TargetAltitudeMetres;
	Delta = Delta->With(Edit);

	FFileHelper::SaveStringToFile(Delta->ToJson(), *FLedgerTerrainDelta::DefaultPath());
	UE_LOG(LogLedger, Log, TEXT("terrain delta: %d edits -> %s"),
		Delta->Num(), *FLedgerTerrainDelta::DefaultPath());

	InvalidateTerrain();
}

void ALedgerPlanet::InvalidateTerrain()
{
	// Everything, live and cached. A cached patch was generated against the
	// old height function and there is nothing in its key that says so -- the
	// alternative is a key that carries a delta version, which is a bigger key
	// on every patch to save a rebuild that happens when somebody levels
	// ground and at no other time.
	TArray<uint64> Live;
	ActiveSections.GetKeys(Live);
	for (const uint64 Key : Live)
	{
		ReleaseSection(Key);
	}
	PatchCache.Empty();
	Stats.CacheEntries = 0;
	Stats.CacheMegabytes = 0.0;
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

	// Load whatever anybody has done to this ground, before the first patch is
	// generated. An empty or missing file gives an empty delta, which costs one
	// pointer test per height sample.
	{
		FString Saved;
		if (FFileHelper::LoadFileToString(Saved, *FLedgerTerrainDelta::DefaultPath()))
		{
			Delta = FLedgerTerrainDelta::FromJson(Saved);
			UE_LOG(LogLedger, Log, TEXT("terrain delta: %d edits loaded from %s"),
				Delta->Num(), *FLedgerTerrainDelta::DefaultPath());
		}
		else
		{
			Delta = MakeShared<FLedgerTerrainDelta>();
		}
	}

	PatchCache.SetBudget(static_cast<int64>(PatchCacheBudgetMB * 1024.0 * 1024.0));

	BuildRoots();

	// Sized against the measured visible-leaf count with headroom for a fast
	// turn. Undersizing does not degrade gracefully on its own — the parent-hold
	// rule in `UpdateTree` is what stops a starved pool from punching holes.
	// 3,600 was sized for MaxDepth 15. T429 took it to 18 and did not resize
	// this, and the consequence was not subtle: at 1920x1080 the tree wants
	// about seven thousand sections, the flight reported 3,363 unfilled nodes,
	// and the ground was full of holes. Worse than the holes, the starvation
	// made the LOD brake collapse patches hard and unevenly, so neighbours
	// ended up several depths apart -- which is the flat wedge cut into the
	// mountain in the sweep capture, and the terrain "sliding" as the rings
	// swept past.
	//
	// Screen-space error scales with viewport width, so this is a function of
	// resolution: the same build at 1280x720 reported zero holes, which is why
	// the first measurement of T429 missed it entirely.
	// Left at 3,600, and 8,000 was tried. It made everything worse: holes went
	// from 1,774 to 2,960 and p99 from 84 to 124 ms, because the shortage is
	// not sections, it is the rate at which patches can be generated, cooked
	// and uploaded. A bigger pool just puts more work in flight.
	ApplyRegressionFaults();

	// `-smallpool` is a deliberate fault for T067: four hundred sections cannot
	// serve any view, so the ground fills with holes. -nolodbrake used to do
	// this and no longer does, because raising ErrorThresholdPixels to 250 cut
	// demand enough that the brake is not what stands between the tree and the
	// pool any more. A fault that has stopped faulting is worse than none.
	const int32 PoolSize =
		FParse::Param(FCommandLine::Get(), TEXT("smallpool")) ? 400 : 3600;
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
		(Radius * LedgerPi * 0.5) / FMath::Pow(2.0, static_cast<double>(MaxDepth)) / (GridResolution - 1) / 100.0);
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

	// How fine the tree may go this frame.
	//
	// The pool is the hard limit: sections it does not have cannot be filled,
	// and a visible set larger than the pool is holes by arithmetic rather than
	// by any failure to stream. So the threshold rises with the overshoot and
	// relaxes back when there is room -- a coarser planet for a second, not a
	// planet with pieces missing.
	{
		// **Pool occupancy, not the visible-node count.** `bVisible` is a
		// horizon test rather than a frustum one, so it counts ground behind
		// the camera; driving the threshold off it throttled detail because of
		// terrain nobody could see. Sections in use against sections that
		// exist is unambiguous: it is the resource that actually runs out.
		// Ninety-five per cent, not eighty. At eighty the flight's ordinary
		// cruise sits above the target and the threshold creeps up all the
		// time: the underwater capture came back as a featureless gradient
		// because the seabed had been coarsened during a frame that was never
		// in trouble. This is a brake for an emergency, and a brake that drags
		// is worse than none.
		// `-nolodbrake` turns the brake off, as a control arm. It exists
		// because the brake is a feedback loop on the LOD tree and a feedback
		// loop can oscillate: giving the planet real relief pushed section
		// occupancy up to the point where this engages, and the terrain's
		// game-thread cost went from 1.3 ms to 11 with the patch cache running
		// at 88% reuse over 158,000 hits -- the signature of patches cycling in
		// and out rather than of more work.
		static const bool bNoBrake =
			FParse::Param(FCommandLine::Get(), TEXT("nolodbrake"));
		const double Target = bNoBrake
			? TNumericLimits<double>::Max()
			: MeshPool.Num() * 0.95;
		// **With a dead band, because this is a feedback loop on the LOD tree.**
		// Without one it chases: raising the threshold collapses nodes, which
		// frees sections, which lowers the threshold, which splits them again.
		// Giving the planet real relief (T428) pushed occupancy to where that
		// starts, and the terrain's game-thread cost went from 1.3 ms to 11
		// with the patch cache at 88% reuse over 158,000 hits -- patches
		// cycling, not work being done. Off the brake it was 8; with a dead
		// band it is 8 and the brake still catches a genuine overload.
		// **Demand, not supply.**
		//
		// This was ActiveSections.Num() / Target, and ActiveSections is capped
		// by the pool -- so however badly the tree out-runs it, occupancy could
		// never read much above 1.0 and the brake could never apply much more
		// than a 5% correction. At 1080p after T429 took MaxDepth to 18 the
		// tree wanted about seven thousand sections against a pool of 3,600,
		// the flight reported 3,363 unfilled nodes, and the ground was full of
		// holes -- while the brake, looking only at what it had been given,
		// saw occupancy 1.05 and nudged the threshold from 150 to 157.
		//
		// Unfilled nodes are the rest of the demand. Adding them makes
		// occupancy read 2.0 in that state, which is what it actually was.
		const double Demand =
			static_cast<double>(ActiveSections.Num() + Stats.UnfilledNodes);
		const double Occupancy = Target > 0.0 ? Demand / Target : 0.0;
		double Wanted = EffectiveErrorPixels;
		if (Occupancy > 1.0)
		{
			Wanted = ErrorThresholdPixels * Occupancy;
		}
		else if (Occupancy < 0.85)
		{
			// Only all the way back down, and only when there is real room.
			Wanted = ErrorThresholdPixels;
		}

		// Eased rather than snapped. Jumping the threshold makes the whole
		// visible set collapse and re-split in one frame, which costs more than
		// the overshoot did.
		EffectiveErrorPixels = EffectiveErrorPixels <= 0.0
			? ErrorThresholdPixels
			: FMath::Lerp(EffectiveErrorPixels, Wanted, 0.15);
		Stats.EffectiveErrorPixels = EffectiveErrorPixels;

		// Logged because the first version of this did nothing measurable and
		// there was no way to see whether the threshold was moving at all.
		UE_LOG(LogLedger, VeryVerbose,
			TEXT("lod: %d sections of %.0f target, threshold %.0f px"),
			ActiveSections.Num(), Target, EffectiveErrorPixels);
	}

	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		UpdateTree(*RootNode, CameraLocal, GeometryLead, ViewportWidth, Fov, false);
	}

	Stats.WorstTreeMs = FMath::Max(Stats.WorstTreeMs,
		(FPlatformTime::Seconds() - PhaseStarted) * 1000.0);

	PhaseStarted = FPlatformTime::Seconds();
	// One allowance for the whole frame's streaming, opened before anything
	// spends it. Collision work is exempt and reported separately.
	Budget.Begin(UploadBudgetMs);

	HarvestCompletedPatches();
	Stats.WorstHarvestMs = FMath::Max(Stats.WorstHarvestMs,
		(FPlatformTime::Seconds() - PhaseStarted) * 1000.0);

	PhaseStarted = FPlatformTime::Seconds();

	TArray<const FLedgerQuadNode*> Leaves;
	TArray<uint8> Urgent;
	Leaves.Reserve(2048);
	Urgent.Reserve(2048);
	Stats.UnfilledNodes = 0;
	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		CollectLeaves(*RootNode, Leaves, Urgent, /*bAncestorHasGeometry*/ false);
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

	Stats.WorstCollectMs = FMath::Max(Stats.WorstCollectMs,
		(FPlatformTime::Seconds() - PhaseStarted) * 1000.0);

	PhaseStarted = FPlatformTime::Seconds();

	// ---- what needs work, and in what order ------------------------------
	//
	// **By class first, then by distance.** Nearest-first alone is not a
	// priority: a hole three kilometres away matters more than a detail level
	// on the next hillside, because one is ground missing and the other is
	// ground that is merely coarse. Given a budget too small for everything,
	// arrival order lets the horizon starve the floor, which is the failure
	// the acceptance calls "holes" rather than "less detail".
	struct FRequest
	{
		const FLedgerQuadNode* Leaf = nullptr;
		ELedgerStreamClass Class = ELedgerStreamClass::Detail;
		double DistanceSquared = 0.0;
	};

	TArray<FRequest> Requests;
	Requests.Reserve(Leaves.Num());

	int32 WithCollision = 0;
	for (int32 Index = 0; Index < Leaves.Num(); ++Index)
	{
		const FLedgerQuadNode* Leaf = Leaves[Index];
		Stats.DeepestVisibleDepth = FMath::Max(Stats.DeepestVisibleDepth, Leaf->Depth);

		const double NearNow = FVector3d::Distance(Leaf->Centre, CameraLocal);
		const double NearSoon = FVector3d::Distance(Leaf->Centre, PredictedLocal);
		const bool bWantsCollision = FMath::Min(NearNow, NearSoon) < CollisionRadius;
		if (bWantsCollision)
		{
			++WithCollision;
		}

		const uint64 Key = NodeKey(*Leaf);

		// **An active section never gains collision, and that is a real defect
		// this does not fix.**
		//
		// A patch built without collision is skipped here forever after. At
		// 900 m/s the geometry lead builds ground several seconds before the
		// ship is within the three kilometre collision radius, so nearly every
		// patch the ship passes over was built while it was still far away --
		// with collision correctly declined -- and is then never reconsidered.
		// The 200 km transect measures the consequence: 1,569 of 2,804 visible
		// nodes want collision and a downward trace misses on 89% of frames.
		//
		// Re-requesting those patches was tried and made it worse, 89% to
		// 99.8%: every frame re-asks for more than the budget can serve, so
		// sections churn instead of settling and the ground under the ship is
		// rebuilt rather than kept. The fix is not to ask again -- it is either
		// to cook collision for what the lead builds (paying for it on ground
		// that may never be flown over) or to upgrade a section in place
		// without a rebuild, which the procedural mesh path cannot do today.
		// Written down rather than half-done. See docs/comparisons/transect/.
		if (ActiveSections.Contains(Key) || InFlight.Contains(Key))
		{
			continue;
		}

		FRequest Request;
		Request.Leaf = Leaf;
		Request.DistanceSquared = FVector3d::DistSquared(Leaf->Centre, CameraLocal);
		Request.Class = bWantsCollision
			? ELedgerStreamClass::Collision
			: (Urgent.IsValidIndex(Index) && Urgent[Index] != 0
				? ELedgerStreamClass::Hole
				: ELedgerStreamClass::Detail);
		Requests.Add(Request);
	}

	Requests.Sort([](const FRequest& A, const FRequest& B)
	{
		if (A.Class != B.Class)
		{
			return A.Class < B.Class;
		}
		return A.DistanceSquared < B.DistanceSquared;
	});

	// ---- spend ------------------------------------------------------------
	//
	// A cache hit is cheap next to generating a patch and expensive next to
	// doing nothing: SetProcMeshSection still rebuilds render resources on the
	// game thread. Unbudgeted, a turn that brings several hundred cached
	// patches back into view serves all of them in one frame, and the cache
	// has turned a smooth stream of work into a single stall.
	int32 Pending = 0;
	for (const FRequest& Request : Requests)
	{
		if (!Budget.Allows(Request.Class))
		{
			Budget.Refused(Request.Class);
			++Pending;
			continue;
		}

		const double Started = FPlatformTime::Seconds();
		const bool bCollision = Request.Class == ELedgerStreamClass::Collision;
		if (!UploadFromCache(*Request.Leaf, bCollision) && !LaunchPatch(*Request.Leaf, bCollision))
		{
			++Pending;
		}
		Budget.Spent(Request.Class, (FPlatformTime::Seconds() - Started) * 1000.0);
	}

	Stats.SpentCollisionMs = Budget.SpentMs(ELedgerStreamClass::Collision);
	Stats.SpentHoleMs = Budget.SpentMs(ELedgerStreamClass::Hole);
	Stats.SpentDetailMs = Budget.SpentMs(ELedgerStreamClass::Detail);
	Stats.RefusedDetail = Budget.RefusedCount(ELedgerStreamClass::Detail);
	Stats.RefusedSpeculative = Budget.RefusedCount(ELedgerStreamClass::Speculative);

	Stats.LastFrameUploadMs = Budget.TotalSpentMs();
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

void ALedgerPlanet::ApplyRegressionFaults()
{
	// T067's holes fault, and it reproduces the regression that happened
	// rather than an invented one. See the header.
	if (FParse::Param(FCommandLine::Get(), TEXT("breakthreshold")))
	{
		ErrorThresholdPixels = 150.0;
		UE_LOG(LogLedger, Warning,
			TEXT("regression fault: ErrorThresholdPixels forced to 150"));
	}
}

