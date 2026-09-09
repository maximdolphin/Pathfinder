// Cube-sphere quadtree terrain. Design Â§6.8.
//
// Six root faces, recursively subdivided against a screen-space error metric
// relative to camera altitude. Crack prevention by edge-index stitching, not
// skirts. Collision cooked only near the player, async, off the game thread.
//
// **Geometry is generated on worker threads.** ADR-0002 measured a 33x33 patch
// at 5â€“8 ms of game-thread time and concluded that was the ceiling on the whole
// implementation â€” the budget could buy smooth frames or a filled horizon, not
// both. Moving generation to the task graph removes the choice: the game thread
// now only uploads finished vertex buffers, and the machine's other cores do the
// sampling. That is also what makes a real-scale planet and a seven-octave
// ridged multifractal affordable at all.
//
// Nanite is deliberately absent: its clusters are built offline, so a
// runtime-generated streamed quadtree cannot use it (design Â§6.8, amended v1.1).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LedgerPatchCache.h"
#include "LedgerBiome.h"
#include "LedgerQuadNode.h"
#include "LedgerScatter.h"
#include "LedgerStreamingBudget.h"
#include "LedgerTerrainDelta.h"
#include "LedgerTerrainSample.h"
#include "LedgerTerrainMath.h"
#include "LedgerPatchComponents.h"
#include "ProceduralMeshComponent.h"
#include <atomic>
#include "LedgerPlanet.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UProceduralMeshComponent;

/// One patch of geometry being generated on a worker thread.
///
/// Every input is copied in at launch so the worker never touches the actor, the
/// quadtree, or anything else the game thread may be mutating. That is the whole
/// thread-safety argument, and it is why the inputs are values rather than
/// pointers.
/// Returns the material for a patch painted with these biomes, or null.
DECLARE_DELEGATE_RetVal_TwoParams(UMaterialInstanceDynamic*, FLedgerPaletteMaterial,
	const FLedgerBiomePalette&, const TArray<FLedgerBiome>&);

struct FLedgerPatchJob
{
	// ---- inputs, immutable once launched --------------------------------
	uint64 Key = 0;
	int32 SectionIndex = INDEX_NONE;
	bool bWithCollision = false;

	ELedgerCubeFace Face = ELedgerCubeFace::PositiveX;
	double U = 0.0;
	double V = 0.0;
	double Extent = 1.0;
	FVector3d Centre = FVector3d::ZeroVector;

	FLedgerTerrainParams Params;
	int32 Side = 33;

	/// Where in the year this patch was generated, 0 to 1.
	///
	/// A constant for a run rather than a clock: the snow a patch carries is
	/// baked into its vertices, so a season that moved would invalidate every
	/// cached patch continuously. Set from -season= and left alone. A moving
	/// year is M04's, with the weather.
	double SeasonPhase = 0.0;

	/// The biome set, shared with every job in flight.
	///
	/// A shared pointer to a const array rather than a copy per job: it is read
	/// on a worker thread while the game thread may be launching more jobs, and
	/// the set itself never changes after load. Null means no biomes were
	/// loaded, in which case the generator falls back to the height ramp.
	TSharedPtr<const TArray<FLedgerBiome>> Biomes;

	/// Ground somebody has changed. Loaded at BeginPlay, replaced whole on
	/// every edit, and handed to every patch job through TerrainParams.
	TSharedPtr<const FLedgerTerrainDelta> Delta;

	/// Approximate world-space extent of the node, centimetres. Baked into the
	/// vertices so the shader can work out how close this patch is to being
	/// collapsed without being told per frame.
	double WorldSize = 0.0;

	/// Edges that border a coarser neighbour and therefore need stitching.
	bool bStitchLeft = false;
	bool bStitchRight = false;
	bool bStitchBottom = false;
	bool bStitchTop = false;

