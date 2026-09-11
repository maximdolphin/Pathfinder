// What the terrain is doing, in words. Design SS12, SS15.1.
//
// Separated from the planet because a diagnostic is not a mechanism: this
// file can grow every counter anybody ever wants without the quadtree
// getting longer, which is the only reason it stayed short.

#include "LedgerLog.h"
#include "LedgerPlanet.h"

#include "HAL/PlatformMemory.h"

void ALedgerPlanet::LogStats() const
{
	UE_LOG(LogLedger, Log, TEXT("=== TERRAIN (design §6.8 / §15.1 spike) ==="));
	UE_LOG(LogLedger, Log, TEXT("  visible nodes      %d  (deepest depth %d)"),
		Stats.VisibleNodes, Stats.DeepestVisibleDepth);
	UE_LOG(LogLedger, Log, TEXT("  with collision     %d"), Stats.NodesWithCollision);
	UE_LOG(LogLedger, Log, TEXT("  jobs in flight     %d"), Stats.JobsInFlight);
	UE_LOG(LogLedger, Log, TEXT("  starved this frame %d"), Stats.PendingBuilds);
	UE_LOG(LogLedger, Log, TEXT("  sections           %d active, %d in flight, %d free of %d"),
		Stats.SectionsActive, Stats.SectionsPending, Stats.SectionsFree, MeshPool.Num());
	UE_LOG(LogLedger, Log, TEXT("  holes              %d now, %d worst at t=%.0fs"),
		Stats.UnfilledNodes, Stats.WorstUnfilled, Stats.WorstUnfilledAt);
	UE_LOG(LogLedger, Log, TEXT("  patches total      %lld"), Stats.TotalBuilds);
	UE_LOG(LogLedger, Log, TEXT("  upload ms (game)   last %.3f  worst %.3f"),
		Stats.LastFrameUploadMs, Stats.WorstFrameUploadMs);
	UE_LOG(LogLedger, Log, TEXT("  generate ms (task) last %.3f"), Stats.LastPatchGenerationMs);
	UE_LOG(LogLedger, Log, TEXT("  cook ms            worst %.3f"), Stats.WorstFrameCollisionMs);
	UE_LOG(LogLedger, Log,
		TEXT("  tick ms (worst)    tree %.2f  harvest %.2f  collect %.2f  "
		     "sort %.2f  imbalance %.2f  whole tick %.2f"),
		Stats.WorstTreeMs, Stats.WorstHarvestMs, Stats.WorstCollectMs,
		Stats.WorstSortMs, Stats.WorstImbalanceMs, Stats.WorstTickMs);
	UE_LOG(LogLedger, Log, TEXT("  imbalanced edges   %d  (worst depth gap %d)"),
		Stats.ImbalancedEdges, Stats.WorstDepthDifference);
	UE_LOG(LogLedger, Log, TEXT("  water sections     %d uploaded"), Stats.WaterSections);
	UE_LOG(LogLedger, Log, TEXT("  patch cache        %lld hit / %lld generated (%.0f%% reused)"),
		Stats.CacheHits, Stats.CacheMisses,
		(Stats.CacheHits + Stats.CacheMisses) > 0
			? 100.0 * static_cast<double>(Stats.CacheHits) / static_cast<double>(Stats.CacheHits + Stats.CacheMisses)
			: 0.0);
	UE_LOG(LogLedger, Log, TEXT("                     %d entries, %.1f MB, %lld evicted"),
		Stats.CacheEntries, Stats.CacheMegabytes, Stats.CacheEvictions);
	UE_LOG(LogLedger, Log, TEXT("  camera speed       %.0f m/s"), CameraVelocityLocal.Length() / 100.0);

	// **What the process is holding, next to the counters that explain it.**
	// A mesh pool of a few thousand sections is a few gigabytes of vertex
	// buffers, and the first sign that something is not being handed back is
	// this number climbing while `sections` above does not. Printing it here
	// rather than in its own place is deliberate: the cause and the size want
	// to be readable in one glance.
	//
	// `Available` is the commit headroom, not free RAM. Windows fails an
	// allocation with "the paging file is too small" when the *commit* limit is
	// reached, which can happen with gigabytes of physical memory still free,
	// so that is the number worth watching.
	const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
	constexpr double ToMB = 1.0 / (1024.0 * 1024.0);
	UE_LOG(LogLedger, Log,
		TEXT("  memory             %.0f MB used, %.0f MB peak, %.0f MB virtual, "
		     "%.0f MB available"),
		Memory.UsedPhysical * ToMB, Memory.PeakUsedPhysical * ToMB,
		Memory.UsedVirtual * ToMB, Memory.AvailablePhysical * ToMB);

	// **Say it before it is fatal.** Running out of commit does not arrive as a
	// diagnosis: it arrives as an access violation in whichever allocation
	// happened to be next, which was `SetProcMeshSection` the one time it was
	// seen. A full pool of patches costs about eight gigabytes on top of the
	// baseline -- mesh, cooked collision and a ray-tracing acceleration
	// structure each -- so two of these processes at once is enough on a
	// machine whose page file is not large. One line, ten seconds before the
	// bang, is the difference between a mystery and a known cost.
	constexpr uint64 LowWaterBytes = 2ull * 1024 * 1024 * 1024;
	if (Memory.AvailablePhysical < LowWaterBytes)
	{
		UE_LOG(LogLedger, Warning,
			TEXT("  memory             only %.0f MB left to allocate; an "
			     "allocation failure now would surface as a crash somewhere "
			     "unrelated"),
			Memory.AvailablePhysical * ToMB);
	}
}
