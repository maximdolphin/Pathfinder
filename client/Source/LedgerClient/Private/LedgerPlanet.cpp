#include "LedgerPlanet.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "KismetProceduralMeshLibrary.h"
#include "LedgerSimSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

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
}

ALedgerPlanet::ALedgerPlanet()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ALedgerPlanet::BeginPlay()
{
	Super::BeginPlay();

	// No content of our own (§13.1: no art through Phase 0), so borrow an
	// engine material. A null result is fine — the default material still shows
	// the silhouette, and the silhouette is what the spike is measuring.
	// WorldGridMaterial ignores vertex colour, so the elevation banding the mesh
	// carries was invisible. The debug vertex-colour material shows it. Neither
	// is art — §13.1 has no art in it — but one of them tells you whether the
	// heightfield is doing anything.
	SurfaceMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly"));
	if (SurfaceMaterial == nullptr)
	{
		SurfaceMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial"));
	}

	BuildRoots();

	// A pool large enough for the visible set at the error threshold, with room
	// to spare so a fast turn does not stall on allocation.
	// Sized against the visible-leaf count at the default error threshold, with
	// headroom for a fast turn. At 256 the pool starved and a third of the
	// visible set never got geometry.
	constexpr int32 PoolSize = 1024;
	MeshPool.Reserve(PoolSize);
	FreeSections.Reserve(PoolSize);
	for (int32 Index = 0; Index < PoolSize; ++Index)
	{
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
		Mesh->SetupAttachment(Root);
		Mesh->RegisterComponent();
		// The one setting that decides whether this hitches (§6.8).
		Mesh->bUseAsyncCooking = true;
		Mesh->SetCastShadow(true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetVisibility(false);
		MeshPool.Add(Mesh);
		FreeSections.Add(Index);
	}

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Ledger.Terrain.Stats"),
		TEXT("Report terrain LOD and collision cook timings."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this] { LogStats(); }),
		ECVF_Default));

	UE_LOG(LogLedger, Log,
		TEXT("planet ready: radius %.0f cm, max elevation %.0f cm, seed %d, max depth %d"),
		Radius, MaxElevation, Seed, MaxDepth);
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
	return Radius + LedgerTerrain::Elevation(UnitDirection.GetSafeNormal(), static_cast<uint32>(Seed), MaxElevation);
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
			ReleaseSection(NodeKey(*Node.Children[Index]));
			Node.Children[Index].Reset();
		}
	}
	Node.bHasChildren = false;
}

bool ALedgerPlanet::IsBeyondHorizon(const FLedgerQuadNode& Node, const FVector3d& CameraLocal) const
{
	const double CameraDistance = CameraLocal.Length();

	// The horizon is computed against the *reference sphere*, not against the
	// highest possible mountain. Using `Radius + MaxElevation` here meant that
	// any camera below peak elevation — which is every camera near the ground —
	// failed the early-out and disabled culling entirely, so the whole planet
	// subdivided behind the player's head. Terrain that pokes over the horizon
	// is handled by the elevation margin below instead.
	if (CameraDistance <= Radius)
	{
		return false;
	}

	// Angle from the camera's own direction out to the horizon tangent.
	const double HorizonAngle = FMath::Acos(FMath::Clamp(Radius / CameraDistance, -1.0, 1.0));

	// The node's angular radius, since WorldSize is an arc length on the sphere,
	// plus the angle a full-height peak could show above the tangent. Half the
	// diagonal, so a node straddling the horizon stays in.
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
	// Distance to the node's surface point, not to the reference sphere — at
	// low altitude over a mountain the difference is the whole LOD decision.
	const FVector3d NodeDirection = Node.Centre.GetSafeNormal();
	const FVector3d NodeSurface = NodeDirection * SurfaceRadiusAt(NodeDirection);
	const double Distance = FVector3d::Distance(CameraLocal, NodeSurface);

	const double Error = LedgerTerrain::ScreenSpaceError(
		Node.WorldSize, Distance, ViewportWidth, FovRadians);

	// Cull first: a node the horizon hides needs neither geometry nor children.
	Node.bVisible = !IsBeyondHorizon(Node, CameraLocal);
	if (!Node.bVisible)
	{
		if (Node.bHasChildren)
		{
			Collapse(Node);
		}
		ReleaseSection(NodeKey(Node));
		return;
	}

	const bool bWantsChildren = Error > ErrorThresholdPixels && Node.Depth < MaxDepth;

	if (bWantsChildren)
	{
		if (!Node.bHasChildren)
		{
			ReleaseSection(NodeKey(Node));
			Split(Node);
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Node.Children[Index].IsValid())
			{
				UpdateTree(*Node.Children[Index], CameraLocal, ViewportWidth, FovRadians);
			}
		}
	}
	else if (Node.bHasChildren)
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

