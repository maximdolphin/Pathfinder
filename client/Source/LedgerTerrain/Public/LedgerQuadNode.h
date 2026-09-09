// A node of the quadtree, and the key that identifies one.
//
// Its own header rather than the actor's, because three files now need the node
// and none of them needs the actor. The key in particular was a file-local
// helper until the planet was split, at which point it turned out to be the
// thing every part of the streaming system agrees on — which is exactly the
// kind of function that should never have been private to one translation unit.

#pragma once

#include "CoreMinimal.h"
#include "LedgerTerrainMath.h"

/// A node of the quadtree. Plain data â€” the tree is walked, not dispatched to.
struct FLedgerQuadNode
{
	ELedgerCubeFace Face = ELedgerCubeFace::PositiveX;
	int32 Depth = 0;

	/// Position and extent within the face's `[0,1]^2` parameter space.
	double U = 0.0;
	double V = 0.0;
	double Extent = 1.0;

	/// Centre of the node on the reference sphere, in planet-local space. Mesh
	/// vertices are built relative to this so float precision stays local â€”
	/// which is what lets the same code run at 60 km or at 6,371 km without the
	/// vertices falling apart.
	FVector3d Centre = FVector3d::ZeroVector;

	/// The same point pushed out to the terrain surface, in planet-local space.
	///
	/// Cached rather than evaluated because the LOD metric wants the distance
	/// to the *ground*, not to the reference sphere, and getting it means a
	/// full fractal elevation sample. A node's centre never moves, so that
	/// sample has the same answer for the life of the node -- and it was being
	/// taken once per visited node per frame. That was 24 ms of the game
	/// thread on approach and 290 ms in the single frame where a climb
	/// collapses the tree, against a 16.7 ms budget, and it was the whole
	/// overrun. See docs/comparisons/terrain-component.md.
	FVector3d SurfacePoint = FVector3d::ZeroVector;

	/// Approximate world-space extent of the node in centimetres.
	double WorldSize = 0.0;

	TStaticArray<TUniquePtr<FLedgerQuadNode>, 4> Children;
	bool bHasChildren = false;

	/// Set each frame by the LOD pass. A node on the far side of the planet is
	/// neither drawn nor subdivided.
	bool bVisible = true;

	/// The metric wants this node collapsed and it has no geometry of its own
	/// yet. Its children stay on screen until it does — collapsing first leaves
	/// nothing at all drawing this ground.
	bool bWantsCollapse = false;

	/// Frames spent waiting for that geometry. Bounded, because the children
	/// being held hold sections from the same pool the parent needs one from,
	/// and an unbounded wait is a deadlock rather than a delay.
	int32 CollapseWaitFrames = 0;

	bool IsLeaf() const { return !bHasChildren; }
};

/// A stable key for a node: face, depth, and grid position within the face.
/// Packs into 64 bits up to depth 24, well past any MaxDepth worth having.
inline uint64 NodeKey(const FLedgerQuadNode& Node)
{
	const uint64 Cells = 1ull << Node.Depth;
	const uint64 X = static_cast<uint64>(
		FMath::Clamp(Node.U * static_cast<double>(Cells), 0.0, static_cast<double>(Cells - 1)));
	const uint64 Y = static_cast<uint64>(
		FMath::Clamp(Node.V * static_cast<double>(Cells), 0.0, static_cast<double>(Cells - 1)));
	return (static_cast<uint64>(Node.Face) << 58)
		| (static_cast<uint64>(Node.Depth) << 52)
		| (X << 26)
		| Y;
}
