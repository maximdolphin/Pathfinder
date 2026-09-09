// The quadtree: building it, splitting it, collapsing it, and deciding which
// of that to do each frame.
//
// These are still ALedgerPlanet methods rather than a separate type, and that
// is a deliberate stopping point rather than an unfinished one. The pure
// geometry — cube-sphere mapping, node keys, horizon tests — is already in
// LedgerTerrainMath and LedgerCore. What is left here is *policy*, and the
// policy reads streaming state: whether a node has geometry decides whether it
// may collapse, and whether the pool is exhausted decides how long it waits.
// Extracting it into a type would mean handing that type the streaming state,
// which is the same coupling with more indirection in front of it.

#include "LedgerPlanet.h"

#include "LedgerQuadNode.h"

#include "Engine/World.h"
#include "LedgerLog.h"
#include "LedgerTerrainMath.h"

void ALedgerPlanet::BuildRoots()
{
	Roots.Empty();
	for (int32 FaceIndex = 0; FaceIndex < static_cast<int32>(ELedgerCubeFace::Count); ++FaceIndex)
	{
		TUniquePtr<FLedgerQuadNode> Node = MakeUnique<FLedgerQuadNode>();
		Node->Face = static_cast<ELedgerCubeFace>(FaceIndex);
		Node->Depth = 0;
		Node->U = 0.0;
		Node->V = 0.0;
		Node->Extent = 1.0;
		Node->Centre = LedgerTerrain::CubeToSphere(
			LedgerTerrain::FaceToCube(Node->Face, 0.5, 0.5)) * Radius;
		const FVector3d RootDirection = Node->Centre.GetSafeNormal();
		Node->SurfacePoint = RootDirection * SurfaceRadiusAt(RootDirection);
		// A face spans a quarter of the circumference.
		Node->WorldSize = Radius * PI * 0.5;
		Roots.Add(MoveTemp(Node));
	}
}

FVector3d ALedgerPlanet::UnitSphereAt(const FLedgerQuadNode& Node, double LocalU, double LocalV) const
{
	const double U = Node.U + LocalU * Node.Extent;
	const double V = Node.V + LocalV * Node.Extent;
	return LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(Node.Face, U, V));
}

double ALedgerPlanet::SurfaceRadiusAt(const FVector3d& UnitDirection) const
{
	return Radius + LedgerTerrain::Elevation(UnitDirection.GetSafeNormal(), TerrainParams());
}

void ALedgerPlanet::Split(FLedgerQuadNode& Node)
{
	if (Node.bHasChildren || Node.Depth >= MaxDepth)
	{
		return;
	}

	const double Half = Node.Extent * 0.5;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TUniquePtr<FLedgerQuadNode> Child = MakeUnique<FLedgerQuadNode>();
		Child->Face = Node.Face;
		Child->Depth = Node.Depth + 1;
		Child->Extent = Half;
		Child->U = Node.U + ((Index & 1) ? Half : 0.0);
		Child->V = Node.V + ((Index & 2) ? Half : 0.0);
		Child->WorldSize = Node.WorldSize * 0.5;
		Child->Centre = LedgerTerrain::CubeToSphere(
			LedgerTerrain::FaceToCube(Child->Face, Child->U + Half * 0.5, Child->V + Half * 0.5)) * Radius;
		const FVector3d ChildDirection = Child->Centre.GetSafeNormal();
		Child->SurfacePoint = ChildDirection * SurfaceRadiusAt(ChildDirection);
		Node.Children[Index] = MoveTemp(Child);
	}
	Node.bHasChildren = true;
}

void ALedgerPlanet::Collapse(FLedgerQuadNode& Node)
{
	if (!Node.bHasChildren)
	{
		return;
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Node.Children[Index].IsValid())
		{
			Collapse(*Node.Children[Index]);
			const uint64 Key = NodeKey(*Node.Children[Index]);
			ReleaseSection(Key);
			AbandonJob(Key);
			Node.Children[Index].Reset();
		}
	}
	Node.bHasChildren = false;
}

