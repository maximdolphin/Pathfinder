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
		// The whole transform, not just the location. The planet turns, so every
		// patch component is rotated, and a vertex added to the origin without
		// that rotation lands off its true place by the rotation times its
		// distance from the patch's centre -- a kilometre at depth 8, where the
		// second run reported 29,420 "cracks" between patches of the SAME depth.
		FTransform Transform;
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
		// At least the grid; the skirts follow it in the buffer.
		if (Land == nullptr || Land->ProcVertexBuffer.Num() < Side * Side)
		{
			++Irregular;
			continue;
		}
		Drawn.Add(Node, FDrawn{ Node, &Land->ProcVertexBuffer, Mesh->GetComponentTransform() });
	}

	auto World = [Side](const FDrawn& Patch, int32 X, int32 Y)
	{
		return FVector3d(Patch.Transform.TransformPosition(FVector((*Patch.Vertices)[Y * Side + X].Position)));
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
	int32 Overlapping = 0;
	int32 OverOneCm = 0;
	int32 OverOneCmSameDepth = 0;
	int32 BeyondSkirt = 0;
	const FLedgerQuadNode* WorstNode = nullptr;
	const FLedgerQuadNode* WorstAcross = nullptr;
	double WorstRadialMetres = 0.0;
	int32 WorstEdge = 0;
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
				// **Adjacent, not overlapping.** What is drawn just past an edge
				// is a neighbour only if its facing edge lies on this edge's line.
				// A parent still drawn behind children that are streaming in
				// contains this patch instead of touching it, and its "facing
				// edge" is on the far side of it -- the first run reported a
				// 4,855 km gap between a quarter-face and the whole face it sits in.
				const double Eps = Node.Extent * 1e-6;
				const bool bAdjacent =
					Edge == Left   ? FMath::IsNearlyEqual(Across->U + Across->Extent, Node.U, Eps)
					: Edge == Right  ? FMath::IsNearlyEqual(Across->U, Node.U + Node.Extent, Eps)
					: Edge == Bottom ? FMath::IsNearlyEqual(Across->V + Across->Extent, Node.V, Eps)
					: FMath::IsNearlyEqual(Across->V, Node.V + Node.Extent, Eps);
				if (!bAdjacent)
				{
					++Overlapping;
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
				OverOneCm += Nearest > 1.0 ? 1 : 0;
				const int32 Levels = FMath::Abs(Node.Depth - Across->Depth);
				MeasuredAcrossLevels += Levels > 0 ? 1 : 0;
				DeepestGapLevels = FMath::Max(DeepestGapLevels, Levels);
				OverOneCmSameDepth += (Nearest > 1.0 && Levels == 0) ? 1 : 0;
				// Deeper than the skirts hanging there (5% of the wider patch's width,
				// LedgerPatchGenerator): the gaps that would still show as a crack.
				BeyondSkirt += Nearest > 0.05 * FMath::Max(Node.WorldSize, Across->WorldSize) ? 1 : 0;
				if (Nearest > Worst)
				{
					Worst = Nearest;
					WorstDepth = Node.Depth;
					WorstNeighbourDepth = Across->Depth;
					WorstNode = &Node;
					WorstAcross = Across;
					WorstEdge = E;
					// Up or sideways? A height mismatch is one kind of fault and a
					// patch in the wrong place is another.
					const FVector3d Centre = FVector3d(GetActorLocation());
					double NearestVertex = TNumericLimits<double>::Max();
					for (int32 J = 0; J < Side; ++J)
					{
						int32 QX = 0, QY = 0;
						EdgeVertex(Facing, J, QX, QY);
						const FVector3d Q = World(*Neighbour, QX, QY);
						if ((Q - P).SizeSquared() < NearestVertex)
						{
							NearestVertex = (Q - P).SizeSquared();
							WorstRadialMetres = ((P - Centre).Length() - (Q - Centre).Length()) / 100.0;
						}
					}
				}
			}
		}
	}

	OutReport = FString::Printf(
		TEXT("edge gaps (T049): %d drawn patches, %d edge vertices measured, "
			 "%d of them across a depth change of up to %d levels; %d skipped "
			 "(cube-face borders or no drawn neighbour), %d irregular sections\n"
			 "%d edge vertices over a patch still drawn behind its children (not adjacent, not measured)\n"
			 "worst gap %.2f cm, between depth %d and depth %d; %d vertices more than 1 cm off\n"),
		Drawn.Num(), Measured, MeasuredAcrossLevels, DeepestGapLevels, Skipped,
		Irregular, Overlapping, Worst, WorstDepth, WorstNeighbourDepth, OverOneCm);
	OutReport += FString::Printf(TEXT("%d of those between patches of the same depth; %d deeper than the skirt under them\n"),
		OverOneCmSameDepth, BeyondSkirt);
	if (WorstNode != nullptr && WorstAcross != nullptr)
	{
		OutReport += FString::Printf(
			TEXT("worst: face %d (%.6f, %.6f) x %.6f depth %d against face %d (%.6f, %.6f) x %.6f depth %d; "
				 "%.2f m of it is height\n"),
			static_cast<int32>(WorstNode->Face), WorstNode->U, WorstNode->V, WorstNode->Extent, WorstNode->Depth,
			static_cast<int32>(WorstAcross->Face), WorstAcross->U, WorstAcross->V, WorstAcross->Extent,
			WorstAcross->Depth, WorstRadialMetres);
	}
	// **Both sides of the worst edge, height by height.** A number for the
	// worst gap says there is one; the two rows of heights say what it is -- a
	// constant offset is a misplaced patch, a sawtooth is a stitch that did
	// not happen, and a smooth disagreement is two surfaces that were never
	// going to meet. Every eighth vertex, metres above the reference sphere.
	if (WorstNode != nullptr && WorstAcross != nullptr)
	{
		const FDrawn* Near = Drawn.Find(WorstNode);
		const FDrawn* Far = Drawn.Find(WorstAcross);
		if (Near != nullptr && Far != nullptr)
		{
			const FVector3d PlanetCentre = FVector3d(GetActorLocation());
			const EEdge Own = static_cast<EEdge>(WorstEdge);
			const EEdge Theirs = Opposite(Own);
			FString Mine;
			FString Other;
			for (int32 K = 0; K < Side; K += 8)
			{
				int32 X = 0, Y = 0;
				EdgeVertex(Own, K, X, Y);
				Mine += FString::Printf(TEXT(" %.1f"), ((World(*Near, X, Y) - PlanetCentre).Length() - Radius) / 100.0);
				EdgeVertex(Theirs, K, X, Y);
				Other += FString::Printf(TEXT(" %.1f"), ((World(*Far, X, Y) - PlanetCentre).Length() - Radius) / 100.0);
			}
			OutReport += FString::Printf(TEXT("edge heights, this side (every 8th):%s\n"), *Mine);
			OutReport += FString::Printf(TEXT("edge heights, facing side (every 8th):%s\n"), *Other);
		}
	}
	return Worst;
}