	// ---- outputs --------------------------------------------------------
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	/// Geomorph data, one per vertex: X is how far this vertex must move along
	/// its radial to sit where the *parent* LOD would have put it, and Y is the
	/// node's world size.
	///
	/// Only a scalar is needed rather than a full offset, because the coarse
	/// position is taken as the same direction at the interpolated elevation.
	/// The true midpoint of two points on a sphere is slightly inside it, and
	/// that chord-versus-arc error at patch scale is a fraction of a millimetre
	/// — far below the elevation difference this is correcting.
	TArray<FVector2D> MorphUVs;

	/// Elevation at each vertex, centimetres. Kept on the job rather than local
	/// to the generator because it is the expensive part -- 4,225 noise samples
	/// -- and the disk cache stores it instead of the finished geometry
	/// (LedgerPatchDisk.h).
	TArray<double> Elevations;

	/// Vertex colour. RGB are the weights of the patch's three palette slots,
	/// summing to one; A is unused. **Not a colour** any more -- the surface
	/// sets carry the colour, and painting a tint on top of an authored scan is
	/// what made the old ramp read as a contour map (T053).
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	/// Which three biomes this patch's vertex colours are weights of. Chosen
	/// from the patch's own totals, so the biome the palette leaves out is by
	/// construction the least present one on it.
	FLedgerBiomePalette Palette;

	/// The sea surface for this patch, as a second mesh section.
	///
	/// Water rides the terrain quadtree rather than having a quadtree of its
	/// own: it inherits the LOD, it is generated by the same job on the same
	/// thread, and its resolution can never disagree with the coastline it meets.
	/// A separate water tree would be a second streaming system to keep in step
	/// with the first, and the seam between them would be exactly at the
	/// shoreline, where it is most visible.
	TArray<FVector> WaterVertices;
	TArray<int32> WaterTriangles;
	TArray<FVector> WaterNormals;
	TArray<FVector2D> WaterUVs;
	TArray<FColor> WaterColors;
	TArray<FProcMeshTangent> WaterTangents;
	bool bHasWater = false;

	/// The cave walls under this patch, as a third mesh section.
	///
	/// Generated by the same job on the same thread as the surface, for the
	/// same reason the water is: it is derived from the same field, at the same
	/// resolution, and a separate system would be a second thing to keep in
	/// step. Empty for the great majority of patches -- see
	/// LedgerCaves::ShouldMeshCaves.
	TArray<FVector> CaveVertices;
	TArray<int32> CaveTriangles;
	TArray<FVector> CaveNormals;
	TArray<FVector2D> CaveUVs;
	bool bHasCaves = false;

	/// What is scattered on this patch, generated on the worker with it.
	TArray<FLedgerScatterInstance> Scatter;

	double GenerationMs = 0.0;

	/// Written last by the worker, read by the game thread. Release/acquire, so
	/// the buffers above are guaranteed visible once this reads true.
	std::atomic<bool> bComplete{false};

	/// Set by the game thread when the patch is no longer wanted â€” the LOD moved
	/// on while it was in flight. The result is dropped rather than uploaded.
	std::atomic<bool> bAbandoned{false};
};

using FLedgerPatchJobRef = TSharedPtr<FLedgerPatchJob, ESPMode::ThreadSafe>;

/// What a live section was built from, so that releasing it can be undone.
///
/// The stitch flags cannot be recovered from the mesh and cannot be recomputed
/// at release either â€” a collapse destroys the node before its section goes
/// back to the pool. They are recorded on the way in.
struct FLedgerSectionMeta
{
	uint64 Key = 0;
	FVector3d Centre = FVector3d::ZeroVector;
	bool bStitchLeft = false;
	bool bStitchRight = false;
	bool bStitchBottom = false;
	bool bStitchTop = false;

	/// Carried so the geometry can be put back in the cache with the palette
	/// its vertex colours were written against.
	FLedgerBiomePalette Palette;
};

/// What the terrain is doing, for the Â§15.1 build/buy decision.
USTRUCT()
struct FLedgerTerrainStats
{
	GENERATED_BODY()

	UPROPERTY()
	int32 VisibleNodes = 0;