bool ALedgerPlanet::IsBeyondHorizon(const FLedgerQuadNode& Node, const FVector3d& CameraLocal) const
{
	const double CameraDistance = CameraLocal.Length();

	// The horizon is computed against the *reference sphere*, not the highest
	// possible mountain. Testing against `Radius + MaxElevation` disables culling
	// for any camera below peak elevation — i.e. every camera near the ground.
	// Terrain that pokes over the horizon is handled by the margin below.
	if (CameraDistance <= Radius)
	{
		return false;
	}

	const double HorizonAngle = FMath::Acos(FMath::Clamp(Radius / CameraDistance, -1.0, 1.0));

	// The node's angular radius, since WorldSize is an arc length on the sphere,
	// plus the angle a full-height peak could show above the tangent.
	const double NodeAngle = (Node.WorldSize * 0.7071) / Radius
		+ FMath::Sqrt(2.0 * MaxElevation / Radius);

	const FVector3d NodeDirection = Node.Centre.GetSafeNormal();
	const FVector3d CameraDirection = CameraLocal / CameraDistance;
	const double NodeAngleFromCamera = FMath::Acos(
		FMath::Clamp(FVector3d::DotProduct(NodeDirection, CameraDirection), -1.0, 1.0));

	return NodeAngleFromCamera > HorizonAngle + NodeAngle;
}

void ALedgerPlanet::UpdateTree(
	FLedgerQuadNode& Node,
	const FVector3d& CameraLocal,
	const FVector3d& LeadLocal,
	double ViewportWidth,
	double FovRadians,
	bool bForceCollapse)
{
	// Cull first: a node the horizon hides needs neither geometry nor children.
	// Against both positions, not just the current one — a node about to rise
	// over the horizon is a node that should already be building.
	Node.bVisible = !IsBeyondHorizon(Node, CameraLocal) || !IsBeyondHorizon(Node, LeadLocal);
	if (!Node.bVisible)
	{
		if (Node.bHasChildren)
		{
			Collapse(Node);
		}
		const uint64 Key = NodeKey(Node);
		ReleaseSection(Key);
		AbandonJob(Key);
		return;
	}

	// Distance to the node's surface point, not to the reference sphere — at low
	// altitude over a mountain the difference is the whole LOD decision. Sampled
	// once when the node is created; see FLedgerQuadNode::SurfacePoint.
	const FVector3d& NodeSurface = Node.SurfacePoint;

	// The nearer of where the camera is and where it will shortly be. Taking the
	// minimum rather than replacing one with the other matters: leading alone
	// would collapse the ground behind a fast mover, and the ground behind is
	// still on screen.
	const double Distance = FMath::Min(
		FVector3d::Distance(CameraLocal, NodeSurface),
		FVector3d::Distance(LeadLocal, NodeSurface));

	const double Error = LedgerTerrain::ScreenSpaceError(
		Node.WorldSize, Distance, ViewportWidth, FovRadians);

	if (!bForceCollapse && Error > EffectiveErrorPixels && Node.Depth < MaxDepth)
	{
		if (!Node.bHasChildren)
		{
			Split(Node);
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Node.Children[Index].IsValid())
			{
				UpdateTree(*Node.Children[Index], CameraLocal, LeadLocal, ViewportWidth, FovRadians, false);
			}
		}

		// Only drop the parent's geometry once every child has some. Releasing
		// it at split time is what punched black holes through the planet when
		// the pool or the job queue was saturated — a starved budget should cost
		// detail, not holes.
		bool bAllChildrenReady = true;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FLedgerQuadNode* Child = Node.Children[Index].Get();
			if (Child == nullptr)
			{
				continue;
			}
			if (Child->IsLeaf() && !ActiveSections.Contains(NodeKey(*Child)))
			{
				bAllChildrenReady = false;
				break;
			}
		}
		// The resident shell keeps its geometry even once its children have
		// theirs. That is the whole point of it: it is not standing in for
		// them, it is standing by for the next time the camera arrives
		// somewhere with nothing built. Releasing it here is what made the
		// shell do nothing at all -- the overload run reported holes in a
		// hundred per cent of frames with the shell in place, because the shell
		// was being dismantled as fast as it was built.
		if (bAllChildrenReady && Node.Depth > ResidentDepth)
		{
			ReleaseSection(NodeKey(Node));
		}
		return;
	}

	if (Node.bHasChildren)
	{
		// The mirror of the split rule above. A split keeps the parent until
		// every child has geometry; a collapse has to keep the children until
		// the parent has some. Dropping them first leaves nothing drawing this
		// ground, which is what punched three thousand holes through the planet
		// on the way out to orbit.
		if (ActiveSections.Contains(NodeKey(Node)))
		{
			Node.bWantsCollapse = false;
			Node.CollapseWaitFrames = 0;
			Collapse(Node);
			return;
		}

		// The wait is bounded, and it has to be. The four children being held
		// occupy sections from the same pool this node needs one from, so a
		// subtree that waits indefinitely is not waiting — it is holding the
		// resource that would end the wait. Under a starved pool that is a
		// deadlock, and it showed up as fifteen hundred patches of unfillable
		// demand that grew for as long as the climb lasted.
		//
		// Past the deadline, collapse regardless and accept a hole for a few
		// frames. A transient hole is a worse frame; a deadlock is a worse game.
		if (++Node.CollapseWaitFrames > 90)
		{
			Node.bWantsCollapse = false;
			Node.CollapseWaitFrames = 0;
			Collapse(Node);
			return;
		}

		// Waiting on our own patch. Push the same decision down rather than
		// stopping here: a climb wants to collapse ten levels at once, and a
		// subtree that stays whole while its root waits is a visible set that
		// grows exactly when it should be shrinking. With the intent pushed
		// down, the tree converges from the bottom.
		//
		// Only the level whose children are already leaves actually asks for
		// geometry — see CollectLeaves. One extra level in flight, not ten.
		Node.bWantsCollapse = true;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Node.Children[Index].IsValid())
			{
				UpdateTree(*Node.Children[Index], CameraLocal, LeadLocal,
					ViewportWidth, FovRadians, /*bForceCollapse*/ true);
			}
		}
		return;
	}

	Node.bWantsCollapse = false;
	Node.CollapseWaitFrames = 0;
}

