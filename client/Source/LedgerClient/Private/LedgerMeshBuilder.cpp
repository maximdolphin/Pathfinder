#include "LedgerMeshBuilder.h"

namespace
{
	/// Appends one flat-shaded quad, `A B C D` counter-clockwise seen from
	/// outside.
	///
	/// The normal is `(B-A) x (C-A)`, not the reverse. With the reverse every
	/// face pointed *into* the solid: the triangles still rendered, because
	/// winding decides culling and not lighting, so the buildings and the ship
	/// came out uniformly black while the cones — which use a different vertex
	/// order — looked fine. A surface lit from behind is the classic symptom.
	void AddQuad(
		FLedgerMeshBuilder& Builder,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FColor& Colour)
	{
		const int32 Base = Builder.Vertices.Num();
		const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		const FVector Tangent = (B - A).GetSafeNormal();

		Builder.Vertices.Append({A, B, C, D});
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Builder.Normals.Add(Normal);
			Builder.Colors.Add(Colour);
			Builder.Tangents.Add(FProcMeshTangent(Tangent, false));
		}
		Builder.UVs.Append({
			FVector2D(0.0, 0.0), FVector2D(1.0, 0.0),
			FVector2D(1.0, 1.0), FVector2D(0.0, 1.0)});

		Builder.Triangles.Append({Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
	}

	/// Same convention as `AddQuad`: `A B C` counter-clockwise seen from outside,
	/// normal `(B-A) x (C-A)`. Both had to agree — with the two using opposite
	/// conventions, whichever one was wrong came back black, and fixing the boxes
	/// moved the problem to the cones.
	void AddTriangle(
		FLedgerMeshBuilder& Builder,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FColor& Colour)
	{
		const int32 Base = Builder.Vertices.Num();
		const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		const FVector Tangent = (B - A).GetSafeNormal();

		Builder.Vertices.Append({A, B, C});
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Builder.Normals.Add(Normal);
			Builder.Colors.Add(Colour);
			Builder.Tangents.Add(FProcMeshTangent(Tangent, false));
		}
		Builder.UVs.Append({FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.5, 1.0)});
		Builder.Triangles.Append({Base, Base + 2, Base + 1});
	}
}

void FLedgerMeshBuilder::AddTaperedBox(
	const FTransform& Transform,
	const FVector2D& NearExtent,
	const FVector2D& FarExtent,
	float Length,
	const FColor& Colour)
{
	// Local +X is the length axis; Y is width, Z is height.
	const float Half = Length * 0.5f;

	auto Point = [&Transform](float X, float Y, float Z)
	{
		return Transform.TransformPosition(FVector(X, Y, Z));
	};

	const FVector N0 = Point(-Half, -NearExtent.X, -NearExtent.Y);
	const FVector N1 = Point(-Half,  NearExtent.X, -NearExtent.Y);
	const FVector N2 = Point(-Half,  NearExtent.X,  NearExtent.Y);
	const FVector N3 = Point(-Half, -NearExtent.X,  NearExtent.Y);

	const FVector F0 = Point(Half, -FarExtent.X, -FarExtent.Y);
	const FVector F1 = Point(Half,  FarExtent.X, -FarExtent.Y);
	const FVector F2 = Point(Half,  FarExtent.X,  FarExtent.Y);
	const FVector F3 = Point(Half, -FarExtent.X,  FarExtent.Y);

	AddQuad(*this, F0, F1, F2, F3, Colour); // front
	AddQuad(*this, N1, N0, N3, N2, Colour); // back
	AddQuad(*this, N0, N1, F1, F0, Colour); // bottom
	AddQuad(*this, N2, N3, F3, F2, Colour); // top
	AddQuad(*this, N1, N2, F2, F1, Colour); // right
	AddQuad(*this, N3, N0, F0, F3, Colour); // left
}

void FLedgerMeshBuilder::AddBox(const FTransform& Transform, const FVector& Extent, const FColor& Colour)
{
	AddTaperedBox(
		Transform,
		FVector2D(Extent.Y, Extent.Z),
		FVector2D(Extent.Y, Extent.Z),
		Extent.X * 2.0f,
		Colour);
}

