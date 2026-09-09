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
#include "Engine/World.h"
#include "LedgerLog.h"
#include "LedgerPatchGenerator.h"
#include "Materials/MaterialInterface.h"
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

	// Neighbour depths are read here, on the game thread, while the tree is
	// stable. The worker never touches the tree.
	Job->bStitchLeft = LeafDepthAt(UnitSphereAt(Node, -0.02, 0.5)) < Node.Depth;
	Job->bStitchRight = LeafDepthAt(UnitSphereAt(Node, 1.02, 0.5)) < Node.Depth;
	Job->bStitchBottom = LeafDepthAt(UnitSphereAt(Node, 0.5, -0.02)) < Node.Depth;
	Job->bStitchTop = LeafDepthAt(UnitSphereAt(Node, 0.5, 1.02)) < Node.Depth;

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
		LedgerTerrain::UploadPatch(*Mesh, *Job, TerrainMaterial(), WaterMaterial);
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
		SectionMeta[Job->SectionIndex] = FLedgerSectionMeta{
			Job->Key, Job->Centre,
			Job->bStitchLeft, Job->bStitchRight, Job->bStitchBottom, Job->bStitchTop };

		Stats.LastPatchGenerationMs = Job->GenerationMs;
		++Stats.TotalBuilds;

		if ((FPlatformTime::Seconds() - Started) * 1000.0 >= UploadBudgetMs)
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
	if (Entry.bStitchLeft != (LeafDepthAt(UnitSphereAt(Node, -0.02, 0.5)) < Node.Depth)
		|| Entry.bStitchRight != (LeafDepthAt(UnitSphereAt(Node, 1.02, 0.5)) < Node.Depth)
		|| Entry.bStitchBottom != (LeafDepthAt(UnitSphereAt(Node, 0.5, -0.02)) < Node.Depth)
		|| Entry.bStitchTop != (LeafDepthAt(UnitSphereAt(Node, 0.5, 1.02)) < Node.Depth))
	{
		PatchCache.Remove(Key);
		Stats.CacheEntries = PatchCache.Num();
		return false;
	}

	const int32 SectionIndex = FreeSections.Pop();
	UProceduralMeshComponent* Mesh = PooledProcedural(SectionIndex);
	Mesh->SetWorldLocation(GetActorLocation() + FVector(Entry.Centre));

	Entry.Land.bEnableCollision = bWithCollision;
	Mesh->SetProcMeshSection(0, Entry.Land);
	if (SurfaceMaterial != nullptr)
	{
		Mesh->SetMaterial(0, TerrainMaterial());
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
		Entry.bStitchLeft, Entry.bStitchRight, Entry.bStitchBottom, Entry.bStitchTop };

	// Removed, not kept: the geometry is on screen again and holding a second
	// copy of it is exactly the waste this cache is shaped to avoid.
	PatchCache.Remove(Key);

	Stats.CacheEntries = PatchCache.Num();
	Stats.CacheMegabytes = static_cast<double>(PatchCache.Bytes()) / (1024.0 * 1024.0);
	++Stats.CacheHits;
	return true;
}

UMaterialInterface* ALedgerPlanet::TerrainMaterial() const
{
	return SurfaceInstance != nullptr
		? Cast<UMaterialInterface>(SurfaceInstance)
		: SurfaceMaterial.Get();
}

void ALedgerPlanet::UpdateMorphParameters(double ViewportWidth, double FovRadians)
{
	if (SurfaceInstance == nullptr)
	{
		return;
	}

	// Same projection the screen-space error metric uses, rearranged.
	//
	// Error in pixels is NodeWorldSize * Scale / Distance, so a node is at its
	// threshold when Distance equals NodeWorldSize * Scale. Handing the shader
	// that one number lets it work out how close any patch is to collapsing
	// from the patch's own size, with nothing per-patch to set.
	const double HalfFov = FMath::Max(FovRadians * 0.5, 0.001);
	const double Scale = (ViewportWidth * 0.5) / (FMath::Tan(HalfFov) * FMath::Max(ErrorThresholdPixels, 1.0));

	SurfaceInstance->SetScalarParameterValue(TEXT("MorphScale"), static_cast<float>(Scale));
	SurfaceInstance->SetVectorParameterValue(TEXT("PlanetCentre"), FLinearColor(
		static_cast<float>(GetActorLocation().X),
		static_cast<float>(GetActorLocation().Y),
		static_cast<float>(GetActorLocation().Z),
		0.0f));
}

void ALedgerPlanet::ReleaseSection(uint64 Key)
{
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

