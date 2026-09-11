// Answering questions about the ground from the ground that is drawn. T061.

#include "LedgerPlanet.h"

#include "LedgerLog.h"
#include "LedgerQuadNode.h"
#include "LedgerTerrainSample.h"
#include "ProceduralMeshComponent.h"

namespace
{
	/// Barycentric weights of a point in a triangle, in the patch's own 2D
	/// coordinates. Negative components mean outside.
	FVector3d Barycentric(
		const FVector2d& Point,
		const FVector2d& A, const FVector2d& B, const FVector2d& C)
	{
		const FVector2d AB = B - A;
		const FVector2d AC = C - A;
		const double Area = AB.X * AC.Y - AB.Y * AC.X;
		if (FMath::IsNearlyZero(Area))
		{
			return FVector3d(1.0, 0.0, 0.0);
		}
		const FVector2d AP = Point - A;
		const double Beta = (AP.X * AC.Y - AP.Y * AC.X) / Area;
		const double Gamma = (AB.X * AP.Y - AB.Y * AP.X) / Area;
		return FVector3d(1.0 - Beta - Gamma, Beta, Gamma);
	}
}

const FLedgerQuadNode* ALedgerPlanet::DrawnNodeAt(
	ELedgerCubeFace Face, double U, double V) const
{
	const FLedgerQuadNode* Node = Roots.IsValidIndex(static_cast<int32>(Face))
		? Roots[static_cast<int32>(Face)].Get()
		: nullptr;

	// **The DEEPEST node with geometry, not the shallowest.**
	//
	// This used to stop at the first ancestor it found in ActiveSections, on
	// the reasoning that a split node keeps its geometry until all four
	// children have theirs, so during streaming the thing under the point is
	// the parent. True, and not the whole rule: children draw over their
	// parent, so once they have geometry the parent is behind them. And the
	// resident shell (ResidentDepth) never releases its sections at all, so
	// there is almost always an ancestor holding one.
	//
	// The consequence was a query that answered from a patch ten thousand
	// kilometres across whenever the tree was shaped by anything other than
	// distance. It appeared twice before it was understood: once under a
	// per-depth error threshold, which was reverted for it, and once under
	// `-forcedepth`, which is what made it reproducible. Both times the drawn
	// geometry was visibly correct in the same frame -- because the renderer
	// draws every section, and only this walk was picking the wrong one.
	//
	// SampleTerrain is what collision and gameplay read (T061). It has been
	// answering from the wrong patch for as long as the resident shell has
	// existed, in any situation that produced an unusual tree.
	const FLedgerQuadNode* Deepest =
		(Node != nullptr && ActiveSections.Contains(NodeKey(*Node))) ? Node : nullptr;

	while (Node != nullptr && Node->bHasChildren)
	{
		const double Half = Node->Extent * 0.5;
		const int32 Child = (U >= Node->U + Half ? 1 : 0) + (V >= Node->V + Half ? 2 : 0);
		const FLedgerQuadNode* Next = Node->Children[Child].Get();
		if (Next == nullptr)
		{
			break;
		}
		Node = Next;
		if (ActiveSections.Contains(NodeKey(*Node)))
		{
			Deepest = Node;
		}
	}

	// Nothing along the path is drawing this ground: a hole. Answering from the
	// leaf anyway would be answering from a patch that is not on screen.
	return Deepest;
}

