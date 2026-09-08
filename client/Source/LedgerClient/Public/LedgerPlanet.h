// Cube-sphere quadtree terrain. Design §6.8.
//
// Six root faces, recursively subdivided against a screen-space error metric
// relative to camera altitude. Crack prevention by edge-index stitching, not
// skirts. Collision cooked only near the player, async, off the game thread.
//
// **The schedule risk lives in the collision.** §6.8 is explicit that Chaos
// heightfield cooking is the hitch source in every implementation of this, and
// that the cure is a budgeted, *predictive* system that cooks ahead along the
// velocity vector rather than on demand. That is what `CollisionLeadSeconds`
// and the per-frame build budget below are for, and `Ledger.Terrain.Stats`
// reports whether it is working.
//
// Nanite is deliberately absent: its clusters are built offline, so a
// runtime-generated streamed quadtree cannot use it (design §6.8, amended v1.1).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LedgerTerrainMath.h"
#include "LedgerPlanet.generated.h"

class UProceduralMeshComponent;

/// A node of the quadtree. Plain data — the tree is walked, not dispatched to.
struct FLedgerQuadNode
{
	ELedgerCubeFace Face = ELedgerCubeFace::PositiveX;
	int32 Depth = 0;

	/// Position and extent within the face's `[0,1]^2` parameter space.
	double U = 0.0;
	double V = 0.0;
	double Extent = 1.0;

	/// Centre of the node on the reference sphere, in planet-local space. Mesh
	/// vertices are built relative to this so float precision stays local —
	/// which is what lets the same code run at 60 km or at planetary scale
	/// without the vertices falling apart.
	FVector3d Centre = FVector3d::ZeroVector;

	/// Approximate world-space extent of the node in centimetres.
	double WorldSize = 0.0;

	TStaticArray<TUniquePtr<FLedgerQuadNode>, 4> Children;
	bool bHasChildren = false;

	/// Set each frame by the LOD pass. A node on the far side of the planet is
	/// neither drawn nor subdivided — without this, half the quadtree is spent
	/// on geometry the horizon hides.
	bool bVisible = true;

	bool IsLeaf() const { return !bHasChildren; }
};

/// What the terrain is doing, for the §15.1 build/buy decision. These numbers
/// are the deliverable of the spike — an ADR that says "we build it" or "we buy
/// it" needs a cook-time trace attached, not an opinion.
USTRUCT()
struct FLedgerTerrainStats
{
	GENERATED_BODY()

	UPROPERTY()
	int32 VisibleNodes = 0;

	UPROPERTY()
	int32 NodesWithCollision = 0;

	UPROPERTY()
	int32 BuildsThisFrame = 0;

	UPROPERTY()
	int32 PendingBuilds = 0;

	UPROPERTY()
	int64 TotalBuilds = 0;

	/// Milliseconds spent generating vertex data on the game thread last frame.
	UPROPERTY()
	double LastFrameBuildMs = 0.0;

	/// Worst single-frame build cost seen. If this creeps up under motion, the
	/// budget is not holding and the architecture is wrong (§6.8).
	UPROPERTY()
	double WorstFrameBuildMs = 0.0;

	/// Milliseconds of collision cook requested last frame. Async, so this is
	/// the request cost rather than the cook itself — the cook lands on a task
	/// thread and the mesh section picks it up when it is ready.
	UPROPERTY()
	double LastFrameCollisionMs = 0.0;

	UPROPERTY()
	double WorstFrameCollisionMs = 0.0;
};

UCLASS()
class LEDGERCLIENT_API ALedgerPlanet : public AActor
{
	GENERATED_BODY()

public:
	ALedgerPlanet();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/// Reference sphere radius in centimetres.
	///
	/// 60 km by default rather than a real planet's 6,000 km. LWC handles the
	/// larger figure — 6e8 cm is well inside a double's envelope (§6.8) — and
	/// nothing in this class assumes otherwise, since vertices are already
	/// built relative to each node's own centre. The smaller number simply
	/// means a shorter flight from orbit to ground while measuring, which is
	/// what the spike is for.
	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	double Radius = 6000000.0;

	/// Peak elevation above the reference sphere, in centimetres. 2.5 km on a
	/// 60 km planet — proportionally far more dramatic than Earth, and
	/// deliberately so: the point of the spike is to see the LOD work, and
	/// Earth's relief is invisible from orbit without vertical exaggeration.
	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	double MaxElevation = 250000.0;

	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	int32 Seed = 20260908;