void ALedgerPlanet::BuildSection(const FLedgerQuadNode& Node, int32 SectionIndex, bool bWithCollision)
{
	UProceduralMeshComponent* Mesh = MeshPool[SectionIndex];
	const int32 Side = FMath::Max(3, GridResolution | 1); // force odd
	const int32 VertexCount = Side * Side;

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	TArray<int32> Triangles;
	TArray<FProcMeshTangent> Tangents;

	Vertices.SetNumUninitialized(VertexCount);
	Normals.SetNumZeroed(VertexCount);
	UVs.SetNumUninitialized(VertexCount);
	Colors.SetNumUninitialized(VertexCount);

	// Which edges border a coarser neighbour. Those edges get their odd
	// vertices collapsed onto the line between their even neighbours, so the
	// finer mesh meets the coarser one exactly — edge-index stitching, not
	// skirts (§6.8). Skirts would hide the crack behind extra fill rate;
	// this removes it.
	const double EdgeStep = Node.Extent * 0.5;
	const bool bCoarseLeft = LeafDepthAt(UnitSphereAt(Node, -0.02, 0.5)) < Node.Depth;
	const bool bCoarseRight = LeafDepthAt(UnitSphereAt(Node, 1.02, 0.5)) < Node.Depth;
	const bool bCoarseBottom = LeafDepthAt(UnitSphereAt(Node, 0.5, -0.02)) < Node.Depth;
	const bool bCoarseTop = LeafDepthAt(UnitSphereAt(Node, 0.5, 1.02)) < Node.Depth;
	(void)EdgeStep;

	const double Inverse = 1.0 / static_cast<double>(Side - 1);
	const uint32 NoiseSeed = static_cast<uint32>(Seed);

	for (int32 Y = 0; Y < Side; ++Y)
	{
		for (int32 X = 0; X < Side; ++X)
		{
			double LocalU = static_cast<double>(X) * Inverse;
			double LocalV = static_cast<double>(Y) * Inverse;

			// Collapse this vertex toward its even neighbour on a stitched edge.
			const bool bOddX = (X & 1) != 0;
			const bool bOddY = (Y & 1) != 0;
			if (X == 0 && bCoarseLeft && bOddY)
			{
				LocalV = static_cast<double>(Y - 1) * Inverse;
			}
			else if (X == Side - 1 && bCoarseRight && bOddY)
			{
				LocalV = static_cast<double>(Y - 1) * Inverse;
			}
			else if (Y == 0 && bCoarseBottom && bOddX)
			{
				LocalU = static_cast<double>(X - 1) * Inverse;
			}
			else if (Y == Side - 1 && bCoarseTop && bOddX)
			{
				LocalU = static_cast<double>(X - 1) * Inverse;
			}

			const FVector3d UnitSphere = UnitSphereAt(Node, LocalU, LocalV);
			const double Elevation = LedgerTerrain::Elevation(UnitSphere, NoiseSeed, MaxElevation);
			const FVector3d Surface = UnitSphere * (Radius + Elevation);

			const int32 Index = Y * Side + X;
			// Relative to the node centre: this is what keeps float precision
			// local, and it is why the same code works at planetary scale.
			Vertices[Index] = FVector(Surface - Node.Centre);
			UVs[Index] = FVector2D(LocalU, LocalV);

			// Elevation as vertex colour, so the shape reads even with no
			// authored material. Green low, grey-white high.
			const double Normalised = FMath::Clamp((Elevation / MaxElevation) * 0.5 + 0.5, 0.0, 1.0);
			const uint8 Shade = static_cast<uint8>(Normalised * 255.0);
			Colors[Index] = Elevation < 0.0
				? FColor(20, 40, 90, 255)
				: FColor(Shade, static_cast<uint8>(120 + Shade / 3), Shade / 2, 255);
		}
	}

	Triangles.Reserve((Side - 1) * (Side - 1) * 6);
	for (int32 Y = 0; Y < Side - 1; ++Y)
	{
		for (int32 X = 0; X < Side - 1; ++X)
		{
			const int32 A = Y * Side + X;
			const int32 B = A + 1;
			const int32 C = A + Side;
			const int32 D = C + 1;
			Triangles.Add(A); Triangles.Add(C); Triangles.Add(B);
			Triangles.Add(B); Triangles.Add(C); Triangles.Add(D);
		}
	}

	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(
		Vertices, Triangles, UVs, Normals, Tangents);

	Mesh->SetWorldLocation(GetActorLocation() + FVector(Node.Centre));
	Mesh->ClearMeshSection(0);

	const double CollisionStart = FPlatformTime::Seconds();
	Mesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, bWithCollision);
	if (bWithCollision)
	{
		Stats.LastFrameCollisionMs += (FPlatformTime::Seconds() - CollisionStart) * 1000.0;
	}

	Mesh->SetCollisionEnabled(bWithCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (SurfaceMaterial != nullptr)
	{
		Mesh->SetMaterial(0, SurfaceMaterial);
	}
	Mesh->SetVisibility(true);

	++Stats.TotalBuilds;
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

	Stats.LastFrameBuildMs = 0.0;
	Stats.LastFrameCollisionMs = 0.0;
	Stats.BuildsThisFrame = 0;

	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		UpdateTree(*RootNode, CameraLocal, ViewportWidth, Fov);
	}

	TArray<const FLedgerQuadNode*> Leaves;
	Leaves.Reserve(256);
	for (const TUniquePtr<FLedgerQuadNode>& RootNode : Roots)
	{
		CollectLeaves(*RootNode, Leaves);
	}

	Stats.VisibleNodes = Leaves.Num();

	// Where the camera will be shortly, so the cook has landed by the time we
	// arrive rather than starting when we get there.
	const FVector3d PredictedLocal = CameraLocal + CameraVelocityLocal * CollisionLeadSeconds;

	// Nearest first: if the budget runs out, it runs out on the far nodes.
	Leaves.Sort([&CameraLocal](const FLedgerQuadNode& A, const FLedgerQuadNode& B)
	{
		return FVector3d::DistSquared(A.Centre, CameraLocal) < FVector3d::DistSquared(B.Centre, CameraLocal);
	});

	const double BuildStart = FPlatformTime::Seconds();
	int32 Pending = 0;
	int32 WithCollision = 0;

	for (const FLedgerQuadNode* Leaf : Leaves)
	{
		const uint64 Key = NodeKey(*Leaf);
		const double NearNow = FVector3d::Distance(Leaf->Centre, CameraLocal);
		const double NearSoon = FVector3d::Distance(Leaf->Centre, PredictedLocal);
		const bool bWantsCollision = FMath::Min(NearNow, NearSoon) < CollisionRadius;
		if (bWantsCollision)
		{
			++WithCollision;
		}

		if (ActiveSections.Contains(Key))
		{
			continue;
		}

		if (Stats.BuildsThisFrame >= BuildBudgetPerFrame || FreeSections.Num() == 0)
		{
			++Pending;
			continue;
		}

		const int32 SectionIndex = FreeSections.Pop();
		ActiveSections.Add(Key, SectionIndex);
		BuildSection(*Leaf, SectionIndex, bWantsCollision);
		++Stats.BuildsThisFrame;
	}

	Stats.PendingBuilds = Pending;
	Stats.NodesWithCollision = WithCollision;
	Stats.LastFrameBuildMs = (FPlatformTime::Seconds() - BuildStart) * 1000.0;
	Stats.WorstFrameBuildMs = FMath::Max(Stats.WorstFrameBuildMs, Stats.LastFrameBuildMs);
	Stats.WorstFrameCollisionMs = FMath::Max(Stats.WorstFrameCollisionMs, Stats.LastFrameCollisionMs);

	// Trace every few seconds for the first minute. Under motion this is the
	// only way to see whether the cook budget is holding (§6.8).
	const double Now = World->GetTimeSeconds();
	if (Now >= NextTraceAt && Now < 60.0)
	{
		NextTraceAt = Now + 5.0;
		LogStats();
	}
}

void ALedgerPlanet::LogStats() const
{
	UE_LOG(LogLedger, Log, TEXT("=== TERRAIN (design §6.8 / §15.1 spike) ==="));
	UE_LOG(LogLedger, Log, TEXT("  visible nodes      %d"), Stats.VisibleNodes);
	UE_LOG(LogLedger, Log, TEXT("  with collision     %d"), Stats.NodesWithCollision);
	UE_LOG(LogLedger, Log, TEXT("  builds queued      %d"), Stats.PendingBuilds);
	UE_LOG(LogLedger, Log, TEXT("  builds total       %lld"), Stats.TotalBuilds);
	UE_LOG(LogLedger, Log, TEXT("  build ms  last %.3f  worst %.3f"), Stats.LastFrameBuildMs, Stats.WorstFrameBuildMs);
	UE_LOG(LogLedger, Log, TEXT("  cook ms   last %.3f  worst %.3f"), Stats.LastFrameCollisionMs, Stats.WorstFrameCollisionMs);
	UE_LOG(LogLedger, Log, TEXT("  camera speed       %.0f cm/s"), CameraVelocityLocal.Length());
}