bool ALedgerPlanet::SampleTerrain(
	const FVector3d& WorldPoint, FLedgerTerrainSample& Out) const
{
	Out = FLedgerTerrainSample();

	const FVector3d Direction = (WorldPoint - FVector3d(GetActorLocation())).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	// SphereToFace, because the query is a direction on the sphere and
	// DirectionToFace would answer about ground up to 657 km away
	// (docs/comparisons/projection/).
	ELedgerCubeFace Face = ELedgerCubeFace::PositiveX;
	double FaceU = 0.0;
	double FaceV = 0.0;
	LedgerTerrain::SphereToFace(Direction, Face, FaceU, FaceV);

	const FLedgerQuadNode* Node = DrawnNodeAt(Face, FaceU, FaceV);
	if (Node == nullptr)
	{
		return false;
	}

	const int32* SectionIndex = ActiveSections.Find(NodeKey(*Node));
	if (SectionIndex == nullptr)
	{
		return false;
	}

	// Non-const because GetProcMeshSection is, though it only reads. Sampling
	// the terrain does not modify it and the method stays const.
	UProceduralMeshComponent* Mesh = PooledProcedural(*SectionIndex);
	const FProcMeshSection* Section = Mesh != nullptr ? Mesh->GetProcMeshSection(0) : nullptr;
	if (Section == nullptr || Section->ProcVertexBuffer.Num() == 0)
	{
		return false;
	}

	Out.bCollision = Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision;

	const int32 Side = FMath::Max(3, GridResolution | 1);
	// At least the grid; the skirts follow it in the buffer.
	if (Section->ProcVertexBuffer.Num() < Side * Side)
	{
		// A patch built at a different grid resolution than this planet's --
		// which cannot happen today and would be silently wrong if it ever did.
		return false;
	}

	const double LocalU = FMath::Clamp((FaceU - Node->U) / Node->Extent, 0.0, 1.0);
	const double LocalV = FMath::Clamp((FaceV - Node->V) / Node->Extent, 0.0, 1.0);

	const double GridU = LocalU * (Side - 1);
	const double GridV = LocalV * (Side - 1);
	const int32 X0 = FMath::Clamp(FMath::FloorToInt32(GridU), 0, Side - 2);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(GridV), 0, Side - 2);
	const double FracX = GridU - X0;
	const double FracY = GridV - Y0;

	// The same two triangles the generator emitted for this cell, split the
	// same way. Interpolating over a quad would give a different surface from
	// the one that was cooked, by up to the cell's own curvature.
	const int32 A = Y0 * Side + X0;
	const int32 B = A + 1;
	const int32 C = A + Side;
	const int32 D = C + 1;

	const FVector2d Point(FracX, FracY);
	int32 Corner[3] = { A, C, B };
	FVector3d Weights = Barycentric(Point,
		FVector2d(0.0, 0.0), FVector2d(0.0, 1.0), FVector2d(1.0, 0.0));
	if (Weights.X < 0.0 || Weights.Y < 0.0 || Weights.Z < 0.0)
	{
		Corner[0] = B; Corner[1] = C; Corner[2] = D;
		Weights = Barycentric(Point,
			FVector2d(1.0, 0.0), FVector2d(0.0, 1.0), FVector2d(1.0, 1.0));
	}

	const FVector Centre = Mesh->GetComponentLocation();
	FVector3d Position = FVector3d::ZeroVector;
	FVector3d Normal = FVector3d::ZeroVector;
	FVector3f Weight = FVector3f::ZeroVector;
	float Snow = 0.0f;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FProcMeshVertex& Vertex = Section->ProcVertexBuffer[Corner[Index]];
		const double Share = Weights[Index];
		Position += (FVector3d(Vertex.Position) + FVector3d(Centre)) * Share;
		Normal += FVector3d(Vertex.Normal) * Share;
		Weight += FVector3f(
			Vertex.Color.R / 255.0f, Vertex.Color.G / 255.0f, Vertex.Color.B / 255.0f)
			* static_cast<float>(Share);
		Snow += (Vertex.Color.A / 255.0f) * static_cast<float>(Share);
	}

	const FVector3d FromCentre = Position - FVector3d(GetActorLocation());
	Out.bValid = true;
	Out.RadiusCm = FromCentre.Length();
	Out.AltitudeMetres = (Out.RadiusCm - Radius) / 100.0;
	Out.Normal = Normal.GetSafeNormal();
	Out.SlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector3d::DotProduct(Out.Normal, FromCentre.GetSafeNormal()), -1.0, 1.0)));
	Out.BiomeWeights = Weight;
	Out.Snow = Snow;
	Out.PatchWorldSize = Node->WorldSize;

	const FLedgerSectionMeta& Meta = SectionMeta[*SectionIndex];
	Out.Palette = Meta.Palette;

	// Climate is a function rather than a vertex attribute, so it is evaluated
	// rather than interpolated -- and at the *sample's* direction, which is
	// what the caller asked about.
	Out.Climate = LedgerClimate::At(FromCentre.GetSafeNormal(), TerrainParams(), SeasonPhase());
	Out.Climate.AltitudeMetres = Out.AltitudeMetres;
	return true;
}
