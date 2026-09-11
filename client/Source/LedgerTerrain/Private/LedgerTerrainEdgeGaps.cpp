// Whether two drawn patches actually meet. T049.
//
// **Measured at the shared edge, from both sides, with the vertices the
// renderer was given.** The first attempt at this sampled the height function
// either side of a depth boundary and saw a 2.16 m step -- which cannot tell a
// gap in the mesh from two patches at different depths describing the same
// ground differently. The only thing that can is the geometry itself: every
// vertex on a patch's edge must lie on the edge line of the patch drawn on the
// other side of it, or there is a hole you can see the sky through.
//
// Edge stitching exists for exactly this, and the case T049 is about -- a
// finer neighbour several levels deep, which pure-distance LOD never makes and
// `-forcedepth` does -- has never had it checked.

#include "LedgerPlanet.h"

#include "LedgerQuadNode.h"
#include "ProceduralMeshComponent.h"

namespace
{
	double EdgeGapPointToSegment(const FVector3d& P, const FVector3d& A, const FVector3d& B)
	{
		const FVector3d AB = B - A;
		const double Length2 = AB.SizeSquared();
		const double T = Length2 > 0.0
			? FMath::Clamp(FVector3d::DotProduct(P - A, AB) / Length2, 0.0, 1.0)
			: 0.0;
		return (P - (A + AB * T)).Length();
	}
}

double ALedgerPlanet::MeasureEdgeGaps(FString& OutReport) const
{
	const int32 Side = FMath::Max(3, GridResolution | 1);

	struct FDrawn
	{
		const FLedgerQuadNode* Node = nullptr;
		const TArray<FProcMeshVertex>* Vertices = nullptr;
		FVector3d Origin = FVector3d::ZeroVector;
	};

	// What is drawn: a node with a section that is also what DrawnNodeAt
	// answers at its own centre, so a parent still holding geometry behind its
	// children is not mistaken for the surface.
	TMap<const FLedgerQuadNode*, FDrawn> Drawn;
	int32 Irregular = 0;
	TArray<const FLedgerQuadNode*> Stack;
	for (const TUniquePtr<FLedgerQuadNode>& FaceRoot : Roots)
	{
		if (FaceRoot.IsValid())
		{
			Stack.Add(FaceRoot.Get());
		}
	}
	while (Stack.Num() > 0)
	{
		const FLedgerQuadNode* Node = Stack.Pop();
		for (const TUniquePtr<FLedgerQuadNode>& Child : Node->Children)
		{
			if (Child.IsValid())
			{
				Stack.Add(Child.Get());
			}
		}
		const int32* Section = ActiveSections.Find(NodeKey(*Node));
		if (Section == nullptr
			|| DrawnNodeAt(Node->Face, Node->U + Node->Extent * 0.5, Node->V + Node->Extent * 0.5) != Node)
		{
			continue;
		}
		UProceduralMeshComponent* Mesh = PooledProcedural(*Section);
		FProcMeshSection* Land = Mesh != nullptr ? Mesh->GetProcMeshSection(0) : nullptr;
		if (Land == nullptr || Land->ProcVertexBuffer.Num() != Side * Side)
		{
			++Irregular;
			continue;
		}
		Drawn.Add(Node, FDrawn{ Node, &Land->ProcVertexBuffer, FVector3d(Mesh->GetComponentLocation()) });
	}

	auto World = [Side](const FDrawn& Patch, int32 X, int32 Y)
	{
		return Patch.Origin + FVector3d((*Patch.Vertices)[Y * Side + X].Position);
	};

	// The four edges, as (X, Y) of the k-th vertex along each, the outward step
	// in face coordinates, and which edge of the neighbour faces this one.
	enum EEdge { Left, Right, Bottom, Top };
	auto EdgeVertex = [Side](EEdge Edge, int32 K, int32& X, int32& Y)
	{
		switch (Edge)
		{
		case Left:   X = 0;        Y = K;        break;
		case Right:  X = Side - 1; Y = K;        break;
		case Bottom: X = K;        Y = 0;        break;
		default:     X = K;        Y = Side - 1; break;
		}
	};
	auto Opposite = [](EEdge Edge)
	{
		return Edge == Left ? Right : Edge == Right ? Left : Edge == Bottom ? Top : Bottom;
	};

	double Worst = 0.0;
	int32 WorstDepth = 0;
	int32 WorstNeighbourDepth = 0;
	int32 Measured = 0;
	int32 Skipped = 0;
	int32 DeepestGapLevels = 0;
	int32 MeasuredAcrossLevels = 0;

	for (const TPair<const FLedgerQuadNode*, FDrawn>& Entry : Drawn)
	{
		const FLedgerQuadNode& Node = *Entry.Key;
		const double Step = Node.Extent * 1e-4;
		for (int32 E = 0; E < 4; ++E)
		{
			const EEdge Edge = static_cast<EEdge>(E);
			for (int32 K = 0; K < Side; ++K)
			{
				int32 X = 0;
				int32 Y = 0;
				EdgeVertex(Edge, K, X, Y);
				double U = Node.U + Node.Extent * X / static_cast<double>(Side - 1);
				double V = Node.V + Node.Extent * Y / static_cast<double>(Side - 1);
				U += Edge == Left ? -Step : Edge == Right ? Step : 0.0;
				V += Edge == Bottom ? -Step : Edge == Top ? Step : 0.0;

				// A cube-face border is another face's patch with its own
				// orientation; this measures within a face, which is where
				// the forced region and its boundary are.
				if (U < 0.0 || U > 1.0 || V < 0.0 || V > 1.0)
				{
					++Skipped;
					continue;
				}
				const FLedgerQuadNode* Across = DrawnNodeAt(Node.Face, U, V);
				const FDrawn* Neighbour = Across != nullptr ? Drawn.Find(Across) : nullptr;
				if (Neighbour == nullptr || Across == &Node)
				{
					++Skipped;
					continue;
				}

				// The distance from this vertex to the neighbour's facing edge,
				// as a polyline through every vertex that edge has.
				const FVector3d P = World(Entry.Value, X, Y);
				const EEdge Facing = Opposite(Edge);
				double Nearest = TNumericLimits<double>::Max();
				for (int32 J = 0; J + 1 < Side; ++J)
				{
					int32 AX = 0, AY = 0, BX = 0, BY = 0;
					EdgeVertex(Facing, J, AX, AY);
					EdgeVertex(Facing, J + 1, BX, BY);
					Nearest = FMath::Min(Nearest, EdgeGapPointToSegment(
						P, World(*Neighbour, AX, AY), World(*Neighbour, BX, BY)));
				}

				++Measured;
				const int32 Levels = FMath::Abs(Node.Depth - Across->Depth);
				MeasuredAcrossLevels += Levels > 0 ? 1 : 0;
				DeepestGapLevels = FMath::Max(DeepestGapLevels, Levels);
				if (Nearest > Worst)
				{
					Worst = Nearest;
					WorstDepth = Node.Depth;
					WorstNeighbourDepth = Across->Depth;
				}
			}
		}
	}

	OutReport = FString::Printf(
		TEXT("edge gaps (T049): %d drawn patches, %d edge vertices measured, "
			 "%d of them across a depth change of up to %d levels; %d skipped "
			 "(cube-face borders or no drawn neighbour), %d irregular sections\n"
			 "worst gap %.2f cm, between depth %d and depth %d\n"),
		Drawn.Num(), Measured, MeasuredAcrossLevels, DeepestGapLevels, Skipped,
		Irregular, Worst, WorstDepth, WorstNeighbourDepth);
	return Worst;
}