	/// Vertices along one edge of a node's grid. Must be odd so edge stitching
	/// has a midpoint to collapse.
	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	int32 GridResolution = 33;

	/// Depth 8 gives ~370 m nodes and ~11 m quads at a 60 km radius.
	///
	/// Depth 10 is what the error metric asks for near the ground, and it is
	/// also what buries this: the visible set climbs past eight thousand nodes
	/// and the per-node build cost (below) cannot keep up, so the terrain
	/// renders with holes. Capping the depth is a *ceiling*, not a fix — see
	/// the note on `BuildBudgetPerFrame`.
	UPROPERTY(EditAnywhere, Category = "Ledger|Planet")
	int32 MaxDepth = 8;

	/// Subdivide when a node's projected error exceeds this many pixels.
	UPROPERTY(EditAnywhere, Category = "Ledger|LOD")
	double ErrorThresholdPixels = 96.0;

	/// Milliseconds per frame the terrain may spend generating geometry.
	///
	/// A *time* budget, not a node count. A count cannot know that eight nodes
	/// cost 53 ms this frame and two cost 12 ms the next, and the count version
	/// of this produced exactly that spread.
	///
	/// Measured: a 33x33 patch costs roughly 5-8 ms on the game thread, almost
	/// all of it in height sampling and tangent calculation. That is the real
	/// ceiling on this implementation — the budget can buy smooth frames or a
	/// filled-in horizon, not both. The fix is not a bigger number here: it is
	/// generating the heightfield on the GPU (§6.8's "GPU compute pass over
	/// layered noise") or buying a plugin that already has. **This measurement
	/// is the deliverable of the §15.1 spike.**
	UPROPERTY(EditAnywhere, Category = "Ledger|LOD")
	double BuildBudgetMs = 6.0;

	/// Cook collision within this distance of the camera.
	UPROPERTY(EditAnywhere, Category = "Ledger|Collision")
	double CollisionRadius = 300000.0;

	/// Cook ahead along the velocity vector by this many seconds of travel.
	/// Predictive, never on demand — §6.8's one non-negotiable.
	UPROPERTY(EditAnywhere, Category = "Ledger|Collision")
	double CollisionLeadSeconds = 2.5;

	const FLedgerTerrainStats& GetStats() const { return Stats; }

	/// Height of the surface above the reference sphere at a direction.
	double SurfaceRadiusAt(const FVector3d& UnitDirection) const;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	/// A pool of mesh components, one per visible leaf. Components are recycled
	/// rather than destroyed: churning `UProceduralMeshComponent`s under a
	/// moving camera costs more than keeping a few dozen idle.
	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> MeshPool;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SurfaceMaterial;

	TArray<TUniquePtr<FLedgerQuadNode>> Roots;

	/// Leaf key -> index into `MeshPool`.
	TMap<uint64, int32> ActiveSections;
	TArray<int32> FreeSections;

	FLedgerTerrainStats Stats;
	TArray<IConsoleObject*> ConsoleCommands;

	FVector3d LastCameraLocal = FVector3d::ZeroVector;
	FVector3d CameraVelocityLocal = FVector3d::ZeroVector;

	void BuildRoots();
	void UpdateTree(FLedgerQuadNode& Node, const FVector3d& CameraLocal, double ViewportWidth, double FovRadians);
	void CollectLeaves(const FLedgerQuadNode& Node, TArray<const FLedgerQuadNode*>& Out) const;

	/// True when the node's cap is entirely beyond the horizon from the camera.
	bool IsBeyondHorizon(const FLedgerQuadNode& Node, const FVector3d& CameraLocal) const;
	void Split(FLedgerQuadNode& Node);
	void Collapse(FLedgerQuadNode& Node);

	/// Depth of the leaf containing a direction. Used for edge stitching, and
	/// it crosses cube faces for free because it starts from the direction
	/// rather than from a face-local coordinate.
	int32 LeafDepthAt(const FVector3d& UnitDirection) const;

	void BuildSection(const FLedgerQuadNode& Node, int32 SectionIndex, bool bWithCollision);
	void ReleaseSection(uint64 Key);

	FVector3d UnitSphereAt(const FLedgerQuadNode& Node, double LocalU, double LocalV) const;

	void LogStats() const;

	/// Seconds until the next automatic stats line. A trace over time is what
	/// the build/buy ADR needs; a single snapshot says nothing about hitching.
	double NextTraceAt = 0.0;
};
