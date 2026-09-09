// Getting geometry onto the screen and off it again: launching jobs,
// harvesting finished ones, uploading from the cache, and returning sections to
// the pool.
//
// The invariant this file exists to keep is that a section is either free or
// live and never both, never neither. It has been broken twice, and both times
// the symptom surfaced somewhere else — as patches that could not be filled,
// hundreds of frames later and several systems away.

#include "LedgerPlanet.h"

#include "LedgerQuadNode.h"

#include "Async/Async.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "LedgerLog.h"
#include "LedgerPatchGenerator.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "ProceduralMeshComponent.h"

bool ALedgerPlanet::LaunchPatch(const FLedgerQuadNode& Node, bool bWithCollision)
{
	if (FreeSections.Num() == 0 || InFlight.Num() >= MaxJobsInFlight)
	{
		return false;
	}

	FLedgerPatchJobRef Job = MakeShared<FLedgerPatchJob, ESPMode::ThreadSafe>();
	Job->Key = NodeKey(Node);
	++Stats.CacheMisses;
	Job->SectionIndex = FreeSections.Pop();
	Job->bWithCollision = bWithCollision;
	Job->Face = Node.Face;
	Job->U = Node.U;
	Job->V = Node.V;
	Job->Extent = Node.Extent;
	Job->Centre = Node.Centre;
	Job->Params = TerrainParams();
	Job->Side = GridResolution;
	Job->WorldSize = Node.WorldSize;
	Job->Biomes = Biomes;

	Job->SeasonPhase = SeasonPhase();

	// Neighbour depths are read here, on the game thread, while the tree is
	// stable. The worker never touches the tree.
	Job->bStitchLeft = LeafDepthAtFace(Node.Face, Node.U - 0.02 * Node.Extent, Node.V + 0.5 * Node.Extent) < Node.Depth;
	Job->bStitchRight = LeafDepthAtFace(Node.Face, Node.U + 1.02 * Node.Extent, Node.V + 0.5 * Node.Extent) < Node.Depth;
	Job->bStitchBottom = LeafDepthAtFace(Node.Face, Node.U + 0.5 * Node.Extent, Node.V - 0.02 * Node.Extent) < Node.Depth;
	Job->bStitchTop = LeafDepthAtFace(Node.Face, Node.U + 0.5 * Node.Extent, Node.V + 1.02 * Node.Extent) < Node.Depth;

	InFlight.Add(Job->Key, Job);

	// The lambda captures the shared reference, so the job outlives the actor if
	// it has to. Nothing inside it touches `this`.
	Async(EAsyncExecution::ThreadPool, [Job]()
	{
		LedgerGeneratePatch(*Job);
	});

	return true;
}

