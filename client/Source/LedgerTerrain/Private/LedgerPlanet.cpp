#include "LedgerPlanet.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LedgerLog.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace
{
	/// A stable key for a node: face, depth, and grid position within the face.
	/// Packs into 64 bits up to depth 24, well past `MaxDepth`.
	uint64 NodeKey(const FLedgerQuadNode& Node)
	{
		const uint64 Cells = 1ull << Node.Depth;
		const uint64 X = static_cast<uint64>(FMath::Clamp(Node.U * static_cast<double>(Cells), 0.0, static_cast<double>(Cells - 1)));
		const uint64 Y = static_cast<uint64>(FMath::Clamp(Node.V * static_cast<double>(Cells), 0.0, static_cast<double>(Cells - 1)));
		return (static_cast<uint64>(Node.Face) << 58)
			| (static_cast<uint64>(Node.Depth) << 52)
			| (X << 26)
			| Y;
	}

	/// Which cube face a direction points at, and where on it.
	void DirectionToFace(const FVector3d& Direction, ELedgerCubeFace& OutFace, double& OutU, double& OutV)
	{
		const double Ax = FMath::Abs(Direction.X);
		const double Ay = FMath::Abs(Direction.Y);
		const double Az = FMath::Abs(Direction.Z);

		double A = 0.0;
		double B = 0.0;

		if (Ax >= Ay && Ax >= Az)
		{
			if (Direction.X > 0.0)
			{
				OutFace = ELedgerCubeFace::PositiveX;
				A = Direction.Y / Ax;
				B = Direction.Z / Ax;
			}
			else
			{
				OutFace = ELedgerCubeFace::NegativeX;
				A = -Direction.Y / Ax;
				B = Direction.Z / Ax;
			}
		}
		else if (Ay >= Az)
		{
			if (Direction.Y > 0.0)
			{
				OutFace = ELedgerCubeFace::PositiveY;
				A = -Direction.X / Ay;
				B = Direction.Z / Ay;
			}
			else
			{
				OutFace = ELedgerCubeFace::NegativeY;
				A = Direction.X / Ay;
				B = Direction.Z / Ay;
			}
		}
		else
		{
			if (Direction.Z > 0.0)
			{
				OutFace = ELedgerCubeFace::PositiveZ;
				A = Direction.X / Az;
				B = Direction.Y / Az;
			}
			else
			{
				OutFace = ELedgerCubeFace::NegativeZ;
				A = Direction.X / Az;
				B = -Direction.Y / Az;
			}
		}

		OutU = FMath::Clamp((A + 1.0) * 0.5, 0.0, 1.0);
		OutV = FMath::Clamp((B + 1.0) * 0.5, 0.0, 1.0);
	}

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

// ---------------------------------------------------------------- generation

void LedgerGeneratePatch(FLedgerPatchJob& Job)
{
	const double Started = FPlatformTime::Seconds();

	const int32 Side = FMath::Max(3, Job.Side | 1); // force odd
	const int32 VertexCount = Side * Side;
	const double Inverse = 1.0 / static_cast<double>(Side - 1);

	Job.Vertices.SetNumUninitialized(VertexCount);
	Job.Normals.SetNumZeroed(VertexCount);
	Job.UVs.SetNumUninitialized(VertexCount);
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

// ---------------------------------------------------------------- actor

ALedgerPlanet::ALedgerPlanet()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

FLedgerTerrainParams ALedgerPlanet::TerrainParams() const
{
	FLedgerTerrainParams Params;
	Params.Seed = static_cast<uint32>(Seed);
	Params.Radius = Radius;
	Params.MaxElevation = MaxElevation;
	Params.SeaLevel = SeaLevel;
	return Params;
}

void ALedgerPlanet::BeginPlay()
{
	Super::BeginPlay();

	// Materials arrive from whoever spawned this actor rather than being built
	// here. That is one line of wiring at the composition root against a module
	// dependency for the whole terrain — and it is the right way round anyway:
	// a quadtree's business is geometry, and what it is painted with is not its
	// decision to make.
	if (SurfaceMaterial == nullptr)
	{
		SurfaceMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly"));
		UE_LOG(LogLedger, Warning,
			TEXT("planet spawned with no surface material; falling back to vertex colour"));
	}

	UE_LOG(LogLedger, Log, TEXT("materials: terrain %s, water %s"),
		SurfaceMaterial != nullptr ? *SurfaceMaterial->GetName() : TEXT("<none>"),
		WaterMaterial != nullptr ? *WaterMaterial->GetName() : TEXT("<none>"));

	BuildRoots();

	// Sized against the measured visible-leaf count with headroom for a fast
	// turn. Undersizing does not degrade gracefully on its own — the parent-hold
	// rule in `UpdateTree` is what stops a starved pool from punching holes.
	constexpr int32 PoolSize = 3600;
	MeshPool.Reserve(PoolSize);
	FreeSections.Reserve(PoolSize);
	for (int32 Index = 0; Index < PoolSize; ++Index)
	{
		SectionMeta.AddDefaulted();
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
		Mesh->SetupAttachment(Root);
		Mesh->RegisterComponent();
		// The one setting that decides whether collision hitches (§6.8).
		Mesh->bUseAsyncCooking = true;
		Mesh->SetCastShadow(true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetVisibility(false);
		MeshPool.Add(Mesh);
		FreeSections.Add(Index);
	}

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Ledger.Terrain.ResetPeaks"),
		TEXT("Clear the worst-case counters, so the next phase is measured on its own."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]
		{
			ALedgerPlanet* Self = const_cast<ALedgerPlanet*>(this);
			Self->Stats.WorstUnfilled = 0;
			Self->Stats.WorstUnfilledAt = 0.0;
			Self->Stats.WorstFrameUploadMs = 0.0;
			Self->Stats.WorstFrameCollisionMs = 0.0;
		}),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Ledger.Terrain.Stats"),
		TEXT("Report terrain LOD, generation and collision timings."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this] { LogStats(); }),
		ECVF_Default));

	UE_LOG(LogLedger, Log,
		TEXT("planet ready: radius %.0f km, relief %.1f km, sea level %.2f, max depth %d (~%.1f m quads)"),
		Radius / 100000.0,
		MaxElevation / 100000.0,
		SeaLevel,
		MaxDepth,
		(Radius * PI * 0.5) / FMath::Pow(2.0, static_cast<double>(MaxDepth)) / (GridResolution - 1) / 100.0);
}