	UPROPERTY()
	int32 NodesWithCollision = 0;

	UPROPERTY()
	int32 JobsInFlight = 0;

	UPROPERTY()
	int32 PendingBuilds = 0;

	UPROPERTY()
	int64 TotalBuilds = 0;

	UPROPERTY()
	int32 DeepestVisibleDepth = 0;

	/// Game-thread milliseconds spent uploading finished patches last frame.
	///
	/// This used to say upload was "the only terrain cost on the game thread".
	/// It was not, and nothing had checked: the LOD walk cost three times as
	/// much. See WorstTreeMs below and docs/comparisons/terrain-component.md.
	UPROPERTY()
	double LastFrameUploadMs = 0.0;

	UPROPERTY()
	double WorstFrameUploadMs = 0.0;

	/// Worker-thread milliseconds for a single patch. Off the critical path, so
	/// this can be large without costing a frame â€” it only bounds throughput.
	UPROPERTY()
	double LastPatchGenerationMs = 0.0;

	UPROPERTY()
	double WorstFrameCollisionMs = 0.0;

	/// Visible leaves with no geometry and no ancestor covering for them. This
	/// is the count of actual holes in the planet, as distinct from leaves
	/// merely waiting on a finer LOD â€” a split parent keeps its geometry until
	/// all four children have theirs, so waiting normally costs detail, not a
	/// hole. Anything but zero here is a hole somebody can fly through.
	UPROPERTY()
	int32 UnfilledNodes = 0;

	UPROPERTY()
	int32 WorstUnfilled = 0;

	/// When the worst happened. A peak with no timestamp cannot be attributed,
	/// and the difference between a climb and a teleport is the whole question.
	UPROPERTY()
	double WorstUnfilledAt = 0.0;

	/// Section pool accounting. A starved frame is either too much demand or a
	/// pool that is not being returned to, and the two have nothing in common.
	UPROPERTY()
	int32 SectionsActive = 0;

	UPROPERTY()
	int32 SectionsFree = 0;

	UPROPERTY()
	int32 SectionsPending = 0;

	/// Where the terrain tick's game-thread time goes, in milliseconds, worst
	/// frame so far.
	///
	/// LastFrameUploadMs above says upload "is now the *only* terrain cost on
	/// the game thread". Nobody measured that. The game thread has been over
	/// budget for the whole project -- 20 to 33 ms at low altitude against
	/// 16.7, with the GPU idle at 5 to 9 -- and upload worsts at about 5, so
	/// something else is spending the rest of it.
	UPROPERTY()
	double WorstTreeMs = 0.0;

	UPROPERTY()
	double WorstHarvestMs = 0.0;

	UPROPERTY()
	double WorstCollectMs = 0.0;

	UPROPERTY()
	double WorstSortMs = 0.0;

	UPROPERTY()
	double WorstImbalanceMs = 0.0;

	UPROPERTY()
	double WorstTickMs = 0.0;

	/// The whole terrain tick, this frame. Worsts say how bad it ever got;
	/// this is what the flight recorder samples so the cost can be reported
	/// against the phase of flight it happened in, which is the only form in
	/// which it answers anything -- the game thread is fine in orbit and 20 ms
	/// over on approach, and one number for the run averages those together.
	UPROPERTY()
	double LastTickMs = 0.0;

	/// Adjacent visible leaves whose depths differ by more than one.
	///
	/// Edge stitching collapses a finer node's odd edge vertices onto their even
	/// neighbours, which closes a one-level difference exactly. It cannot close
	/// two: the finer node would have to collapse two levels of vertices and it
	/// only knows how to collapse one. Any count here is a crack somewhere.
	UPROPERTY()
	int32 ImbalancedEdges = 0;

	UPROPERTY()
	int32 WorstDepthDifference = 0;

	UPROPERTY()
	int32 WaterSections = 0;

	/// Patches served from the cache against patches that had to be generated.
	/// The ratio of these two is the whole argument for the cache existing.
	UPROPERTY()
	int64 CacheHits = 0;

