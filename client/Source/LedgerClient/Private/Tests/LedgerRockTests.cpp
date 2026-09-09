// Is the stone a stone? T434.
//
// The overhead capture showed the scatter as flat, screen-aligned rectangles,
// which is not what a 320-face noise-displaced sphere looks like from any
// angle. Either the geometry is degenerate or the instancing is, and looking
// harder at the photograph cannot tell those apart.

#include "LedgerMeshBuilder.h"
#include "LedgerRock.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerRockIsAStone,
	"Ledger.Rock.IsSolidAndRoughlyStoneShaped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerRockIsAStone::RunTest(const FString&)
{
	for (int32 Variant = 0; Variant < LedgerRock::Variants; ++Variant)
	{
		const FString Which = FString::Printf(TEXT("variant %d"), Variant);
		FLedgerMeshBuilder Builder;
		LedgerRock::Describe(Builder, 0x0C0B15u + Variant * 7919u,
			Variant / static_cast<double>(LedgerRock::Variants - 1));

		// 320 faces, flat shaded, so three vertices each and no sharing.
		TestEqual(*(Which + TEXT(": triangles")), Builder.Triangles.Num() / 3, 320);
		TestEqual(*(Which + TEXT(": vertices")), Builder.Vertices.Num(), 960);
		TestEqual(*(Which + TEXT(": one normal per vertex")),
			Builder.Normals.Num(), Builder.Vertices.Num());
		TestEqual(*(Which + TEXT(": one colour per vertex")),
			Builder.Colors.Num(), Builder.Vertices.Num());
		TestEqual(*(Which + TEXT(": one UV per vertex")),
			Builder.UVs.Num(), Builder.Vertices.Num());

		FBox Box(ForceInit);
		for (const FVector& Vertex : Builder.Vertices)
		{
			Box += Vertex;
		}
		const FVector Extent = Box.GetSize();

		// Roughly a metre across. Not exactly, because the noise decides.
		TestTrue(*(Which + TEXT(": about a metre wide")),
			Extent.X > 60.0 && Extent.X < 180.0);
		TestTrue(*(Which + TEXT(": about a metre deep")),
			Extent.Y > 60.0 && Extent.Y < 180.0);

		// **Solid, not flat.** This is the assertion the capture demanded: a
		// stone squashed to a card is exactly what a field of rectangles would
		// be. A stone at rest is wider than tall, so height is allowed to be
		// well under width -- but not under a third of it.
		TestTrue(*(Which + TEXT(": has height")), Extent.Z > 30.0);
		const double Flatness = Extent.Z / FMath::Max(Extent.X, Extent.Y);
		TestTrue(*(Which + TEXT(": is not a card")), Flatness > 0.33);

		// Sitting on the ground rather than half buried in it: the scatter puts
		// the instance origin on the surface.
		TestTrue(*(Which + TEXT(": base at zero")),
			FMath::Abs(Box.Min.Z) < 1.0);

		// Every face has area. A degenerate triangle is invisible on its own
		// and a whole mesh of them is the rectangle that started this.
		int32 Degenerate = 0;
		for (int32 Face = 0; Face < Builder.Triangles.Num() / 3; ++Face)
		{
			const FVector A = Builder.Vertices[Builder.Triangles[Face * 3 + 0]];
			const FVector B = Builder.Vertices[Builder.Triangles[Face * 3 + 1]];
			const FVector C = Builder.Vertices[Builder.Triangles[Face * 3 + 2]];
			if (FVector::CrossProduct(B - A, C - A).Size() < 0.01)
			{
				++Degenerate;
			}
		}
		TestEqual(*(Which + TEXT(": no degenerate faces")), Degenerate, 0);

		// Wound for Unreal, which is a different claim from the normals below
		// and is the one that was wrong.
		//
		// The icosahedron table is right-handed, so cross(B-A, C-A) points
		// outward for a counter-clockwise face -- and Unreal, being
		// left-handed, treats that as the back. The stones were built inside
		// out and this test passed anyway, because it only checked normals.
		// Here the geometric winding is required to be the opposite of the
		// stored normal, which is exactly what "front-facing in Unreal" means.
		int32 Frontfacing = 0;
		for (int32 Face = 0; Face < Builder.Triangles.Num() / 3; ++Face)
		{
			const FVector A = Builder.Vertices[Builder.Triangles[Face * 3 + 0]];
			const FVector B = Builder.Vertices[Builder.Triangles[Face * 3 + 1]];
			const FVector C = Builder.Vertices[Builder.Triangles[Face * 3 + 2]];
			const FVector Wound = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			if (FVector::DotProduct(Wound, Builder.Normals[Face * 3]) < 0.0)
			{
				++Frontfacing;
			}
		}
		TestEqual(*(Which + TEXT(": every face wound front-facing")),
			Frontfacing, Builder.Triangles.Num() / 3);

		// Outward-facing normals. Separate claim, and it was always true.
		const FVector Centre = Box.GetCenter();
		int32 Inward = 0;
		for (int32 Vertex = 0; Vertex < Builder.Vertices.Num(); ++Vertex)
		{
			const FVector Out = (Builder.Vertices[Vertex] - Centre).GetSafeNormal();
			if (FVector::DotProduct(Out, Builder.Normals[Vertex]) < 0.0)
			{
				++Inward;
			}
		}
		// Not zero: a genuinely concave face can point slightly inward of the
		// centre-to-vertex line. A handful is geometry; a third of them is a
		// winding bug.
		TestTrue(*(Which + TEXT(": faces point outward")),
			Inward < Builder.Vertices.Num() / 10);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