void ALedgerPlanet::CollectLeaves(const FLedgerQuadNode& Node,
	TArray<const FLedgerQuadNode*>& Out, TArray<uint8>& OutUrgent,
	bool bAncestorHasGeometry) const
{
	if (!Node.bVisible)
	{
		return;
	}

	const bool bHasGeometry = ActiveSections.Contains(NodeKey(Node));

	if (Node.IsLeaf())
	{
		// Nothing anywhere up the chain is drawing this ground. Not a coarse
		// patch standing in for a fine one -- a hole. Recorded per node as
		// well as counted, because it is what decides whether this node's
		// geometry gets paid for before somebody else's detail (T063).
		const bool bIsHole = !bHasGeometry && !bAncestorHasGeometry;
		Out.Add(&Node);
		OutUrgent.Add(bIsHole ? 1 : 0);
		if (bIsHole)
		{
			++const_cast<ALedgerPlanet*>(this)->Stats.UnfilledNodes;
		}
		return;
	}

	// A split node still holds geometry while its children are being built, so
	// it counts for rendering purposes until they arrive.
	//
	// A node waiting to collapse has none and needs some, and the only way to
	// ask for it is to be in the list the leaf pass walks — but only once its
	// children are leaves. Requesting at every level of a collapsing subtree at
	// once is ten levels of geometry in flight to replace one; requesting only
	// at the bottom edge is one, and the subtree walks up a level at a time.
	bool bCollapseFront = Node.bWantsCollapse;
	if (bCollapseFront)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FLedgerQuadNode* Child = Node.Children[Index].Get();
			if (Child != nullptr && !Child->IsLeaf())
			{
				bCollapseFront = false;
				break;
			}
		}
	}

	// The resident shell keeps geometry whether it is a leaf or not, so that
	// arriving anywhere on the planet finds coarse ground already there rather
	// than a hole waiting to be filled. See ResidentDepth.
	const bool bResident = Node.Depth <= ResidentDepth;
	if (bHasGeometry || bCollapseFront || bResident)
	{
		Out.Add(&Node);

		// **Urgent, not a hole.** A split resident node with no geometry of its
		// own is covered by its children and nothing is missing on screen; what
		// it is, is the thing that will cover the screen the next time the
		// camera arrives somewhere new, so it is worth paying for before
		// somebody's detail. Marking it as a hole instead made the overload
		// report say a hundred per cent of frames had holes when what they had
		// was a shell being prefetched -- a measurement counting its own fix.
		OutUrgent.Add(!bHasGeometry && bResident ? 1 : 0);
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Node.Children[Index].IsValid())
		{
			CollectLeaves(*Node.Children[Index], Out, OutUrgent,
				bAncestorHasGeometry || bHasGeometry);
		}
	}
}