	UPROPERTY()
	int64 CacheMisses = 0;

	UPROPERTY()
	int64 CacheEvictions = 0;

	UPROPERTY()
	int32 CacheEntries = 0;

	UPROPERTY()
	double CacheMegabytes = 0.0;

	/// Instances currently in the scatter components, and what the last full
	/// rebuild of them cost on the game thread. The rebuild is the whole of
	/// T057's frame-budget question, so it is measured rather than assumed.
	UPROPERTY()
	int32 ScatterInstances = 0;

	UPROPERTY()
	double LastScatterRebuildMs = 0.0;

	/// Streaming time spent last frame, by what it was for, and how many
	/// pieces of work were refused for want of budget. Together these say
	/// whether a budget is holding by doing less or by doing nothing.
	UPROPERTY()
	double SpentCollisionMs = 0.0;

	UPROPERTY()
	double SpentHoleMs = 0.0;

	UPROPERTY()
	double SpentDetailMs = 0.0;

	UPROPERTY()
	double SpentSpeculativeMs = 0.0;

	UPROPERTY()
	int32 RefusedDetail = 0;

	UPROPERTY()
	int32 RefusedSpeculative = 0;

	/// The error threshold actually in force, which rises above the configured
	/// one when the visible set is larger than the pool can hold.
	UPROPERTY()
	double EffectiveErrorPixels = 0.0;
};

UCLASS()
class LEDGERTERRAIN_API ALedgerPlanet : public AActor
{
	GENERATED_BODY()

public:
	ALedgerPlanet();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/// Reference sphere radius in centimetres. Earth: 6,371 km.
	///
	/// LWC carries this without complaint â€” 6.37e8 cm is well inside a double's
	/// envelope (Â§6.8) â€” and the mesh does not care either, because vertices are
	/// built relative to each node's own centre and never leave float range.
	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	double Radius = 637100000.0;

	/// Peak elevation above the reference sphere. Earth's is about 9 km, which
	/// is 0.14% of its radius â€” invisible from orbit, and correctly so. What
	/// makes a planet read from space is the coastline, not the relief.
	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	double MaxElevation = 900000.0;

	/// Where the waterline sits in the continent noise, in [-1, 1]. Higher means
	/// less land; Earth is about 71% ocean.
	/// 0.14 puts roughly two thirds of the surface under water, which is close
	/// to Earth and, more to the point, gives coastlines something to be a coast
	/// *of*. At 0.06 the planet was one supercontinent with ponds.
	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	double SeaLevel = 0.14;

	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	int32 Seed = 20260908;

	/// Vertices along one edge of a node's grid. Must be odd so edge stitching
	/// has a midpoint to collapse.
	/// 65 rather than 33: a patch with twice the vertices per side covers four
	/// times the ground at the same screen-space error, so the same triangle
	/// count arrives in a quarter of the draw calls. At 33 the visible set hit
	/// eighteen thousand components and the pool starved into a checkerboard of
	/// holes.
	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	int32 GridResolution = 65;

	/// 18 levels of 64-quad patches over a 6,371 km planet: 0.60 m quads.
	///
	/// It was 15, which is 4.77 m, and that was the hard floor on how good the
	/// ground could ever look. Standing twenty metres from a quad at 1920 px
	/// and 90 degrees horizontal, one triangle edge was 229 pixels wide. No
	/// amount of material work fixes a surface made of quarter-screen facets.
	///
	/// Three levels rather than more because the near-field band that gives
	/// these triangles something to describe bottoms out around 1.9 m (T429),
	/// and triangles finer than the field they sample are triangles
	/// interpolating a plane more expensively.
	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	int32 MaxDepth = 18;

	/// Subdivide when a node's projected error exceeds this many pixels.
	/// Paired with the patch resolution above: together they set how many
	/// components the visible set needs. 150 px with 65-vertex patches lands
	/// near a thousand, which the pool can actually serve.
	UPROPERTY(EditAnywhere, Category = "Ledger|LOD")
	double ErrorThresholdPixels = 150.0;