void FLedgerMeshBuilder::AddCylinder(
	const FTransform& Transform,
	float RadiusBottom,
	float RadiusTop,
	float Height,
	int32 Sides,
	const FColor& Colour)
{
	Sides = FMath::Max(3, Sides);

	for (int32 Side = 0; Side < Sides; ++Side)
	{
		const float A0 = (static_cast<float>(Side) / Sides) * 2.0f * PI;
		const float A1 = (static_cast<float>(Side + 1) / Sides) * 2.0f * PI;

		const FVector B0 = Transform.TransformPosition(
			FVector(FMath::Cos(A0) * RadiusBottom, FMath::Sin(A0) * RadiusBottom, 0.0f));
		const FVector B1 = Transform.TransformPosition(
			FVector(FMath::Cos(A1) * RadiusBottom, FMath::Sin(A1) * RadiusBottom, 0.0f));
		const FVector T0 = Transform.TransformPosition(
			FVector(FMath::Cos(A0) * RadiusTop, FMath::Sin(A0) * RadiusTop, Height));
		const FVector T1 = Transform.TransformPosition(
			FVector(FMath::Cos(A1) * RadiusTop, FMath::Sin(A1) * RadiusTop, Height));

		AddQuad(*this, B0, B1, T1, T0, Colour);
	}

	// Cap the top only. Trunks and nacelles sit on or in something, so the
	// bottom is never seen and the triangles would be wasted.
	const FVector Apex = Transform.TransformPosition(FVector(0.0f, 0.0f, Height));
	for (int32 Side = 0; Side < Sides; ++Side)
	{
		const float A0 = (static_cast<float>(Side) / Sides) * 2.0f * PI;
		const float A1 = (static_cast<float>(Side + 1) / Sides) * 2.0f * PI;
		const FVector T0 = Transform.TransformPosition(
			FVector(FMath::Cos(A0) * RadiusTop, FMath::Sin(A0) * RadiusTop, Height));
		const FVector T1 = Transform.TransformPosition(
			FVector(FMath::Cos(A1) * RadiusTop, FMath::Sin(A1) * RadiusTop, Height));
		AddTriangle(*this, Apex, T0, T1, Colour);
	}
}

void FLedgerMeshBuilder::AddCone(
	const FTransform& Transform,
	float Radius,
	float Height,
	int32 Sides,
	const FColor& Colour)
{
	Sides = FMath::Max(3, Sides);
	const FVector Apex = Transform.TransformPosition(FVector(0.0f, 0.0f, Height));

	for (int32 Side = 0; Side < Sides; ++Side)
	{
		const float A0 = (static_cast<float>(Side) / Sides) * 2.0f * PI;
		const float A1 = (static_cast<float>(Side + 1) / Sides) * 2.0f * PI;
		const FVector B0 = Transform.TransformPosition(
			FVector(FMath::Cos(A0) * Radius, FMath::Sin(A0) * Radius, 0.0f));
		const FVector B1 = Transform.TransformPosition(
			FVector(FMath::Cos(A1) * Radius, FMath::Sin(A1) * Radius, 0.0f));

		AddTriangle(*this, Apex, B0, B1, Colour);
		// Skirt underneath, so the canopy is not hollow when seen from below.
		AddTriangle(*this, Transform.TransformPosition(FVector::ZeroVector), B1, B0, Colour);
	}
}

void FLedgerMeshBuilder::AddCardTriangle(
	const FVector& A, const FVector& B, const FVector& C,
	const FVector& NormalA, const FVector& NormalB, const FVector& NormalC,
	const FColor& Colour)
{
	// Front and back as two triangles of their own, wound opposite ways, in
	// AddTriangle's convention -- indices Base, Base + 2, Base + 1 for A B C
	// counter-clockwise seen from the front.
	const FVector Tangent = (B - A).GetSafeNormal();
	const FVector Corners[2][3] = { { A, B, C }, { A, C, B } };
	const FVector Normals3[2][3] = { { NormalA, NormalB, NormalC }, { NormalA, NormalC, NormalB } };
	for (int32 Face = 0; Face < 2; ++Face)
	{
		const int32 Base = Vertices.Num();
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			Vertices.Add(Corners[Face][Corner]);
			Normals.Add(Normals3[Face][Corner].GetSafeNormal());
			Colors.Add(Colour);
			Tangents.Add(FProcMeshTangent(Tangent, false));
		}
		UVs.Append({ FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.5, 1.0) });
		Triangles.Append({ Base, Base + 2, Base + 1 });
	}
}

void FLedgerMeshBuilder::Reset()
{
	Vertices.Reset();
	Triangles.Reset();
	Normals.Reset();
	UVs.Reset();
	Colors.Reset();
	Tangents.Reset();
}

void FLedgerMeshBuilder::Upload(UProceduralMeshComponent* Mesh, int32 Section, bool bCreateCollision) const
{
	if (Mesh == nullptr || Vertices.Num() == 0)
	{
		return;
	}
	Mesh->CreateMeshSection(Section, Vertices, Triangles, Normals, UVs, Colors, Tangents, bCreateCollision);
}
