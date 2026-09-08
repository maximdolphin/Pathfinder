#include "LedgerPatchGenerator.h"

#include "LedgerPlanet.h"
#include "LedgerTerrainMath.h"

namespace
{
	FColor Blend(const FColor& A, const FColor& B, double T)
	{
		const double Alpha = FMath::Clamp(T, 0.0, 1.0);
		return FColor(
			static_cast<uint8>(FMath::Lerp<double>(A.R, B.R, Alpha)),
			static_cast<uint8>(FMath::Lerp<double>(A.G, B.G, Alpha)),
			static_cast<uint8>(FMath::Lerp<double>(A.B, B.B, Alpha)),
			255);
	}

	/// Surface colour from height and steepness.
	///
	/// Deliberately dark: these are albedos, and albedo near 1.0 blows out the
	/// moment a sun hits it. Slope matters as much as height — a cliff is bare
	/// rock at any altitude, and keying colour on height alone gives the banded
	/// contour-map look that reads as procedural immediately.
	FColor SurfaceColour(double Elevation, double MaxElevation, double Steepness)
	{
		if (Elevation < 0.0)
		{
			const double Depth = FMath::Clamp(-Elevation / (MaxElevation * 0.45), 0.0, 1.0);
			return Blend(FColor(28, 62, 84), FColor(6, 16, 34), FMath::Pow(Depth, 0.6));
		}

		const double Height = FMath::Clamp(Elevation / MaxElevation, 0.0, 1.0);

		FColor Ground;
		if (Height < 0.015)
		{
			Ground = Blend(FColor(126, 116, 92), FColor(74, 88, 52), Height / 0.015);         // beach -> grass
		}
		else if (Height < 0.16)
		{
			Ground = Blend(FColor(96, 108, 66), FColor(76, 88, 54), (Height - 0.015) / 0.145); // grass -> forest
		}
		else if (Height < 0.42)
		{
			Ground = Blend(FColor(76, 88, 54), FColor(104, 94, 72), (Height - 0.16) / 0.26);  // forest -> scrub
		}
		else if (Height < 0.68)
		{
			Ground = Blend(FColor(84, 76, 60), FColor(96, 92, 88), (Height - 0.42) / 0.26);   // scrub -> rock
		}
		else
		{
			Ground = Blend(FColor(96, 92, 88), FColor(206, 210, 216), (Height - 0.68) / 0.32); // rock -> snow
		}

		// Steep ground is bare. Snow does not hold on a cliff either, so this is
		// applied last and wins.
		const double Bare = FMath::SmoothStep(0.45, 0.78, Steepness);
		return Blend(Ground, FColor(78, 72, 66), Bare);
	}
}