void ALedgerPlanet::EndPlay(const EEndPlayReason::Type Reason)
{
	// Jobs hold a shared reference to their own state, so they finish harmlessly
	// after teardown — but nothing should be waiting on them.
	for (TPair<uint64, FLedgerPatchJobRef>& Entry : InFlight)
	{
		if (Entry.Value.IsValid())
		{
			Entry.Value->bAbandoned.store(true, std::memory_order_relaxed);
		}
	}
	InFlight.Empty();
	PatchCache.Empty();
	PatchCacheBytes = 0;

	for (IConsoleObject* Command : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Command);
	}
	ConsoleCommands.Empty();

	Super::EndPlay(Reason);
}

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
	// altitude over a mountain the difference is the whole LOD decision.
	const FVector3d NodeDirection = Node.Centre.GetSafeNormal();
	const FVector3d NodeSurface = NodeDirection * SurfaceRadiusAt(NodeDirection);

	// The nearer of where the camera is and where it will shortly be. Taking the
	// minimum rather than replacing one with the other matters: leading alone
	// would collapse the ground behind a fast mover, and the ground behind is
	// still on screen.
	const double Distance = FMath::Min(
		FVector3d::Distance(CameraLocal, NodeSurface),
		FVector3d::Distance(LeadLocal, NodeSurface));

	const double Error = LedgerTerrain::ScreenSpaceError(
		Node.WorldSize, Distance, ViewportWidth, FovRadians);

	if (!bForceCollapse && Error > ErrorThresholdPixels && Node.Depth < MaxDepth)
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
		if (bAllChildrenReady)
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