int32 ALedgerPlanet::LeafDepthAtFace(ELedgerCubeFace Face, double U, double V) const
{
	// An out-of-range coordinate names a point on this face's extended plane,
	// and which face it really belongs to is whichever cube coordinate is now
	// largest -- which is what DirectionToFace decides. No sphere in this, so
	// no warp to undo.
	if (U < 0.0 || U > 1.0 || V < 0.0 || V > 1.0)
	{
		LedgerTerrain::DirectionToFace(
			LedgerTerrain::FaceToCube(Face, U, V), Face, U, V);
	}
	return LeafDepthAtResolved(Face, U, V);
}

int32 ALedgerPlanet::LeafDepthAt(const FVector3d& UnitDirection) const
{
	ELedgerCubeFace Face = ELedgerCubeFace::PositiveX;
	double U = 0.0;
	double V = 0.0;
	// SphereToFace, not DirectionToFace. DirectionToFace inverts FaceToCube and
	// not CubeToSphere's warp, so a sphere direction comes back as face
	// coordinates up to 0.066 of a face out -- 657 km on a cube edge of 10,008.
	LedgerTerrain::SphereToFace(UnitDirection, Face, U, V);

	return LeafDepthAtResolved(Face, U, V);
}

int32 ALedgerPlanet::LeafDepthAtResolved(ELedgerCubeFace Face, double U, double V) const
{
	const FLedgerQuadNode* Node = Roots.IsValidIndex(static_cast<int32>(Face))
		? Roots[static_cast<int32>(Face)].Get()
		: nullptr;
	if (Node == nullptr)
	{
		return 0;
	}

	while (Node->bHasChildren)
	{
		// Stop at whatever is actually being *drawn*, not at the tree's leaf.
		//
		// A split node keeps its geometry until all four children have theirs,
		// so during streaming the thing on screen at this direction is the
		// parent, at the parent's resolution — while the tree below it is
		// already several levels deeper.
		//
		// This function's answer decides edge stitching. Returning the tree
		// depth meant a patch stitched against a neighbour that was not the one
		// it met, for as long as that neighbour was streaming: a crack that
		// appears under load, closes when the load passes, and is therefore
		// invisible in any still taken afterwards.
		if (ActiveSections.Contains(NodeKey(*Node)))
		{
			break;
		}

		const double Half = Node->Extent * 0.5;
		const int32 Index = (U >= Node->U + Half ? 1 : 0) | (V >= Node->V + Half ? 2 : 0);
		const FLedgerQuadNode* Child = Node->Children[Index].Get();
		if (Child == nullptr)
		{
			break;
		}
		Node = Child;
	}

	return Node->Depth;
}