	/// Depth down to which every visible node keeps geometry, always.
	///
	/// A shell so that arriving somewhere new finds coarse ground already
	/// there. Kept at 3 -- six by sixty-four nodes at most, of which only the
	/// ones facing the camera are built.
	///
	/// **It was added to fix a problem that turned out not to exist.** The
	/// overload run reported holes in a hundred per cent of frames; the shell
	/// moved that by under two per cent, and so did keeping the shell resident
	/// against the parent-hold rule. A photograph of the same run settled it:
	/// the ground is solid. `UnfilledNodes` counts nodes that `bVisible` says
	/// are visible, and `bVisible` is a horizon test rather than a frustum one,
	/// so it counts the ground behind the camera. See
	/// docs/comparisons/overload/. The shell stays because prefetching the
	/// coarse levels is right on its own terms and costs sixty-odd sections of
	/// three thousand six hundred; it is not a fix for anything.
	UPROPERTY(EditAnywhere, Category = "Ledger|LOD")
	int32 ResidentDepth = 3;

	/// Patches allowed in flight at once. Bounded so a fast turn cannot queue
	/// thousands of jobs whose results are stale before they land.
	UPROPERTY(EditAnywhere, Category = "Ledger|LOD")
	int32 MaxJobsInFlight = 128;

	/// Milliseconds per frame the game thread may spend on streaming: cache
	/// uploads, finished patches and the scatter rebuild, together.
	///
	/// **Together is the point.** These were three separate budgets, each
	/// reasonable alone, and a frame that did all three did the sum of them.
	/// Collision work is exempt and is reported separately, because ground the
	/// player is standing on is not a thing to be economical about.
	UPROPERTY(EditAnywhere, Category = "Ledger|LOD")
	double UploadBudgetMs = 6.0;

	/// Cook collision within this distance of the camera.
	UPROPERTY(EditAnywhere, Category = "Ledger|Collision")
	double CollisionRadius = 300000.0;

	/// Cook ahead along the velocity vector by this many seconds of travel.
	/// Predictive, never on demand â€” Â§6.8's one non-negotiable.
	UPROPERTY(EditAnywhere, Category = "Ledger|Collision")
	double CollisionLeadSeconds = 2.5;

	/// Subdivide ahead along the velocity vector by this many seconds.
	///
	/// Longer than the collision lead, because geometry is the slower of the
	/// two: a patch has to be queued, sampled on a worker and uploaded, where a
	/// cook only has to happen. Arriving somewhere that was decided a moment
	/// ago means arriving before the decision has finished being acted on.
	UPROPERTY(EditAnywhere, Category = "Ledger|LOD")
	double GeometryLeadSeconds = 3.0;

	/// Memory the patch cache may hold, in megabytes.
	///
	/// Bounded by bytes rather than by entries because entries are not the same
	/// size: a patch over open water carries a second mesh section and a patch
	/// inland does not. A count would be a budget for the average patch and a
	/// surprise for every other one.
	///
	/// A 65-vertex patch is around 700 KB of interleaved vertices, so this holds
	/// roughly seven hundred of them â€” a few minutes of flying, which is the
	/// span over which a player actually retraces ground.
	UPROPERTY(EditAnywhere, Category = "Ledger|LOD")
	double PatchCacheBudgetMB = 512.0;

	const FLedgerTerrainStats& GetStats() const { return Stats; }

	/// Height of the surface above the reference sphere at a direction.
	double SurfaceRadiusAt(const FVector3d& UnitDirection) const;

	/// What the ground is at a world point. T061.
	///
	/// The one place gameplay asks. It answers from the patch that is drawn --
	/// the same vertices, the same triangles the collision was cooked from --
	/// so it agrees with a physics trace rather than with the height function
	/// the trace does not know about. False where nothing is loaded.
	// No LEDGERTERRAIN_API: the class already carries it, and a member of a
	// dll-interface class may not repeat it.
	bool SampleTerrain(const FVector3d& WorldPoint, FLedgerTerrainSample& Out) const;