void ALedgerPlanet::CollectLeaves(const FLedgerQuadNode& Node, TArray<const FLedgerQuadNode*>& Out, bool bAncestorHasGeometry) const
{
	if (!Node.bVisible)
	{
		return;
	}

	const bool bHasGeometry = ActiveSections.Contains(NodeKey(Node));

	if (Node.IsLeaf())
	{
		Out.Add(&Node);
		if (!bHasGeometry && !bAncestorHasGeometry)
		{
			// Nothing anywhere up the chain is drawing this ground. Not a
			// coarse patch standing in for a fine one — a hole.
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

	if (bHasGeometry || bCollapseFront)
	{
		Out.Add(&Node);
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Node.Children[Index].IsValid())
		{
			CollectLeaves(*Node.Children[Index], Out, bAncestorHasGeometry || bHasGeometry);
		}
	}
}

int32 ALedgerPlanet::LeafDepthAt(const FVector3d& UnitDirection) const
{
	ELedgerCubeFace Face = ELedgerCubeFace::PositiveX;
	double U = 0.0;
	double V = 0.0;
	DirectionToFace(UnitDirection, Face, U, V);

	const FLedgerQuadNode* Node = Roots.IsValidIndex(static_cast<int32>(Face))
		? Roots[static_cast<int32>(Face)].Get()
		: nullptr;
	if (Node == nullptr)
	{
		return 0;
	}

	while (Node->bHasChildren)
	{
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

bool ALedgerPlanet::LaunchPatch(const FLedgerQuadNode& Node, bool bWithCollision)
{
	if (FreeSections.Num() == 0 || InFlight.Num() >= MaxJobsInFlight)
	{
		return false;
	}

	FLedgerPatchJobRef Job = MakeShared<FLedgerPatchJob, ESPMode::ThreadSafe>();
	Job->Key = NodeKey(Node);
	++Stats.CacheMisses;
	Job->SectionIndex = FreeSections.Pop();
	Job->bWithCollision = bWithCollision;
	Job->Face = Node.Face;
	Job->U = Node.U;
	Job->V = Node.V;
	Job->Extent = Node.Extent;
	Job->Centre = Node.Centre;
	Job->Params = TerrainParams();
	Job->Side = GridResolution;

	// Neighbour depths are read here, on the game thread, while the tree is
	// stable. The worker never touches the tree.
	Job->bStitchLeft = LeafDepthAt(UnitSphereAt(Node, -0.02, 0.5)) < Node.Depth;
	Job->bStitchRight = LeafDepthAt(UnitSphereAt(Node, 1.02, 0.5)) < Node.Depth;
	Job->bStitchBottom = LeafDepthAt(UnitSphereAt(Node, 0.5, -0.02)) < Node.Depth;
	Job->bStitchTop = LeafDepthAt(UnitSphereAt(Node, 0.5, 1.02)) < Node.Depth;

	InFlight.Add(Job->Key, Job);

	// The lambda captures the shared reference, so the job outlives the actor if
	// it has to. Nothing inside it touches `this`.
	Async(EAsyncExecution::ThreadPool, [Job]()
	{
		LedgerGeneratePatch(*Job);
	});

	return true;
}

void ALedgerPlanet::HarvestCompletedPatches()
{
	const double Started = FPlatformTime::Seconds();

	TArray<uint64> Landed;
	Landed.Reserve(InFlight.Num());

	for (TPair<uint64, FLedgerPatchJobRef>& Entry : InFlight)
	{
		FLedgerPatchJob* Job = Entry.Value.Get();
		if (Job == nullptr || !Job->bComplete.load(std::memory_order_acquire))
		{
			continue;
		}

		Landed.Add(Entry.Key);

		if (Job->bAbandoned.load(std::memory_order_relaxed))
		{
			FreeSections.Add(Job->SectionIndex);
			continue;
		}

		UProceduralMeshComponent* Mesh = MeshPool[Job->SectionIndex];
		Mesh->SetWorldLocation(GetActorLocation() + FVector(Job->Centre));
		Mesh->ClearMeshSection(0);

		const double CollisionStart = FPlatformTime::Seconds();
		Mesh->CreateMeshSection(
			0, Job->Vertices, Job->Triangles, Job->Normals, Job->UVs,
			Job->Colors, Job->Tangents, Job->bWithCollision);
		if (Job->bWithCollision)
		{
			Stats.WorstFrameCollisionMs = FMath::Max(
				Stats.WorstFrameCollisionMs, (FPlatformTime::Seconds() - CollisionStart) * 1000.0);
		}

		Mesh->SetCollisionEnabled(Job->bWithCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
		if (SurfaceMaterial != nullptr)
		{
			Mesh->SetMaterial(0, SurfaceMaterial);
		}

		// Section 1 is the sea. Same component, so it moves and culls with the
		// land it belongs to and costs no extra transform.
		Mesh->ClearMeshSection(1);
		if (Job->bHasWater)
		{
			++Stats.WaterSections;
			Mesh->CreateMeshSection(
				1, Job->WaterVertices, Job->WaterTriangles, Job->WaterNormals,
				Job->WaterUVs, Job->WaterColors, Job->WaterTangents, /*bCreateCollision*/ false);
			if (WaterMaterial != nullptr)
			{
				Mesh->SetMaterial(1, WaterMaterial);
			}
		}

		Mesh->SetVisibility(true);

		ActiveSections.Add(Job->Key, Job->SectionIndex);
		SectionMeta[Job->SectionIndex] = FLedgerSectionMeta{
			Job->Key, Job->Centre,
			Job->bStitchLeft, Job->bStitchRight, Job->bStitchBottom, Job->bStitchTop };

		Stats.LastPatchGenerationMs = Job->GenerationMs;
		++Stats.TotalBuilds;

		if ((FPlatformTime::Seconds() - Started) * 1000.0 >= UploadBudgetMs)
		{
			break;
		}
	}

	for (const uint64 Key : Landed)
	{
		InFlight.Remove(Key);
	}

	Stats.LastFrameUploadMs = (FPlatformTime::Seconds() - Started) * 1000.0;
	Stats.WorstFrameUploadMs = FMath::Max(Stats.WorstFrameUploadMs, Stats.LastFrameUploadMs);
}

int64 FLedgerCachedPatch::Bytes() const
{
	// GetAllocatedSize rather than Num() times sizeof: TArray over-allocates on
	// growth, and a budget measured against the useful part of an allocation
	// overshoots by whatever the slack happens to be.
	return Land.ProcVertexBuffer.GetAllocatedSize() + Land.ProcIndexBuffer.GetAllocatedSize()
		+ Water.ProcVertexBuffer.GetAllocatedSize() + Water.ProcIndexBuffer.GetAllocatedSize();
}

bool ALedgerPlanet::UploadFromCache(const FLedgerQuadNode& Node, bool bWithCollision)
{
	if (FreeSections.Num() == 0)
	{
		return false;
	}

	const uint64 Key = NodeKey(Node);
	TSharedPtr<FLedgerCachedPatch>* Found = PatchCache.Find(Key);
	if (Found == nullptr || !Found->IsValid())
	{
		return false;
	}

	// Neighbour depths, read the same way LaunchPatch reads them. A patch whose
	// coarser neighbour has subdivided since needs different edge geometry, and
	// the cached buffers would leave a crack along that edge.
	FLedgerCachedPatch& Entry = **Found;
	if (Entry.bStitchLeft != (LeafDepthAt(UnitSphereAt(Node, -0.02, 0.5)) < Node.Depth)
		|| Entry.bStitchRight != (LeafDepthAt(UnitSphereAt(Node, 1.02, 0.5)) < Node.Depth)
		|| Entry.bStitchBottom != (LeafDepthAt(UnitSphereAt(Node, 0.5, -0.02)) < Node.Depth)
		|| Entry.bStitchTop != (LeafDepthAt(UnitSphereAt(Node, 0.5, 1.02)) < Node.Depth))
	{
		PatchCacheBytes -= Entry.Bytes();
		PatchCache.Remove(Key);
		Stats.CacheEntries = PatchCache.Num();
		return false;
	}

	const int32 SectionIndex = FreeSections.Pop();
	UProceduralMeshComponent* Mesh = MeshPool[SectionIndex];
	Mesh->SetWorldLocation(GetActorLocation() + FVector(Entry.Centre));

	Entry.Land.bEnableCollision = bWithCollision;
	Mesh->SetProcMeshSection(0, Entry.Land);
	if (SurfaceMaterial != nullptr)
	{
		Mesh->SetMaterial(0, SurfaceMaterial);
	}

	if (Entry.bHasWater)
	{
		++Stats.WaterSections;
		Mesh->SetProcMeshSection(1, Entry.Water);
		if (WaterMaterial != nullptr)
		{
			Mesh->SetMaterial(1, WaterMaterial);
		}
	}
	else
	{
		Mesh->ClearMeshSection(1);
	}

	Mesh->SetCollisionEnabled(bWithCollision
		? ECollisionEnabled::QueryAndPhysics
		: ECollisionEnabled::NoCollision);
	Mesh->SetVisibility(true);

	ActiveSections.Add(Key, SectionIndex);
	SectionMeta[SectionIndex] = FLedgerSectionMeta{
		Key, Entry.Centre,
		Entry.bStitchLeft, Entry.bStitchRight, Entry.bStitchBottom, Entry.bStitchTop };

	// Removed, not kept: the geometry is on screen again and holding a second
	// copy of it is exactly the waste this cache is shaped to avoid.
	PatchCacheBytes -= Entry.Bytes();
	PatchCache.Remove(Key);

	Stats.CacheEntries = PatchCache.Num();
	Stats.CacheMegabytes = static_cast<double>(PatchCacheBytes) / (1024.0 * 1024.0);
	++Stats.CacheHits;
	return true;
}

void ALedgerPlanet::CachePatch(TSharedPtr<FLedgerCachedPatch> Entry, uint64 Key)
{
	if (!Entry.IsValid() || PatchCacheBudgetMB <= 0.0)
	{
		return;
	}

	Entry->LastUsed = ++CacheClock;

	if (const TSharedPtr<FLedgerCachedPatch>* Existing = PatchCache.Find(Key))
	{
		if (Existing->IsValid())
		{
			PatchCacheBytes -= (*Existing)->Bytes();
		}
	}

	PatchCacheBytes += Entry->Bytes();
	PatchCache.Add(Key, Entry);

	// ponytail: linear scan for the least-recently-used entry. Eviction only
	// runs once the budget is already exceeded, over a few hundred entries, so
	// it is microseconds — swap in an intrusive list if it ever shows up in a
	// profile.
	const int64 Budget = static_cast<int64>(PatchCacheBudgetMB * 1024.0 * 1024.0);
	while (PatchCacheBytes > Budget && PatchCache.Num() > 1)
	{
		uint64 Oldest = 0;
		uint64 OldestUse = MAX_uint64;
		for (const TPair<uint64, TSharedPtr<FLedgerCachedPatch>>& Candidate : PatchCache)
		{
			if (Candidate.Value.IsValid() && Candidate.Value->LastUsed < OldestUse)
			{
				OldestUse = Candidate.Value->LastUsed;
				Oldest = Candidate.Key;
			}
		}
		if (OldestUse == MAX_uint64)
		{
			break;
		}
		PatchCacheBytes -= PatchCache[Oldest]->Bytes();
		PatchCache.Remove(Oldest);
		++Stats.CacheEvictions;
	}

	Stats.CacheEntries = PatchCache.Num();
	Stats.CacheMegabytes = static_cast<double>(PatchCacheBytes) / (1024.0 * 1024.0);
}

void ALedgerPlanet::ReleaseSection(uint64 Key)
{
	int32 SectionIndex = INDEX_NONE;
	if (!ActiveSections.RemoveAndCopyValue(Key, SectionIndex))
	{
		return;
	}
	if (MeshPool.IsValidIndex(SectionIndex))
	{
		UProceduralMeshComponent* Mesh = MeshPool[SectionIndex];

		// Take the geometry on the way out. This is the only moment it can be
		// taken: after ClearMeshSection it is gone, and before release it is
		// still on screen and not worth a second copy.
		if (const FProcMeshSection* Land = Mesh->GetProcMeshSection(0))
		{
			TSharedPtr<FLedgerCachedPatch> Entry = MakeShared<FLedgerCachedPatch>();
			const FLedgerSectionMeta& Meta = SectionMeta[SectionIndex];
			Entry->Centre = Meta.Centre;
			Entry->bStitchLeft = Meta.bStitchLeft;
			Entry->bStitchRight = Meta.bStitchRight;
			Entry->bStitchBottom = Meta.bStitchBottom;
			Entry->bStitchTop = Meta.bStitchTop;
			Entry->Land = *Land;

			if (const FProcMeshSection* Water = Mesh->GetProcMeshSection(1))
			{
				Entry->Water = *Water;
				Entry->bHasWater = Water->ProcIndexBuffer.Num() > 0;
			}

			CachePatch(Entry, Key);
		}

		Mesh->ClearMeshSection(0);
		Mesh->ClearMeshSection(1);
		Mesh->SetVisibility(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	FreeSections.Add(SectionIndex);
}

void ALedgerPlanet::AbandonJob(uint64 Key)
{
	if (const FLedgerPatchJobRef* Job = InFlight.Find(Key))
	{
		if (Job->IsValid())
		{
			// The worker may still be mid-generation; it reads this only after
			// finishing, so the result is discarded rather than interrupted.
			(*Job)->bAbandoned.store(true, std::memory_order_relaxed);
		}
	}
}

void ALedgerPlanet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FVector CameraWorld = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	double Fov = FMath::DegreesToRadians(90.0);
	double ViewportWidth = 1920.0;

	if (const APlayerController* Controller = World->GetFirstPlayerController())
	{
		Controller->GetPlayerViewPoint(CameraWorld, CameraRotation);
		if (Controller->PlayerCameraManager != nullptr)
		{
			Fov = FMath::DegreesToRadians(Controller->PlayerCameraManager->GetFOVAngle());
		}
		int32 SizeX = 0;
		int32 SizeY = 0;
		Controller->GetViewportSize(SizeX, SizeY);
		if (SizeX > 0)
		{
			ViewportWidth = static_cast<double>(SizeX);
		}
	}

	const FVector3d CameraLocal = FVector3d(CameraWorld - GetActorLocation());

	// Velocity is what makes the collision cook *predictive* rather than
	// reactive. §6.8: cook ahead along the velocity vector, never on demand.
	if (DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		CameraVelocityLocal = (CameraLocal - LastCameraLocal) / static_cast<double>(DeltaSeconds);
	}
	LastCameraLocal = CameraLocal;

	// Where the camera will be in a few seconds, for the LOD to aim at.
	//
	// Clamped twice, and both clamps were learned the hard way.
	//
	// Against altitude, because a lead point below the surface asks for detail
	// underground. Against an absolute distance, because scaling the lead with
	// altitude means that at 300 km up, three seconds of climb is a lead point
	// 150 km away — and the LOD then subdivides a region nobody is flying to,
	// at a depth chosen by proximity to a point in empty space. That produced
	// fifteen hundred patches of demand that never fell, on a pool of 3,600,
	// growing for as long as the climb continued.
	//
	// Prefetch earns its keep near the ground, where a patch is metres across
	// and arriving before it exists is the whole problem. Five kilometres is
	// sixteen seconds at cruise and more than the deepest LOD ever needs.
	constexpr double MaxLeadDistance = 500000.0;   // 5 km
	const double AltitudeAbove = FMath::Max(
		1.0, CameraLocal.Length() - SurfaceRadiusAt(CameraLocal.GetSafeNormal()));

	FVector3d LeadOffset = CameraVelocityLocal * GeometryLeadSeconds;
	const double LeadLength = LeadOffset.Length();
	const double LeadLimit = FMath::Min(MaxLeadDistance, AltitudeAbove * 0.5);
	if (LeadLength > LeadLimit)
	{
		LeadOffset *= LeadLimit / LeadLength;
	}
	const FVector3d GeometryLead = CameraLocal + LeadOffset;

	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		UpdateTree(*RootNode, CameraLocal, GeometryLead, ViewportWidth, Fov, false);
	}

	HarvestCompletedPatches();

	TArray<const FLedgerQuadNode*> Leaves;
	Leaves.Reserve(2048);
	Stats.UnfilledNodes = 0;
	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		CollectLeaves(*RootNode, Leaves, /*bAncestorHasGeometry*/ false);
	}

	Stats.VisibleNodes = Leaves.Num();
	if (Stats.UnfilledNodes > Stats.WorstUnfilled)
	{
		Stats.WorstUnfilled = Stats.UnfilledNodes;
		Stats.WorstUnfilledAt = World->GetTimeSeconds();
	}
	Stats.DeepestVisibleDepth = 0;

	// Where the camera will be shortly, so the cook has landed by the time we
	// arrive rather than starting when we get there.
	const FVector3d PredictedLocal = CameraLocal + CameraVelocityLocal * CollisionLeadSeconds;

	// Nearest first: if the queue runs out, it runs out on the far nodes.
	Leaves.Sort([&CameraLocal](const FLedgerQuadNode& A, const FLedgerQuadNode& B)
	{
		return FVector3d::DistSquared(A.Centre, CameraLocal) < FVector3d::DistSquared(B.Centre, CameraLocal);
	});

	int32 Pending = 0;
	int32 WithCollision = 0;

	// A cache hit is cheap next to generating a patch and expensive next to
	// doing nothing: SetProcMeshSection still rebuilds render resources on the
	// game thread. Unbudgeted, a turn that brings several hundred cached
	// patches back into view would serve all of them in one frame, and the
	// cache would have turned a smooth stream of work into a single stall.
	const double CacheUploadStart = FPlatformTime::Seconds();
	const double CacheUploadBudgetMs = FMath::Max(0.0, UploadBudgetMs - Stats.LastFrameUploadMs);

	for (const FLedgerQuadNode* Leaf : Leaves)
	{
		Stats.DeepestVisibleDepth = FMath::Max(Stats.DeepestVisibleDepth, Leaf->Depth);

		const uint64 Key = NodeKey(*Leaf);
		const double NearNow = FVector3d::Distance(Leaf->Centre, CameraLocal);
		const double NearSoon = FVector3d::Distance(Leaf->Centre, PredictedLocal);
		const bool bWantsCollision = FMath::Min(NearNow, NearSoon) < CollisionRadius;
		if (bWantsCollision)
		{
			++WithCollision;
		}

		if (ActiveSections.Contains(Key) || InFlight.Contains(Key))
		{
			continue;
		}

		// Cache first. A hit costs an upload; a miss costs a worker thread and
		// several milliseconds of sampling.
		if ((FPlatformTime::Seconds() - CacheUploadStart) * 1000.0 < CacheUploadBudgetMs
			&& UploadFromCache(*Leaf, bWantsCollision))
		{
			continue;
		}

		if (!LaunchPatch(*Leaf, bWantsCollision))
		{
			++Pending;
		}
	}

	Stats.LastFrameUploadMs += (FPlatformTime::Seconds() - CacheUploadStart) * 1000.0;
	Stats.WorstFrameUploadMs = FMath::Max(Stats.WorstFrameUploadMs, Stats.LastFrameUploadMs);

	Stats.PendingBuilds = Pending;
	Stats.SectionsActive = ActiveSections.Num();
	Stats.SectionsFree = FreeSections.Num();
	Stats.SectionsPending = InFlight.Num();
	Stats.NodesWithCollision = WithCollision;
	Stats.JobsInFlight = InFlight.Num();

	// Trace every few seconds for the first minute. Under motion this is the
	// only way to see whether the pipeline is keeping up (§6.8).
	const double Now = World->GetTimeSeconds();
	if (Now >= NextTraceAt)
	{
		NextTraceAt = Now + 5.0;
		LogStats();
	}
}

void ALedgerPlanet::LogStats() const
{
	UE_LOG(LogLedger, Log, TEXT("=== TERRAIN (design §6.8 / §15.1 spike) ==="));
	UE_LOG(LogLedger, Log, TEXT("  visible nodes      %d  (deepest depth %d)"),
		Stats.VisibleNodes, Stats.DeepestVisibleDepth);
	UE_LOG(LogLedger, Log, TEXT("  with collision     %d"), Stats.NodesWithCollision);
	UE_LOG(LogLedger, Log, TEXT("  jobs in flight     %d"), Stats.JobsInFlight);
	UE_LOG(LogLedger, Log, TEXT("  starved this frame %d"), Stats.PendingBuilds);
	UE_LOG(LogLedger, Log, TEXT("  sections           %d active, %d in flight, %d free of %d"),
		Stats.SectionsActive, Stats.SectionsPending, Stats.SectionsFree, MeshPool.Num());
	UE_LOG(LogLedger, Log, TEXT("  holes              %d now, %d worst at t=%.0fs"),
		Stats.UnfilledNodes, Stats.WorstUnfilled, Stats.WorstUnfilledAt);
	UE_LOG(LogLedger, Log, TEXT("  patches total      %lld"), Stats.TotalBuilds);
	UE_LOG(LogLedger, Log, TEXT("  upload ms (game)   last %.3f  worst %.3f"),
		Stats.LastFrameUploadMs, Stats.WorstFrameUploadMs);
	UE_LOG(LogLedger, Log, TEXT("  generate ms (task) last %.3f"), Stats.LastPatchGenerationMs);
	UE_LOG(LogLedger, Log, TEXT("  cook ms            worst %.3f"), Stats.WorstFrameCollisionMs);
	UE_LOG(LogLedger, Log, TEXT("  water sections     %d uploaded"), Stats.WaterSections);
	UE_LOG(LogLedger, Log, TEXT("  patch cache        %lld hit / %lld generated (%.0f%% reused)"),
		Stats.CacheHits, Stats.CacheMisses,
		(Stats.CacheHits + Stats.CacheMisses) > 0
			? 100.0 * static_cast<double>(Stats.CacheHits) / static_cast<double>(Stats.CacheHits + Stats.CacheMisses)
			: 0.0);
	UE_LOG(LogLedger, Log, TEXT("                     %d entries, %.1f MB, %lld evicted"),
		Stats.CacheEntries, Stats.CacheMegabytes, Stats.CacheEvictions);
	UE_LOG(LogLedger, Log, TEXT("  camera speed       %.0f m/s"), CameraVelocityLocal.Length() / 100.0);
}