void LedgerGeneratePatch(FLedgerPatchJob& Job)
{
	const double Started = FPlatformTime::Seconds();

	const int32 Side = FMath::Max(3, Job.Side | 1); // force odd
	const int32 VertexCount = Side * Side;
	const double Inverse = 1.0 / static_cast<double>(Side - 1);

	Job.Vertices.SetNumUninitialized(VertexCount);
	Job.Normals.SetNumZeroed(VertexCount);
	Job.UVs.SetNumUninitialized(VertexCount);
	Job.MorphUVs.SetNumUninitialized(VertexCount);
	Job.Colors.SetNumUninitialized(VertexCount);
	Job.Tangents.SetNumUninitialized(VertexCount);

	// Elevation is kept alongside the positions so colouring can use it without
	// re-sampling the noise, which is the expensive part.
	TArray<double> Elevations;
	Elevations.SetNumUninitialized(VertexCount);

	for (int32 Y = 0; Y < Side; ++Y)
	{
		for (int32 X = 0; X < Side; ++X)
		{
			double LocalU = static_cast<double>(X) * Inverse;
			double LocalV = static_cast<double>(Y) * Inverse;

			// Collapse this vertex onto its even neighbour along a stitched
			// edge, so the finer mesh meets the coarser one exactly —
			// edge-index stitching, not skirts (§6.8). Skirts hide the crack
			// behind extra fill rate; this removes it.
			const bool bOddX = (X & 1) != 0;
			const bool bOddY = (Y & 1) != 0;
			if (X == 0 && Job.bStitchLeft && bOddY)
			{
				LocalV = static_cast<double>(Y - 1) * Inverse;
			}
			else if (X == Side - 1 && Job.bStitchRight && bOddY)
			{
				LocalV = static_cast<double>(Y - 1) * Inverse;
			}
			else if (Y == 0 && Job.bStitchBottom && bOddX)
			{
				LocalU = static_cast<double>(X - 1) * Inverse;
			}
			else if (Y == Side - 1 && Job.bStitchTop && bOddX)
			{
				LocalU = static_cast<double>(X - 1) * Inverse;
			}

			const double U = Job.U + LocalU * Job.Extent;
			const double V = Job.V + LocalV * Job.Extent;
			const FVector3d UnitSphere = LedgerTerrain::CubeToSphere(
				LedgerTerrain::FaceToCube(Job.Face, U, V));

			const double Elevation = LedgerTerrain::Elevation(UnitSphere, Job.Params);
			const FVector3d Surface = UnitSphere * (Job.Params.Radius + Elevation);

			const int32 Index = Y * Side + X;
			// Relative to the node centre: this is what keeps float precision
			// local, and it is why the same code works at planetary scale.
			Job.Vertices[Index] = FVector(Surface - Job.Centre);
			Job.UVs[Index] = FVector2D(LocalU, LocalV);
			Elevations[Index] = Elevation;
		}
	}

	// ---- geomorph targets ------------------------------------------------
	//
	// A vertex at an odd grid position does not exist in the parent LOD. When
	// this node collapses, that vertex vanishes and its neighbourhood snaps to
	// whatever the parent's coarser sampling says — which is the pop.
	//
	// So each vertex records where the parent *would* have put it: even ones do
	// not move, edge-odd ones sit at the midpoint of their two even neighbours,
	// and centre-odd ones at the average of the four surrounding evens. The
	// shader blends toward that as the node approaches its collapse threshold,
	// so by the moment it collapses the two meshes already agree.
	for (int32 Y = 0; Y < Side; ++Y)
	{
		for (int32 X = 0; X < Side; ++X)
		{
			const int32 Index = Y * Side + X;
			const bool bOddX = (X & 1) != 0;
			const bool bOddY = (Y & 1) != 0;

			double Coarse = Elevations[Index];
			if (bOddX && bOddY)
			{
				Coarse = 0.25 * (Elevations[(Y - 1) * Side + (X - 1)]
					+ Elevations[(Y - 1) * Side + (X + 1)]
					+ Elevations[(Y + 1) * Side + (X - 1)]
					+ Elevations[(Y + 1) * Side + (X + 1)]);
			}
			else if (bOddX)
			{
				Coarse = 0.5 * (Elevations[Y * Side + (X - 1)] + Elevations[Y * Side + (X + 1)]);
			}
			else if (bOddY)
			{
				Coarse = 0.5 * (Elevations[(Y - 1) * Side + X] + Elevations[(Y + 1) * Side + X]);
			}

			Job.MorphUVs[Index] = FVector2D(Coarse - Elevations[Index], Job.WorldSize);
		}
	}

	Job.Triangles.Reset((Side - 1) * (Side - 1) * 6);
	for (int32 Y = 0; Y < Side - 1; ++Y)
	{
		for (int32 X = 0; X < Side - 1; ++X)
		{
			const int32 A = Y * Side + X;
			const int32 B = A + 1;
			const int32 C = A + Side;
			const int32 D = C + 1;
			Job.Triangles.Add(A); Job.Triangles.Add(C); Job.Triangles.Add(B);
			Job.Triangles.Add(B); Job.Triangles.Add(C); Job.Triangles.Add(D);
		}
	}

	// Normals by accumulating face normals. Linear in triangle count, and far
	// cheaper than `CalculateTangentsForMesh`, which was most of the old
	// per-patch cost and produced no better result for an untextured surface.
	for (int32 Triangle = 0; Triangle < Job.Triangles.Num(); Triangle += 3)
	{
		const int32 I0 = Job.Triangles[Triangle];
		const int32 I1 = Job.Triangles[Triangle + 1];
		const int32 I2 = Job.Triangles[Triangle + 2];
		const FVector Edge1 = Job.Vertices[I1] - Job.Vertices[I0];
		const FVector Edge2 = Job.Vertices[I2] - Job.Vertices[I0];
		const FVector FaceNormal = FVector::CrossProduct(Edge2, Edge1);
		Job.Normals[I0] += FaceNormal;
		Job.Normals[I1] += FaceNormal;
		Job.Normals[I2] += FaceNormal;
	}

	// The node's own up vector, for the steepness term. Vertices are relative to
	// the centre, so the centre's direction is the local vertical.
	const FVector LocalUp = FVector(Job.Centre.GetSafeNormal());

	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		FVector Normal = Job.Normals[Index].GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			Normal = LocalUp;
		}
		Job.Normals[Index] = Normal;

		const double Steepness = 1.0 - FMath::Clamp(
			static_cast<double>(FVector::DotProduct(Normal, LocalUp)), 0.0, 1.0);
		Job.Colors[Index] = SurfaceColour(Elevations[Index], Job.Params.MaxElevation, Steepness);

		// A tangent perpendicular to the normal. Nothing samples a normal map
		// yet, but ProcMesh wants the channel and a degenerate basis shows up as
		// black shading the moment something does.
		const FVector Reference = FMath::Abs(Normal.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
		Job.Tangents[Index] = FProcMeshTangent(
			FVector::CrossProduct(Reference, Normal).GetSafeNormal(), false);
	}

	// ---- sea surface ----------------------------------------------------
	//
	// Sea level is exactly the reference sphere: `Elevation` returns negative
	// below the waterline, so the shell sits at `Radius` plus a couple of
	// centimetres to keep it off the terrain at the shoreline, where the two
	// meet at the same height by definition and would otherwise z-fight.
	constexpr double WaterLift = 3.0;

	bool bAnyWater = false;
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		if (Elevations[Index] < 0.0)
		{
			bAnyWater = true;
			break;
		}
	}

	if (bAnyWater)
	{
		Job.bHasWater = true;
		Job.WaterVertices.SetNumUninitialized(VertexCount);
		Job.WaterNormals.SetNumUninitialized(VertexCount);
		Job.WaterUVs.SetNumUninitialized(VertexCount);
		Job.WaterColors.SetNumUninitialized(VertexCount);
		Job.WaterTangents.SetNumUninitialized(VertexCount);

		const FVector LocalUpForWater = FVector(Job.Centre.GetSafeNormal());

		for (int32 Y = 0; Y < Side; ++Y)
		{
			for (int32 X = 0; X < Side; ++X)
			{
				const int32 Index = Y * Side + X;
				const double LocalU = static_cast<double>(X) * Inverse;
				const double LocalV = static_cast<double>(Y) * Inverse;
				const FVector3d UnitSphere = LedgerTerrain::CubeToSphere(
					LedgerTerrain::FaceToCube(
						Job.Face, Job.U + LocalU * Job.Extent, Job.V + LocalV * Job.Extent));

				const FVector3d Surface = UnitSphere * (Job.Params.Radius + WaterLift);
				Job.WaterVertices[Index] = FVector(Surface - Job.Centre);
				Job.WaterUVs[Index] = FVector2D(LocalU, LocalV);
				// The sea is a sphere, so its normal is the radial direction.
				Job.WaterNormals[Index] = FVector(UnitSphere);

				const FVector Reference = FMath::Abs(Job.WaterNormals[Index].Z) < 0.9f
					? FVector::UpVector : FVector::ForwardVector;
				Job.WaterTangents[Index] = FProcMeshTangent(
					FVector::CrossProduct(Reference, Job.WaterNormals[Index]).GetSafeNormal(), false);

				// Two different depth encodings, because the two consumers need
				// wildly different ranges.
				//
				// Alpha is depth over kilometres, for the colour gradient from
				// shallows to open ocean. Red is depth over the first three
				// metres, for waves and foam: on the alpha scale, standing in
				// knee-deep water reads as 0.02 and everything at the shoreline
				// is indistinguishable from everything else.
				const double Depth = FMath::Clamp(
					-Elevations[Index] / (Job.Params.MaxElevation * 0.30), 0.0, 1.0);
				const double ShoreProximity = 1.0 - FMath::Clamp(
					-Elevations[Index] / 300.0, 0.0, 1.0);

				Job.WaterColors[Index] = FColor(
					static_cast<uint8>(ShoreProximity * 255.0),
					255,
					255,
					static_cast<uint8>(FMath::Pow(Depth, 0.5) * 255.0));
			}
		}

		// Only emit a quad where some corner is actually underwater. Without
		// this the sea is a continuous sheet laid over the continents.
		Job.WaterTriangles.Reset();
		for (int32 Y = 0; Y < Side - 1; ++Y)
		{
			for (int32 X = 0; X < Side - 1; ++X)
			{
				const int32 A = Y * Side + X;
				const int32 B = A + 1;
				const int32 C = A + Side;
				const int32 D = C + 1;

				if (Elevations[A] >= 0.0 && Elevations[B] >= 0.0 &&
					Elevations[C] >= 0.0 && Elevations[D] >= 0.0)
				{
					continue;
				}

				Job.WaterTriangles.Add(A); Job.WaterTriangles.Add(C); Job.WaterTriangles.Add(B);
				Job.WaterTriangles.Add(B); Job.WaterTriangles.Add(C); Job.WaterTriangles.Add(D);
			}
		}

		Job.bHasWater = Job.WaterTriangles.Num() > 0;
	}

	Job.GenerationMs = (FPlatformTime::Seconds() - Started) * 1000.0;
	// Release: everything written above is visible to whoever sees this true.
	Job.bComplete.store(true, std::memory_order_release);
}