	/// Brings the ground inside a radius to one altitude, blending out over the
	/// falloff, and saves it. T062.
	///
	/// The delta is published as a new immutable one and the old patches are
	/// dropped, so the change is visible on the next stream rather than on the
	/// next reload. Everything already built out of the terrain -- collision,
	/// the sampling API, scatter -- follows for free, because they all read the
	/// same height function.
	void LevelPad(const FVector3d& Direction, double RadiusMetres,
		double FalloffMetres, double TargetAltitudeMetres);

	/// Every edit made to this planet's ground. Never null.
	TSharedPtr<const FLedgerTerrainDelta> TerrainDelta() const { return Delta; }

	/// Throws away every patch, live and cached, so the terrain is rebuilt from
	/// the current height function.
	void InvalidateTerrain();

	/// Where in the year this planet is, 0 to 1. Set from -season=.
	double SeasonPhase() const;

	FLedgerTerrainParams TerrainParams() const;

	/// Set before BeginPlay by whoever spawns the planet. The quadtree's
	/// business is geometry; what it is painted with is not its decision.
	void SetMaterials(UMaterialInterface* Surface, UMaterialInterface* Water)
	{
		SurfaceMaterial = Surface;
		WaterMaterial = Water;
	}

	/// How a palette becomes a material. Set beside SetMaterials, and for the
	/// same reason: binding a biome's surface set needs the manifest and the
	/// texture assets, which is the material module's business, and reaching
	/// for it from here is what would make these two modules a pair.
	///
	/// Left unbound, every patch gets the one surface instance and the ground
	/// is whatever the material's default slots are.
	void SetPaletteMaterialProvider(FLedgerPaletteMaterial Provider)
	{
		PaletteMaterial = MoveTemp(Provider);
	}

	/// The biome set the patches are weighed against. Shared with every job in
	/// flight, so it is fixed at BeginPlay and never mutated after.
	void SetBiomes(TSharedPtr<const TArray<FLedgerBiome>> InBiomes)
	{
		Biomes = MoveTemp(InBiomes);
	}

	/// The meshes a scatter variant can be. Set beside the materials, for the
	/// same reason: the quadtree decides where things go and not what they are.
	/// An empty array means no scatter at all.
	void SetScatterMeshes(const TArray<UStaticMesh*>& Meshes);

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	/// The pool, in whichever component type this run is measuring. Held as
	/// the base type so the two candidates share every line of the streaming
	/// code except the upload itself; see LedgerPatchComponents.h.
	UPROPERTY()
	TArray<TObjectPtr<UMeshComponent>> MeshPool;

	ELedgerPatchComponent ComponentKind = ELedgerPatchComponent::Procedural;

	/// The pooled component as a procedural one, or null when this run is
	/// measuring the other backend. The patch cache stores FProcMeshSection
	/// and therefore only exists for the procedural path -- which is one of
	/// the differences being measured, not an oversight.
	UProceduralMeshComponent* PooledProcedural(int32 SectionIndex) const;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SurfaceMaterial;

	/// The surface material with the geomorph parameters bound.
	///
	/// One instance for the whole planet rather than one per patch: the two
	/// things the blend needs that change — where the camera is and how wide
	/// the viewport is — are the same for every patch in a frame, and the one
	/// thing that differs per patch is baked into its vertices.
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SurfaceInstance;

	/// One instance per distinct palette, not per patch.
	///
	/// Eight biomes give at most a few dozen palettes a planet actually uses,
	/// and patches sharing a palette share a material -- which is the point,
	/// because binding nine textures per patch at 762 uploads a second would
	/// cost more than the blend it is setting up.
	UPROPERTY()
	TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> PaletteInstances;

	FLedgerPaletteMaterial PaletteMaterial;

	TSharedPtr<const TArray<FLedgerBiome>> Biomes;

