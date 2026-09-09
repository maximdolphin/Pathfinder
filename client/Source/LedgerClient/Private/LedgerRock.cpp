#include "LedgerRock.h"

#include "LedgerMeshBuilder.h"
#include "LedgerNoise.h"

namespace
{
	/// One metre across, which is what the scatter's size range is written
	/// against. Instances scale it from gravel to boulder.
	constexpr double BaseRadius = 50.0;

	/// Two subdivisions of an icosahedron: 320 faces. Enough for a silhouette
	/// that reads as stone rather than as a die, cheap enough that tens of
	/// thousands of them are still one instanced draw. Three would be 1,280 and
	/// buys nothing at the size these are drawn.
	constexpr int32 Subdivisions = 2;

	struct FTriangle
	{
		FVector3d A;
		FVector3d B;
		FVector3d C;
	};

	/// The twenty faces of an icosahedron, as unit vectors.
	void Icosahedron(TArray<FTriangle>& Out)
	{
		const double T = (1.0 + FMath::Sqrt(5.0)) * 0.5;
		const FVector3d V[12] = {
			FVector3d(-1,  T,  0), FVector3d( 1,  T,  0),
			FVector3d(-1, -T,  0), FVector3d( 1, -T,  0),
			FVector3d( 0, -1,  T), FVector3d( 0,  1,  T),
			FVector3d( 0, -1, -T), FVector3d( 0,  1, -T),
			FVector3d( T,  0, -1), FVector3d( T,  0,  1),
			FVector3d(-T,  0, -1), FVector3d(-T,  0,  1),
		};
		const int32 F[20][3] = {
			{0,11,5}, {0,5,1},  {0,1,7},   {0,7,10}, {0,10,11},
			{1,5,9},  {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
			{3,9,4},  {3,4,2},  {3,2,6},   {3,6,8},  {3,8,9},
			{4,9,5},  {2,4,11}, {6,2,10},  {8,6,7},  {9,8,1},
		};
		for (const auto& Face : F)
		{
			Out.Add({ V[Face[0]].GetSafeNormal(),
					  V[Face[1]].GetSafeNormal(),
					  V[Face[2]].GetSafeNormal() });
		}
	}

	/// Split every triangle into four, projected back onto the sphere.
	void Subdivide(TArray<FTriangle>& Faces)
	{
		TArray<FTriangle> Out;
		Out.Reserve(Faces.Num() * 4);
		for (const FTriangle& Face : Faces)
		{
			const FVector3d AB = ((Face.A + Face.B) * 0.5).GetSafeNormal();
			const FVector3d BC = ((Face.B + Face.C) * 0.5).GetSafeNormal();
			const FVector3d CA = ((Face.C + Face.A) * 0.5).GetSafeNormal();
			Out.Add({ Face.A, AB, CA });
			Out.Add({ AB, Face.B, BC });
			Out.Add({ CA, BC, Face.C });
			Out.Add({ AB, BC, CA });
		}
		Faces = MoveTemp(Out);
	}
}

namespace LedgerRock
{
	void Describe(FLedgerMeshBuilder& Builder, uint32 Seed, double Roundness)
	{
		TArray<FTriangle> Faces;
		Icosahedron(Faces);
		for (int32 Step = 0; Step < Subdivisions; ++Step)
		{
			Subdivide(Faces);
		}

		// How far a point on the unit sphere is pushed in or out.
		//
		// Two bands. The low one gives the stone its overall shape -- stones
		// are not spheres, they are lumps with a long axis -- and the high one
		// gives it corners. Roundness turns the high band down, which is the
		// difference between a fragment and a cobble.
		const double CornerAmplitude = FMath::Lerp(0.30, 0.05, Roundness);
		auto Displace = [Seed, CornerAmplitude](const FVector3d& Unit) -> FVector3d
		{
			const double Shape = LedgerNoise::Fractal(Unit * 1.3, Seed, 2);
			const double Corners = LedgerNoise::Fractal(Unit * 4.7, Seed ^ 0x9E3Du, 3);
			double Radius = BaseRadius * (1.0 + Shape * 0.34 + Corners * CornerAmplitude);

			// Squashed, because a stone at rest is wider than it is tall. Done
			// on the radius rather than by scaling the mesh so the noise is not
			// stretched with it.
			Radius *= FMath::Lerp(1.0, 0.72, FMath::Abs(Unit.Z));
			return Unit * Radius;
		};

		// Flat shading: every face gets its own three vertices and one normal.
		//
		// Deliberate, and the whole reason these read as stone. Smooth normals
		// over a noise-displaced sphere give a soft blob that looks like a
		// potato; a face normal per face gives the flat planes and hard edges
		// that a broken rock actually has, and at 320 faces there are enough of
		// them to be facets rather than a shape.
		const int32 First = Builder.Vertices.Num();
		Builder.Vertices.Reserve(First + Faces.Num() * 3);

		double Lowest = 0.0;
		TArray<FVector3d> Positions;
		Positions.Reserve(Faces.Num() * 3);
		for (const FTriangle& Face : Faces)
		{
			for (const FVector3d& Unit : { Face.A, Face.B, Face.C })
			{
				const FVector3d Point = Displace(Unit);
				Lowest = FMath::Min(Lowest, Point.Z);
				Positions.Add(Point);
			}
		}

		// Sat on the ground rather than buried to its centre. The scatter puts
		// the origin on the surface, so a stone whose lowest point is below
		// zero is a stone half underground.
		const double Lift = -Lowest;

		for (int32 Face = 0; Face < Faces.Num(); ++Face)
		{
			const FVector3d A = Positions[Face * 3 + 0];
			const FVector3d B = Positions[Face * 3 + 1];
			const FVector3d C = Positions[Face * 3 + 2];
			const FVector3d Normal =
				FVector3d::CrossProduct(B - A, C - A).GetSafeNormal();

			// Vertex colour varies a little face to face. The material
			// multiplies by it, so this is what stops a field of stones being
			// one flat grey -- and it costs nothing, unlike a second material.
			const double Shade = 0.82 + 0.18 * LedgerNoise::Fractal(
				A.GetSafeNormal() * 6.1, Seed ^ 0x51EDu, 1);
			const FColor Colour(
				static_cast<uint8>(FMath::Clamp(Shade * 168.0, 0.0, 255.0)),
				static_cast<uint8>(FMath::Clamp(Shade * 160.0, 0.0, 255.0)),
				static_cast<uint8>(FMath::Clamp(Shade * 148.0, 0.0, 255.0)),
				255);

			for (const FVector3d& Point : { A, B, C })
			{
				Builder.Vertices.Add(FVector(Point.X, Point.Y, Point.Z + Lift));
				Builder.Normals.Add(FVector(Normal));
				Builder.Colors.Add(Colour);
				Builder.Tangents.Add(FProcMeshTangent(
					FVector(FVector3d::CrossProduct(
						FVector3d(0, 0, 1), Normal).GetSafeNormal()), false));

				// Planar UVs from the two axes least aligned with the face
				// normal. Nothing samples them -- the material is triplanar and
				// world-space -- but a static mesh without UVs fails to build a
				// lightmap and warns about it on every load.
				Builder.UVs.Add(FVector2D(
					(Point.X / BaseRadius) * 0.5 + 0.5,
					(Point.Y / BaseRadius) * 0.5 + 0.5));
			}

			// Wound backwards, deliberately.
			//
			// The icosahedron above is the standard right-handed table, whose
			// faces are counter-clockwise seen from outside. Unreal is
			// left-handed and takes the opposite winding as front-facing, so
			// the stones were built inside out: from eye level they were
			// invisible, and from directly above they rendered as the flat
			// insides of their own far sides -- a field of pale, screen-aligned
			// rectangles, which is what the overhead capture actually was.
			//
			// The vertex normals were outward the whole time, which is why the
			// geometry test passed while nothing looked right. A normal and a
			// winding are different claims.
			Builder.Triangles.Add(First + Face * 3 + 0);
			Builder.Triangles.Add(First + Face * 3 + 2);
			Builder.Triangles.Add(First + Face * 3 + 1);
		}
	}
}