void ALedgerPlanet::HarvestCompletedPatches()
{
	const double Started = FPlatformTime::Seconds();
	double Previous = Started;

	TArray<uint64> Landed;
	Landed.Reserve(InFlight.Num());

	for (TPair<uint64, FLedgerPatchJobRef>& Entry : InFlight)
	{
		FLedgerPatchJob* Job = Entry.Value.Get();
		if (Job == nullptr || !Job->bComplete.load(std::memory_order_acquire))
		{
			continue;
		}

		Landed.Add(Entry.Key);

		if (Job->bAbandoned.load(std::memory_order_relaxed))
		{
			FreeSections.Add(Job->SectionIndex);
			continue;
		}

		UMeshComponent* Mesh = MeshPool[Job->SectionIndex];
		Mesh->SetWorldLocation(GetActorLocation() + FVector(Job->Centre));

		// Section 1 is the sea. Same component, so it moves and culls with the
		// land it belongs to and costs no extra transform.
		const double UploadStart = FPlatformTime::Seconds();
		LedgerTerrain::UploadPatch(*Mesh, *Job, TerrainMaterial(Job->Palette), WaterMaterial);
		if (Job->bHasWater)
		{
			++Stats.WaterSections;
		}
		if (Job->bWithCollision)
		{
			Stats.WorstFrameCollisionMs = FMath::Max(
				Stats.WorstFrameCollisionMs, (FPlatformTime::Seconds() - UploadStart) * 1000.0);
		}

		Mesh->SetCollisionEnabled(Job->bWithCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
		Mesh->SetVisibility(true);

		ActiveSections.Add(Job->Key, Job->SectionIndex);
		if (Job->Scatter.Num() > 0)
		{
			FPatchScatter Scattered;
			Scattered.Centre = Job->Centre;
			Scattered.Instances = MoveTemp(Job->Scatter);
			LiveScatter.Add(Job->Key, MoveTemp(Scattered));
			ScatterBucketDirty[ScatterBucketOf(Job->Key)] = true;
		}
		SectionMeta[Job->SectionIndex] = FLedgerSectionMeta{
			Job->Key, Job->Centre,
			Job->bStitchLeft, Job->bStitchRight, Job->bStitchBottom, Job->bStitchTop,
			Job->Palette };

		Stats.LastPatchGenerationMs = Job->GenerationMs;
		++Stats.TotalBuilds;

		// The same allowance the request loop spends from, so a frame that
		// harvests a hundred finished patches has less left to ask for more.
		// Each patch is charged its own time rather than the elapsed total,
		// which is the difference between a budget and a stopwatch.
		const double Now = FPlatformTime::Seconds();
		Budget.Spent(Job->bWithCollision
			? ELedgerStreamClass::Collision : ELedgerStreamClass::Detail,
			(Now - Previous) * 1000.0);
		Previous = Now;

		// A finished patch with collision is uploaded whatever the budget says:
		// the work is already done on the worker and the alternative is ground
		// the player can fall through.
		if (!Budget.Allows(ELedgerStreamClass::Detail))
		{
			break;
		}
	}

	for (const uint64 Key : Landed)
	{
		InFlight.Remove(Key);
	}

	Stats.LastFrameUploadMs = (FPlatformTime::Seconds() - Started) * 1000.0;
	Stats.WorstFrameUploadMs = FMath::Max(Stats.WorstFrameUploadMs, Stats.LastFrameUploadMs);
}

UProceduralMeshComponent* ALedgerPlanet::PooledProcedural(int32 SectionIndex) const
{
	return MeshPool.IsValidIndex(SectionIndex)
		? Cast<UProceduralMeshComponent>(MeshPool[SectionIndex].Get())
		: nullptr;
}

bool ALedgerPlanet::UploadFromCache(const FLedgerQuadNode& Node, bool bWithCollision)
{
	// The cache holds FProcMeshSection, which is the procedural component's own
	// interleaved buffer. There is no equivalent for a static mesh built at
	// runtime -- its render data is built once and cannot be handed back -- so
	// under that backend every patch is regenerated. That is a difference
	// between the candidates rather than a gap in the measurement, and it is
	// reported as one.
	if (ComponentKind != ELedgerPatchComponent::Procedural || FreeSections.Num() == 0)
	{
		return false;
	}

	const uint64 Key = NodeKey(Node);
	FLedgerCachedPatch* Found = PatchCache.Take(Key);
	if (Found == nullptr)
	{
		return false;
	}

	// Neighbour depths, read the same way LaunchPatch reads them. A patch whose
	// coarser neighbour has subdivided since needs different edge geometry, and
	// the cached buffers would leave a crack along that edge.
	FLedgerCachedPatch& Entry = *Found;
	if (Entry.bStitchLeft != (LeafDepthAtFace(Node.Face, Node.U - 0.02 * Node.Extent, Node.V + 0.5 * Node.Extent) < Node.Depth)
		|| Entry.bStitchRight != (LeafDepthAtFace(Node.Face, Node.U + 1.02 * Node.Extent, Node.V + 0.5 * Node.Extent) < Node.Depth)
		|| Entry.bStitchBottom != (LeafDepthAtFace(Node.Face, Node.U + 0.5 * Node.Extent, Node.V - 0.02 * Node.Extent) < Node.Depth)
		|| Entry.bStitchTop != (LeafDepthAtFace(Node.Face, Node.U + 0.5 * Node.Extent, Node.V + 1.02 * Node.Extent) < Node.Depth))
	{
		PatchCache.Remove(Key);
		Stats.CacheEntries = PatchCache.Num();
		return false;
	}

	const int32 SectionIndex = FreeSections.Pop();
	UProceduralMeshComponent* Mesh = PooledProcedural(SectionIndex);
	Mesh->SetWorldLocation(GetActorLocation() + FVector(Entry.Centre));

	if (Entry.Scatter.Num() > 0)
	{
		FPatchScatter Scattered;
		Scattered.Centre = Entry.Centre;
		Scattered.Instances = Entry.Scatter;
		LiveScatter.Add(Key, MoveTemp(Scattered));
		ScatterBucketDirty[ScatterBucketOf(Key)] = true;
	}

	Entry.Land.bEnableCollision = bWithCollision;
	Mesh->SetProcMeshSection(0, Entry.Land);
	if (SurfaceMaterial != nullptr)
	{
		Mesh->SetMaterial(0, TerrainMaterial(Entry.Palette));
	}

	if (Entry.bHasWater)
	{
		++Stats.WaterSections;
		Mesh->SetProcMeshSection(1, Entry.Water);
		if (WaterMaterial != nullptr)
		{
			Mesh->SetMaterial(1, WaterMaterial);
		}
	}
	else
	{
		Mesh->ClearMeshSection(1);
	}

	Mesh->SetCollisionEnabled(bWithCollision
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision);
	Mesh->SetVisibility(true);

	ActiveSections.Add(Key, SectionIndex);
	SectionMeta[SectionIndex] = FLedgerSectionMeta{
		Key, Entry.Centre,
		Entry.bStitchLeft, Entry.bStitchRight, Entry.bStitchBottom, Entry.bStitchTop,
		Entry.Palette };

	// Removed, not kept: the geometry is on screen again and holding a second
	// copy of it is exactly the waste this cache is shaped to avoid.
	PatchCache.Remove(Key);

	Stats.CacheEntries = PatchCache.Num();
	Stats.CacheMegabytes = static_cast<double>(PatchCache.Bytes()) / (1024.0 * 1024.0);
	++Stats.CacheHits;
	return true;
}

UMaterialInterface* ALedgerPlanet::TerrainMaterial(const FLedgerBiomePalette& Palette)
{
	// No biomes on this patch, or nobody able to bind them: the plain instance,
	// which is what the whole planet used before T053.
	if (Palette.IsEmpty() || !PaletteMaterial.IsBound() || !Biomes.IsValid())
	{
		return SurfaceInstance != nullptr
			? Cast<UMaterialInterface>(SurfaceInstance)
			: SurfaceMaterial.Get();
	}

	const uint32 Key = Palette.Key();
	if (const TObjectPtr<UMaterialInstanceDynamic>* Existing = PaletteInstances.Find(Key))
	{
		return *Existing;
	}

	UMaterialInstanceDynamic* Instance = PaletteMaterial.Execute(Palette, *Biomes);
	if (Instance == nullptr)
	{
		// Do not cache the failure: whatever went wrong is worth retrying, and
		// a null in the map would paint this palette with nothing forever.
		return SurfaceInstance != nullptr
			? Cast<UMaterialInterface>(SurfaceInstance)
			: SurfaceMaterial.Get();
	}

	PaletteInstances.Add(Key, Instance);
	// A palette created between frames would otherwise render one frame with an
	// unset morph scale, which reads as every patch on that palette popping.
	ApplyMorphParameters(*Instance);
	return Instance;
}

void ALedgerPlanet::UpdateMorphParameters(double ViewportWidth, double FovRadians)
{
	// Same projection the screen-space error metric uses, rearranged.
	//
	// Error in pixels is NodeWorldSize * Scale / Distance, so a node is at its
	// threshold when Distance equals NodeWorldSize * Scale. Handing the shader
	// that one number lets it work out how close any patch is to collapsing
	// from the patch's own size, with nothing per-patch to set.
	const double HalfFov = FMath::Max(FovRadians * 0.5, 0.001);
	const double Scale = (ViewportWidth * 0.5) / (FMath::Tan(HalfFov) * FMath::Max(ErrorThresholdPixels, 1.0));

	MorphScale = Scale;

	if (SurfaceInstance != nullptr)
	{
		ApplyMorphParameters(*SurfaceInstance);
	}
	for (const TPair<uint32, TObjectPtr<UMaterialInstanceDynamic>>& Palette : PaletteInstances)
	{
		if (Palette.Value != nullptr)
		{
			ApplyMorphParameters(*Palette.Value);
		}
	}
}

void ALedgerPlanet::ApplyMorphParameters(UMaterialInstanceDynamic& Instance) const
{
	Instance.SetScalarParameterValue(TEXT("MorphScale"), static_cast<float>(MorphScale));
	Instance.SetVectorParameterValue(TEXT("PlanetCentre"), FLinearColor(
		static_cast<float>(GetActorLocation().X),
		static_cast<float>(GetActorLocation().Y),
		static_cast<float>(GetActorLocation().Z),
		0.0f));
}

void ALedgerPlanet::ReleaseSection(uint64 Key)
{
	if (LiveScatter.Remove(Key) > 0)
	{
		ScatterBucketDirty[ScatterBucketOf(Key)] = true;
	}

	int32 SectionIndex = INDEX_NONE;
	if (!ActiveSections.RemoveAndCopyValue(Key, SectionIndex))
	{
		return;
	}
	if (MeshPool.IsValidIndex(SectionIndex))
	{
		UProceduralMeshComponent* Mesh = PooledProcedural(SectionIndex);

		// Take the geometry on the way out. This is the only moment it can be
		// taken: after ClearMeshSection it is gone, and before release it is
		// still on screen and not worth a second copy.
		const FProcMeshSection* Land = Mesh != nullptr ? Mesh->GetProcMeshSection(0) : nullptr;
		if (Land != nullptr)
		{
			TSharedPtr<FLedgerCachedPatch> Entry = MakeShared<FLedgerCachedPatch>();
			const FLedgerSectionMeta& Meta = SectionMeta[SectionIndex];
			Entry->Centre = Meta.Centre;
			Entry->bStitchLeft = Meta.bStitchLeft;
			Entry->bStitchRight = Meta.bStitchRight;
			Entry->bStitchBottom = Meta.bStitchBottom;
			Entry->bStitchTop = Meta.bStitchTop;
			Entry->Palette = Meta.Palette;
			if (const FPatchScatter* Scattered = LiveScatter.Find(Key))
			{
				Entry->Scatter = Scattered->Instances;
			}
			Entry->Land = *Land;

			if (const FProcMeshSection* Water = Mesh->GetProcMeshSection(1))
			{
				Entry->Water = *Water;
				Entry->bHasWater = Water->ProcIndexBuffer.Num() > 0;
			}

			PatchCache.Insert(Key, Entry);
			Stats.CacheEntries = PatchCache.Num();
			Stats.CacheMegabytes = static_cast<double>(PatchCache.Bytes()) / (1024.0 * 1024.0);
			Stats.CacheEvictions = PatchCache.Evictions();
		}

		UMeshComponent* Pooled = MeshPool[SectionIndex];
		LedgerTerrain::ClearPatch(*Pooled);
		Pooled->SetVisibility(false);
		Pooled->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	FreeSections.Add(SectionIndex);
}

void ALedgerPlanet::AbandonJob(uint64 Key)
{
	if (const FLedgerPatchJobRef* Job = InFlight.Find(Key))
	{
		if (Job->IsValid())
		{
			// The worker may still be mid-generation; it reads this only after
			// finishing, so the result is discarded rather than interrupted.
			(*Job)->bAbandoned.store(true, std::memory_order_relaxed);
		}
	}
}


void ALedgerPlanet::SetScatterMeshes(const TArray<UStaticMesh*>& Meshes)
{
	for (UHierarchicalInstancedStaticMeshComponent* Component : ScatterComponents)
	{
		if (Component != nullptr)
		{
			Component->DestroyComponent();
		}
	}
	ScatterComponents.Reset();
	ScatterVariants = 0;

	for (int32 Variant = 0; Variant < Meshes.Num(); ++Variant)
	{
		if (Meshes[Variant] == nullptr)
		{
			continue;
		}
		for (int32 Bucket = 0; Bucket < ScatterBuckets; ++Bucket)
		{
			UHierarchicalInstancedStaticMeshComponent* Component =
				NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			Component->SetStaticMesh(Meshes[Variant]);
			Component->SetupAttachment(GetRootComponent());
			// No collision on the instances. Tens of thousands of them with
			// collision is that many more shapes for the physics scene to
			// sweep, and nothing in the game yet can touch a tree. It goes on
			// when something can.
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCastShadow(true);
			Component->RegisterComponent();
			ScatterComponents.Add(Component);
		}
		++ScatterVariants;
	}

	ScatterBucketDirty.Init(true, ScatterBuckets);
	UE_LOG(LogLedger, Log, TEXT("scatter: %d variants over %d buckets, %d components"),
		ScatterVariants, ScatterBuckets, ScatterComponents.Num());
}

void ALedgerPlanet::RebuildScatter()
{
	if (ScatterVariants == 0)
	{
		return;
	}
	if (ScatterBucketDirty.Num() != ScatterBuckets)
	{
		ScatterBucketDirty.Init(true, ScatterBuckets);
	}

	if (!Budget.Allows(ELedgerStreamClass::Speculative))
	{
		// Trees are the first thing to give up. Somebody standing on ground
		// that has not arrived notices; somebody standing on ground whose
		// trees arrive a frame late does not.
		Budget.Refused(ELedgerStreamClass::Speculative);
		return;
	}

	const double Started = FPlatformTime::Seconds();
	const FVector PlanetOrigin = GetActorLocation();

	// At most two buckets a frame. Patches arrive faster than that while
	// streaming, so the queue can be behind -- by up to eight frames, an eighth
	// of a second, during which a newly arrived patch has ground and no trees.
	// That is the trade: a visible fill-in against a millisecond of every
	// frame, and the fill-in is at the edge of the collision radius where
	// nothing is close enough to notice.
	constexpr int32 BucketsPerFrame = 2;
	int32 Rebuilt = 0;

	TArray<TArray<FTransform>> ByVariant;
	for (int32 Step = 0; Step < ScatterBuckets && Rebuilt < BucketsPerFrame; ++Step)
	{
		const int32 Bucket = (ScatterBucketCursor + Step) % ScatterBuckets;
		if (!ScatterBucketDirty[Bucket])
		{
			continue;
		}
		ScatterBucketDirty[Bucket] = false;
		++Rebuilt;
		ScatterBucketCursor = (Bucket + 1) % ScatterBuckets;

		ByVariant.Reset();
		ByVariant.SetNum(ScatterVariants);

		for (const TPair<uint64, FPatchScatter>& Patch : LiveScatter)
		{
			if (ScatterBucketOf(Patch.Key) != Bucket)
			{
				continue;
			}

			// Instance positions are relative to their patch centre, which is
			// what keeps them in float precision on a planet 6.37e8 cm across.
			// The component wants world space, so the centre goes back on here.
			const FVector Base = PlanetOrigin + FVector(Patch.Value.Centre);
			for (const FLedgerScatterInstance& Instance : Patch.Value.Instances)
			{
				if (!ByVariant.IsValidIndex(Instance.Variant))
				{
					continue;
				}
				ByVariant[Instance.Variant].Add(FTransform(
					FQuat(Instance.Rotation),
					Base + FVector(Instance.Position),
					FVector(Instance.Scale)));
			}
		}

		for (int32 Variant = 0; Variant < ScatterVariants; ++Variant)
		{
			UHierarchicalInstancedStaticMeshComponent* Component =
				ScatterComponents[Variant * ScatterBuckets + Bucket];
			if (Component == nullptr)
			{
				continue;
			}
			Component->ClearInstances();
			if (ByVariant[Variant].Num() > 0)
			{
				Component->AddInstances(ByVariant[Variant],
					/*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
			}
		}
	}

	if (Rebuilt == 0)
	{
		return;
	}

	int32 Total = 0;
	for (const TPair<uint64, FPatchScatter>& Patch : LiveScatter)
	{
		Total += Patch.Value.Instances.Num();
	}
	Stats.ScatterInstances = Total;
	Stats.LastScatterRebuildMs = (FPlatformTime::Seconds() - Started) * 1000.0;
	Budget.Spent(ELedgerStreamClass::Speculative, Stats.LastScatterRebuildMs);
	Stats.SpentSpeculativeMs = Budget.SpentMs(ELedgerStreamClass::Speculative);

	// Logged on every rebuild, because the rebuild cost is the whole of T057's
	// frame-budget question and a number nobody can see is a number nobody
	// checked.
	UE_LOG(LogLedger, Verbose,
		TEXT("scatter: %d instances over %d patches, %d buckets rebuilt in %.2f ms"),
		Total, LiveScatter.Num(), Rebuilt, Stats.LastScatterRebuildMs);
}

double ALedgerPlanet::SeasonPhase() const
{
	// Read once. FParse on every patch would be a string scan per patch, and
	// the season cannot change during a run by design.
	static const double Season = []()
	{
		float Parsed = 0.0f;
		FParse::Value(FCommandLine::Get(), TEXT("season="), Parsed);
		return static_cast<double>(FMath::Frac(Parsed));
	}();
	return Season;
}