	/// Ground somebody has changed. Loaded at BeginPlay, replaced whole on
	/// every edit, and handed to every patch job through TerrainParams.
	TSharedPtr<const FLedgerTerrainDelta> Delta;

	/// How many components each scatter variant is spread across.
	///
	/// **The whole of T057's frame budget is in this number.** One component
	/// per variant is two draw calls and a rebuild that costs 13 ms at a forest
	/// site, because every arriving patch invalidates all 78,000 instances. One
	/// component per patch is a rebuild that costs nothing and four hundred
	/// draw calls. Sixteen buckets is 32 draws and a rebuild of a sixteenth,
	/// and at most two buckets are rebuilt in a frame -- so a change shows up
	/// within about a quarter of a second and never costs more than a
	/// millisecond of it.
	static constexpr int32 ScatterBuckets = 16;

	/// Instanced components, indexed variant-major: Variant * ScatterBuckets +
	/// Bucket. Hierarchical, so a stand of trees behind a rise is culled as a
	/// group rather than one at a time.
	UPROPERTY()
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ScatterComponents;

	/// Which buckets have a patch that arrived or left since they were built,
	/// and where the round robin got to.
	TBitArray<> ScatterBucketDirty;
	int32 ScatterBucketCursor = 0;
	int32 ScatterVariants = 0;

	/// What each live patch scattered, kept so the components can be rebuilt.
	///
	/// **Rebuilt whole, not edited.** An instanced component has no stable
	/// handle for an instance -- removing one renumbers the rest -- so tracking
	/// which indices belong to which patch across a stream of arrivals and
	/// departures is a bookkeeping problem with a known bad ending. Clearing
	/// and re-adding is one batch call, it happens only when the near set
	/// actually changes, and the cost is measured in the terrain stats rather
	/// than assumed.
	/// A patch's scatter, with the centre its positions are relative to.
	struct FPatchScatter
	{
		FVector3d Centre = FVector3d::ZeroVector;
		TArray<FLedgerScatterInstance> Instances;
	};
	/// The frame's streaming allowance, and where it went.
	FLedgerStreamingBudget Budget;

	/// The error threshold in force this frame.
	///
	/// **A pool that cannot hold the visible set makes holes whatever the
	/// streamer does**, and the overload run found a visible set of 8,644
	/// against a pool of 3,600. The honest response is not to stream harder but
	/// to ask for less: when the set outgrows the pool the threshold rises, the
	/// tree splits less, and the ground is coarser instead of absent. It falls
	/// back to the configured value as soon as there is room, so nothing is
	/// permanently degraded by one bad second.
	double EffectiveErrorPixels = 0.0;

	TMap<uint64, FPatchScatter> LiveScatter;

	/// Which bucket a patch's instances live in. A hash of the key rather than
	/// anything spatial: neighbouring patches landing in different buckets is
	/// what stops one bucket holding everything in front of the camera.
	static int32 ScatterBucketOf(uint64 Key)
	{
		return static_cast<int32>((Key * 0x9E3779B97F4A7C15ull) >> 60) % ScatterBuckets;
	}

	void RebuildScatter();

	/// Last computed geomorph scale, so an instance created between frames can
	/// be brought up to date without waiting for the next one.
	double MorphScale = 0.0;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> WaterMaterial;

	TArray<TUniquePtr<FLedgerQuadNode>> Roots;

	/// Leaf key -> index into `MeshPool`, for patches whose geometry is live.
	TMap<uint64, int32> ActiveSections;
	/// Leaf key -> job, for patches currently being generated.
	TMap<uint64, FLedgerPatchJobRef> InFlight;

	/// Per-section record of what is loaded there, parallel to `MeshPool`.
	TArray<FLedgerSectionMeta> SectionMeta;

	/// Geometry for patches that were drawn and are not drawn now. Nothing in
	/// here is on screen; entries exist so that wanting a patch again costs an
	/// upload rather than a generation.
	FLedgerPatchCache PatchCache;
	TArray<int32> FreeSections;

