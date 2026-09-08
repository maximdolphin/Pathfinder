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

		UProceduralMeshComponent* Mesh = MeshPool[Job->SectionIndex];
		Mesh->SetWorldLocation(GetActorLocation() + FVector(Job->Centre));
		Mesh->ClearMeshSection(0);

		const double CollisionStart = FPlatformTime::Seconds();
		Mesh->CreateMeshSection(
			0, Job->Vertices, Job->Triangles, Job->Normals, Job->UVs,
			Job->Colors, Job->Tangents, Job->bWithCollision);
		if (Job->bWithCollision)
		{
			Stats.WorstFrameCollisionMs = FMath::Max(
				Stats.WorstFrameCollisionMs, (FPlatformTime::Seconds() - CollisionStart) * 1000.0);
		}

		Mesh->SetCollisionEnabled(Job->bWithCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
		if (SurfaceMaterial != nullptr)
		{
			Mesh->SetMaterial(0, SurfaceMaterial);
		}

		// Section 1 is the sea. Same component, so it moves and culls with the
		// land it belongs to and costs no extra transform.
		Mesh->ClearMeshSection(1);
		if (Job->bHasWater)
		{
			++Stats.WaterSections;
			Mesh->CreateMeshSection(
				1, Job->WaterVertices, Job->WaterTriangles, Job->WaterNormals,
				Job->WaterUVs, Job->WaterColors, Job->WaterTangents, /*bCreateCollision*/ false);
			if (WaterMaterial != nullptr)
			{
				Mesh->SetMaterial(1, WaterMaterial);
			}
		}

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

bool ALedgerPlanet::UploadFromCache(const FLedgerQuadNode& Node, bool bWithCollision)
{
	if (FreeSections.Num() == 0)
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
	UProceduralMeshComponent* Mesh = MeshPool[SectionIndex];
	Mesh->SetWorldLocation(GetActorLocation() + FVector(Entry.Centre));

	Entry.Land.bEnableCollision = bWithCollision;
	Mesh->SetProcMeshSection(0, Entry.Land);
	if (SurfaceMaterial != nullptr)
	{
		Mesh->SetMaterial(0, SurfaceMaterial);
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

void ALedgerPlanet::ReleaseSection(uint64 Key)
{
	int32 SectionIndex = INDEX_NONE;
	if (!ActiveSections.RemoveAndCopyValue(Key, SectionIndex))
	{
		return;
	}
	if (MeshPool.IsValidIndex(SectionIndex))
	{
		UProceduralMeshComponent* Mesh = MeshPool[SectionIndex];

		// Take the geometry on the way out. This is the only moment it can be
		// taken: after ClearMeshSection it is gone, and before release it is
		// still on screen and not worth a second copy.
		if (const FProcMeshSection* Land = Mesh->GetProcMeshSection(0))
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

		Mesh->ClearMeshSection(0);
		Mesh->ClearMeshSection(1);
		Mesh->SetVisibility(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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

