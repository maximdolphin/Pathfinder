// What the terrain is doing, in words. Design SS12, SS15.1.
//
// Separated from the planet because a diagnostic is not a mechanism: this
// file can grow every counter anybody ever wants without the quadtree
// getting longer, which is the only reason it stayed short.

#include "LedgerLog.h"
#include "LedgerPlanet.h"

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
	UE_LOG(LogLedger, Log, TEXT("  water sections     %d uploaded"), Stats.WaterSections);
	UE_LOG(LogLedger, Log, TEXT("  patch cache        %lld hit / %lld generated (%.0f%% reused)"),
		Stats.CacheHits, Stats.CacheMisses,
		(Stats.CacheHits + Stats.CacheMisses) > 0
			? 100.0 * static_cast<double>(Stats.CacheHits) / static_cast<double>(Stats.CacheHits + Stats.CacheMisses)
			: 0.0);
	UE_LOG(LogLedger, Log, TEXT("                     %d entries, %.1f MB, %lld evicted"),
		Stats.CacheEntries, Stats.CacheMegabytes, Stats.CacheEvictions);
	UE_LOG(LogLedger, Log, TEXT("  camera speed       %.0f m/s"), CameraVelocityLocal.Length() / 100.0);
}