	FLedgerTerrainStats Stats;
	TArray<IConsoleObject*> ConsoleCommands;

	FVector3d LastCameraLocal = FVector3d::ZeroVector;
	FVector3d CameraVelocityLocal = FVector3d::ZeroVector;
	double NextTraceAt = 0.0;

	void BuildRoots();
	void UpdateTree(FLedgerQuadNode& Node, const FVector3d& CameraLocal, const FVector3d& LeadLocal, double ViewportWidth, double FovRadians, bool bForceCollapse);
	/// Collects the nodes that need geometry, and marks the urgent ones: leaves
	/// with nothing standing in for them (holes), and the resident shell, which
	/// is what stops the next arrival being a hole.
	void CollectLeaves(const FLedgerQuadNode& Node, TArray<const FLedgerQuadNode*>& Out,
		TArray<uint8>& OutUrgent, bool bAncestorHasGeometry) const;
	void Split(FLedgerQuadNode& Node);
	void Collapse(FLedgerQuadNode& Node);
	bool IsBeyondHorizon(const FLedgerQuadNode& Node, const FVector3d& CameraLocal) const;

	/// Depth of the leaf containing a direction. Used for edge stitching, and it
	/// crosses cube faces for free because it starts from the direction rather
	/// than from a face-local coordinate.
	int32 LeafDepthAt(const FVector3d& UnitDirection) const;

	/// The same, from face coordinates, which may run outside [0,1] to reach a
	/// neighbouring face.
	///
	/// **The one the stitch probes want.** They know the face position they are
	/// asking about; going out to a sphere direction and back needs the inverse
	/// of CubeToSphere's warp, which is a forty-step fixed point, and putting
	/// that in this path took the terrain's game-thread cost from 6.6 ms to 16.
	/// Resolving an out-of-range face coordinate onto its real face is exact
	/// and costs a divide.
	int32 LeafDepthAtFace(ELedgerCubeFace Face, double U, double V) const;

	/// The tree descent both entry points share, on coordinates already known
	/// to belong to this face.
	int32 LeafDepthAtResolved(ELedgerCubeFace Face, double U, double V) const;

	/// The node whose geometry is on screen at these face coordinates.
	const FLedgerQuadNode* DrawnNodeAt(ELedgerCubeFace Face, double U, double V) const;

	/// Launches generation on a worker thread. Returns false if the pool or the
	/// in-flight cap is exhausted.
	bool LaunchPatch(const FLedgerQuadNode& Node, bool bWithCollision);

	/// Uploads whatever finished since last frame, within the budget.
	void HarvestCompletedPatches();

	/// Serves a leaf from the cache if the geometry is there and still correct.
	bool UploadFromCache(const FLedgerQuadNode& Node, bool bWithCollision);


	/// The instance if there is one, the base material otherwise. Patches take
	/// this rather than SurfaceMaterial, because the geomorph parameters live on
	/// the instance and a patch wearing the base material would not blend.
	UMaterialInterface* TerrainMaterial(const FLedgerBiomePalette& Palette);

	/// Feeds the geomorph blend the same numbers the LOD decision uses. Called
	/// once a frame; if the two ever disagree the pop does not disappear, it
	/// moves somewhere else.
	void UpdateMorphParameters(double ViewportWidth, double FovRadians);

	/// Binds the geomorph parameters onto one instance. Every palette needs
	/// them, and a palette created mid-frame needs them before it draws.
	void ApplyMorphParameters(UMaterialInstanceDynamic& Instance) const;

	void ReleaseSection(uint64 Key);
	void AbandonJob(uint64 Key);

	FVector3d UnitSphereAt(const FLedgerQuadNode& Node, double LocalU, double LocalV) const;

	void LogStats() const;
};

/// Generates a patch's geometry. Free function, no engine state, safe to call
/// from any thread â€” which is the point.
LEDGERTERRAIN_API void LedgerGeneratePatch(FLedgerPatchJob& Job);
