// Procedural mesh assembly. Design §6.9: a parametric modular kit, not bespoke
// hulls — "change `hull_length` 24 m to 18 m, regenerate in seconds".
//
// This is the primitive layer that kit sits on: boxes and tapered boxes placed
// with a transform, accumulated into one vertex buffer. Deliberately not
// Geometry Script — §6.9 (amended v1.1) says that runs at editor time and bakes
// to `StaticMesh`, and these shapes are simple enough that a few hundred lines
// of buffer appending beats a dependency on the whole Dynamic Mesh stack.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

/// Accumulates geometry into the six parallel arrays `CreateMeshSection` wants.
struct LEDGERCLIENT_API FLedgerMeshBuilder
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	/// A box with independent half-extents at each end, so one call makes a
	/// cube, a wedge, a tapered fuselage or a fin. Flat-shaded: each face gets
	/// its own vertices, because a hull with shared normals reads as soap.
	void AddTaperedBox(
		const FTransform& Transform,
		const FVector2D& NearExtent,
		const FVector2D& FarExtent,
		float Length,
		const FColor& Colour);

	void AddBox(const FTransform& Transform, const FVector& Extent, const FColor& Colour);

	/// A cylinder along local +Z, `Sides` around. Used for tree trunks and
	/// engine nacelles.
	void AddCylinder(
		const FTransform& Transform,
		float RadiusBottom,
		float RadiusTop,
		float Height,
		int32 Sides,
		const FColor& Colour);

	/// A cone along local +Z. Tree canopies.
	void AddCone(
		const FTransform& Transform,
		float Radius,
		float Height,
		int32 Sides,
		const FColor& Colour);

	/// A triangle drawn from both sides with the normals given rather than its
	/// face's, so a flat card is lit as the volume it stands for -- the same
	/// on both faces, which a card seen from either side has to be. T058's
	/// impostor.
	void AddCardTriangle(
		const FVector& A, const FVector& B, const FVector& C,
		const FVector& NormalA, const FVector& NormalB, const FVector& NormalC,
		const FColor& Colour);

	void Reset();
	int32 VertexCount() const { return Vertices.Num(); }

	/// Uploads into a section, computing nothing further — normals and tangents
	/// were produced as the geometry was added.
	void Upload(UProceduralMeshComponent* Mesh, int32 Section, bool bCreateCollision) const;
};
