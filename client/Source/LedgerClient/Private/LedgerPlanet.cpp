#include "LedgerPlanet.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "LedgerSimSubsystem.h"
#include "LedgerSurface.h"
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

	SurfaceMaterial = LedgerSurface::CreateTerrainMaterial(this, static_cast<uint32>(Seed));
	if (SurfaceMaterial == nullptr)
	{
		SurfaceMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly"));
	}
	UE_LOG(LogLedger, Log, TEXT("terrain material: %s"),
		SurfaceMaterial != nullptr ? *SurfaceMaterial->GetName() : TEXT("<none, engine default>"));

	BuildRoots();

	// Sized against the measured visible-leaf count with headroom for a fast
	// turn. Undersizing does not degrade gracefully on its own — the parent-hold
	// rule in `UpdateTree` is what stops a starved pool from punching holes.
	constexpr int32 PoolSize = 2560;
	MeshPool.Reserve(PoolSize);
	FreeSections.Reserve(PoolSize);
	for (int32 Index = 0; Index < PoolSize; ++Index)
	{
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
	double ViewportWidth,
	double FovRadians)
{
	// Cull first: a node the horizon hides needs neither geometry nor children.
	Node.bVisible = !IsBeyondHorizon(Node, CameraLocal);
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
	const double Distance = FVector3d::Distance(CameraLocal, NodeSurface);

	const double Error = LedgerTerrain::ScreenSpaceError(
		Node.WorldSize, Distance, ViewportWidth, FovRadians);

	if (Error > ErrorThresholdPixels && Node.Depth < MaxDepth)
	{
		if (!Node.bHasChildren)
		{
			Split(Node);
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Node.Children[Index].IsValid())
			{
				UpdateTree(*Node.Children[Index], CameraLocal, ViewportWidth, FovRadians);
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
		Collapse(Node);
	}
}

void ALedgerPlanet::CollectLeaves(const FLedgerQuadNode& Node, TArray<const FLedgerQuadNode*>& Out) const
{
	if (!Node.bVisible)
	{
		return;
	}

	if (Node.IsLeaf())
	{
		Out.Add(&Node);
		return;
	}

	// A split node still holds geometry while its children are being built, so
	// it counts for rendering purposes until they arrive.
	if (ActiveSections.Contains(NodeKey(Node)))
	{
		Out.Add(&Node);
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Node.Children[Index].IsValid())
		{
			CollectLeaves(*Node.Children[Index], Out);
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
		Mesh->SetVisibility(true);

		ActiveSections.Add(Job->Key, Job->SectionIndex);
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

void ALedgerPlanet::ReleaseSection(uint64 Key)
{
	int32 SectionIndex = INDEX_NONE;
	if (!ActiveSections.RemoveAndCopyValue(Key, SectionIndex))
	{
		return;
	}
	if (MeshPool.IsValidIndex(SectionIndex))
	{
		MeshPool[SectionIndex]->ClearMeshSection(0);
		MeshPool[SectionIndex]->SetVisibility(false);
		MeshPool[SectionIndex]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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

	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		UpdateTree(*RootNode, CameraLocal, ViewportWidth, Fov);
	}

	HarvestCompletedPatches();

	TArray<const FLedgerQuadNode*> Leaves;
	Leaves.Reserve(2048);
	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		CollectLeaves(*RootNode, Leaves);
	}

	Stats.VisibleNodes = Leaves.Num();
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

		if (!LaunchPatch(*Leaf, bWantsCollision))
		{
			++Pending;
		}
	}

	Stats.PendingBuilds = Pending;
	Stats.NodesWithCollision = WithCollision;
	Stats.JobsInFlight = InFlight.Num();

	// Trace every few seconds for the first minute. Under motion this is the
	// only way to see whether the pipeline is keeping up (§6.8).
	const double Now = World->GetTimeSeconds();
	if (Now >= NextTraceAt && Now < 90.0)
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
	UE_LOG(LogLedger, Log, TEXT("  patches total      %lld"), Stats.TotalBuilds);
	UE_LOG(LogLedger, Log, TEXT("  upload ms (game)   last %.3f  worst %.3f"),
		Stats.LastFrameUploadMs, Stats.WorstFrameUploadMs);
	UE_LOG(LogLedger, Log, TEXT("  generate ms (task) last %.3f"), Stats.LastPatchGenerationMs);
	UE_LOG(LogLedger, Log, TEXT("  cook ms            worst %.3f"), Stats.WorstFrameCollisionMs);
	UE_LOG(LogLedger, Log, TEXT("  camera speed       %.0f m/s"), CameraVelocityLocal.Length() / 100.0);
}
