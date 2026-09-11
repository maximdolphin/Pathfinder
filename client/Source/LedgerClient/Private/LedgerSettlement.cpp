#include "LedgerSettlement.h"

#include "LedgerMeshBuilder.h"
#include "LedgerPlanet.h"
#include "LedgerLog.h"
#include "LedgerSurface.h"
#include "LedgerWind.h"
#include "Engine/World.h"
#include "LedgerTerrainMath.h"
#include "StaticMeshResources.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "LedgerMeshBake.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	/// Deterministic scatter. Same seed, same town, every run and every machine.
	struct FScatterRandom
	{
		uint32 State;

		explicit FScatterRandom(uint32 InSeed) : State(InSeed | 1u) {}

		uint32 Next()
		{
			State ^= State << 13;
			State ^= State >> 17;
			State ^= State << 5;
			return State;
		}

		double Unit() { return static_cast<double>(Next() & 0xFFFFFFu) / static_cast<double>(0xFFFFFF); }
		double Range(double Min, double Max) { return Min + Unit() * (Max - Min); }
		int32 Below(int32 N) { return static_cast<int32>(Next() % static_cast<uint32>(FMath::Max(1, N))); }
	};

	const FColor WallColours[5] = {
		FColor(150, 142, 128, 255),
		FColor(122, 116, 108, 255),
		FColor(138, 124, 104, 255),
		FColor(104, 106, 110, 255),
		FColor(160, 150, 132, 255),
	};

	const FColor RoofColour(72, 68, 66, 255);
	const FColor PadColour(58, 60, 64, 255);
	const FColor PadStripe(184, 152, 46, 255);
	const FColor TrunkColour(58, 44, 32, 255);
	const FColor CanopyColour(44, 68, 36, 255);
	const FColor CanopyColourAlt(56, 78, 42, 255);

	/// Where the impostors take over from the full trees, cm. An eighteen-metre
	/// tree at 250 m is about seven per cent of a 90 degree view high, which is
	/// where its cones stop reading as cones.
	constexpr int32 TreeImpostorFromCm = 25000;
}

ALedgerSettlement::ALedgerSettlement()
{
	// Ticks for the vane and nothing else.
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	const TCHAR* InstanceNames[2] = { TEXT("TreesA"), TEXT("TreesB") };
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		TreeInstances[Variant] = CreateDefaultSubobject<
			UHierarchicalInstancedStaticMeshComponent>(InstanceNames[Variant]);
		TreeInstances[Variant]->SetupAttachment(Root);
		TreeInstances[Variant]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TreeInstances[Variant]->SetCastShadow(true);
		TreeImpostors[Variant] = CreateDefaultSubobject<
			UHierarchicalInstancedStaticMeshComponent>(Variant == 0 ? TEXT("TreeImpostorsA") : TEXT("TreeImpostorsB"));
		TreeImpostors[Variant]->SetupAttachment(Root);
		TreeImpostors[Variant]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TreeImpostors[Variant]->SetCastShadow(true);
	}

	// Buildings and the pad block, because a ship has to be able to land on
	// the one and not fly through the other -- the procedural mesh these
	// replace cooked its collision at build time for the same reason.
	for (int32 Variant = 0; Variant < WallVariants; ++Variant)
	{
		Buildings[Variant] = CreateDefaultSubobject<
			UHierarchicalInstancedStaticMeshComponent>(
				*FString::Printf(TEXT("Buildings%d"), Variant));
		Buildings[Variant]->SetupAttachment(Root);
		Buildings[Variant]->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Buildings[Variant]->SetCastShadow(true);
	}
	Pad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pad"));
	Pad->SetupAttachment(Root);
	Pad->SetMobility(EComponentMobility::Movable);
	Pad->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	for (TObjectPtr<UStaticMeshComponent>* Part : { &VanePole, &Vane })
	{
		*Part = CreateDefaultSubobject<UStaticMeshComponent>(
			Part == &VanePole ? TEXT("VanePole") : TEXT("Vane"));
		(*Part)->SetupAttachment(Root);
		(*Part)->SetMobility(EComponentMobility::Movable);
		(*Part)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void ALedgerSettlement::SurfaceFrame(
	const FVector3d& SiteDirection,
	const FVector3d& East,
	const FVector3d& North,
	double OffsetEast,
	double OffsetNorth,
	FVector& OutLocation,
	FVector& OutUp) const
{
	if (Planet == nullptr)
	{
		OutLocation = FVector::ZeroVector;
		OutUp = FVector::UpVector;
		return;
	}

	// Offsets are metres along the tangent plane; re-normalising bends them back
	// onto the sphere. Over a few hundred metres the correction is tiny, but it
	// is the difference between a town that sits on the ground and one that
	// slowly lifts off it.
	const FVector3d Direction =
		(SiteDirection * Planet->Radius + East * OffsetEast + North * OffsetNorth).GetSafeNormal();

	const double SurfaceRadius = Planet->SurfaceRadiusAt(Direction);
	OutLocation = Planet->GetActorLocation() + FVector(Direction * SurfaceRadius);
	OutUp = FVector(Direction);
}

// One tree, at the origin, unit scale, +Z up.
//
// Separated from placement so the same shape has two destinations: a baked
// static mesh, and the instance transforms that put it on the ground. Before
// this the settlement generated 43,092 vertices of tree geometry on the game
// thread every time the world was built, merged into one buffer that Nanite
// could not touch, no instance could be culled out of, and no distance field
// existed for.
//
// `bAltCanopy` picks which of the two canopy colours the lower cone gets. Two
// meshes rather than one, because the variation is baked into vertex colour and
// the alternative is a per-instance colour the flat material cannot read. Two
// draws for three hundred and forty-two trees is still two.
namespace LedgerTreeShape
{
	// One tree, two descriptions of it: the mesh and its impostor.
	constexpr double TrunkHeight = 620.0;
	constexpr double TrunkRadius = 42.0;
	constexpr double CanopyHeight = 1180.0;
	constexpr double CanopyRadius = 400.0;
}

int32 ALedgerSettlement::DescribeTree(FLedgerMeshBuilder& Builder, bool bAltCanopy)
{
	// The file-scope colours, used directly rather than shadowed.
	using namespace LedgerTreeShape;

	// Sunk, so the trunk still meets the ground on a slope. Scaled with the
	// instance rather than fixed, which is if anything more correct: a bigger
	// tree sits deeper.
	Builder.AddCylinder(FTransform(FVector(0.0, 0.0, -90.0)),
		TrunkRadius, TrunkRadius * 0.72, TrunkHeight, 6, TrunkColour);

	// Where the trunk ends and the canopy begins, in triangles. The bake turns
	// this into a second material slot, so the two colours survive as materials
	// rather than as vertex colours -- which a baked static mesh on an
	// instanced component does not carry through to the shader.
	const int32 CanopyStartsAt = Builder.Triangles.Num() / 3;

	Builder.AddCone(FTransform(FVector(0.0, 0.0, TrunkHeight * 0.55)),
		CanopyRadius, CanopyHeight, 7, bAltCanopy ? CanopyColourAlt : CanopyColour);

	// A second, smaller cone above, so the silhouette is not one pyramid
	// repeated three hundred times.
	Builder.AddCone(
		FTransform(FVector(0.0, 0.0, TrunkHeight * 0.55 + CanopyHeight * 0.42)),
		CanopyRadius * 0.66, CanopyHeight * 0.7, 7, CanopyColourAlt);

	return CanopyStartsAt;
}

int32 ALedgerSettlement::DescribeTreeImpostor(FLedgerMeshBuilder& Builder, bool bAltCanopy)
{
	using namespace LedgerTreeShape;
	const double CanopyBase = TrunkHeight * 0.55;
	const double UpperBase = CanopyBase + CanopyHeight * 0.42;
	const FVector Up(0.0, 0.0, 1.0);

	// Two planes at right angles, each the tree's outline seen side on.
	// Normals are the cone's own, not the card's: a card lit by its face goes
	// dark whenever the sun is behind it, which is half the forest at any hour.
	// Tilted up by the cone's slope (radius over height), so the crown is lit
	// from above the way the cones are.
	auto Outward = [&Up](const FVector& Across, double Radius, double Height)
	{
		return (Across * Height + Up * Radius).GetSafeNormal();
	};
	const FVector Planes[2] = { FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0) };
	for (const FVector& Across : Planes)
	{
		// The trunk, up into the canopy, sunk like the mesh's.
		const FVector L0 = -Across * TrunkRadius + Up * -90.0;
		const FVector R0 = Across * TrunkRadius + Up * -90.0;
		const FVector L1 = -Across * TrunkRadius * 0.72 + Up * CanopyBase;
		const FVector R1 = Across * TrunkRadius * 0.72 + Up * CanopyBase;
		Builder.AddCardTriangle(L0, R0, R1, -Across, Across, Across, TrunkColour);
		Builder.AddCardTriangle(L0, R1, L1, -Across, Across, -Across, TrunkColour);
	}
	const int32 CanopyStartsAt = Builder.Triangles.Num() / 3;
	for (const FVector& Across : Planes)
	{
		Builder.AddCardTriangle(
			-Across * CanopyRadius + Up * CanopyBase, Across * CanopyRadius + Up * CanopyBase,
			Up * (CanopyBase + CanopyHeight),
			Outward(-Across, CanopyRadius, CanopyHeight), Outward(Across, CanopyRadius, CanopyHeight), Up,
			bAltCanopy ? CanopyColourAlt : CanopyColour);
		Builder.AddCardTriangle(
			-Across * CanopyRadius * 0.66 + Up * UpperBase, Across * CanopyRadius * 0.66 + Up * UpperBase,
			Up * (UpperBase + CanopyHeight * 0.7),
			Outward(-Across, CanopyRadius * 0.66, CanopyHeight * 0.7),
			Outward(Across, CanopyRadius * 0.66, CanopyHeight * 0.7), Up,
			CanopyColourAlt);
	}
	return CanopyStartsAt;
}

void ALedgerSettlement::ForceTreeLod(int32 Lod) const
{
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		UHierarchicalInstancedStaticMeshComponent* Full = TreeInstances[Variant];
		UHierarchicalInstancedStaticMeshComponent* Impostor = TreeImpostors[Variant];
		if (Full == nullptr || Impostor == nullptr)
		{
			continue;
		}
		Full->SetVisibility(Lod != 2);
		Impostor->SetVisibility(Lod != 1);
		Full->SetCullDistances(0, Lod == 0 ? TreeImpostorFromCm : 0);
		Impostor->SetCullDistances(Lod == 0 ? TreeImpostorFromCm : 0, 0);
	}
}


FVector ALedgerSettlement::VaneLocation() const
{
	return VaneTop;
}

void ALedgerSettlement::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	// T099: the air the town stands in, read where the vane reads the wind.
	VaneAir = LedgerEnvironment::At(World, VaneTop);

	const ULedgerWind* Wind = World != nullptr ? World->GetSubsystem<ULedgerWind>() : nullptr;
	if (Wind == nullptr || Vane == nullptr || Vane->GetStaticMesh() == nullptr)
	{
		return;
	}

	// **Every tick, from the one wind.** The vane reads the field where it
	// stands, twelve metres up, and keeps nothing of its own -- so it cannot
	// lag behind a change or disagree with anything else about what the air
	// is doing.
	LastVaneWind = Wind->WindAtMetres(VaneTop);
	const FVector3d Across = LastVaneWind - VaneUp * FVector3d::DotProduct(LastVaneWind, VaneUp);
	if (Across.Length() < 0.1)
	{
		// Calm: a vane in still air points wherever it last pointed.
		return;
	}
	const FVector3d Downwind = Across.GetSafeNormal();
	VaneBearing = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(
		FVector3d::DotProduct(Downwind, VaneEast),
		FVector3d::DotProduct(Downwind, VaneNorth))) + 360.0, 360.0);

	// The streamer's long axis along the wind, hung off the pole's top so it
	// trails downwind of it rather than being skewered through the middle.
	const FQuat Heading = FRotationMatrix::MakeFromXZ(
		FVector(Downwind), FVector(VaneUp)).ToQuat();
	Vane->SetWorldTransform(FTransform(Heading,
		VaneTop + FVector(Downwind) * 150.0 - FVector(VaneUp) * 30.0,
		FVector(3.0, 0.08, 0.6)));
}

int32 ALedgerSettlement::DescribeBuilding(FLedgerMeshBuilder& Builder)
{
	// White: the colour is the material's tint, one component per wall colour.
	// Per-vertex colour would not survive the trip onto an instanced component,
	// which three hundred and forty-two black trees already established.
	Builder.AddBox(FTransform(FVector(0.0, 0.0, 50.0)),
		FVector(50.0, 50.0, 50.0), FColor::White);

	const int32 RoofStartsAt = Builder.Triangles.Num() / 3;

	// The cap the procedural town had: eight per cent wider than the walls.
	// Its thickness now scales with the building, four per cent of the height
	// -- thirty-six centimetres on the lowest and a metre and a quarter on the
	// tallest, where it was a fixed metre and twenty before.
	Builder.AddBox(FTransform(FVector(0.0, 0.0, 102.0)),
		FVector(54.0, 54.0, 2.0), FColor::White);
	return RoofStartsAt;
}

int32 ALedgerSettlement::DescribePad(FLedgerMeshBuilder& Builder)
{
	constexpr double PadHalf = 2600.0;   // 52 m square
	constexpr double PadThickness = 120.0;

	// Sunk slightly, so uneven ground under it does not show a gap at the rim.
	Builder.AddBox(FTransform(FVector(0.0, 0.0, -PadThickness * 0.4)),
		FVector(PadHalf, PadHalf, PadThickness), FColor::White);

	const int32 StripesStartAt = Builder.Triangles.Num() / 3;

	// Two stripes, so it reads as a pad rather than a slab.
	for (int32 Stripe = -1; Stripe <= 1; Stripe += 2)
	{
		Builder.AddBox(
			FTransform(FVector(0.0, Stripe * PadHalf * 0.55, PadThickness * 0.62)),
			FVector(PadHalf * 0.78, 90.0, 16.0), FColor::White);
	}
	return StripesStartAt;
}

namespace
{
	/// Straight 0-1, not through FLinearColor(FColor).
	///
	/// These colours were authored for the procedural mesh, whose vertex
	/// colours reach the shader undecoded. FLinearColor(FColor) applies the
	/// sRGB decode and would darken every wall by a factor of three or more,
	/// so the town that came out of the bake would not be the town that went
	/// in.
	FLinearColor SettlementTint(const FColor& Colour)
	{
		return FLinearColor(Colour.R / 255.0f, Colour.G / 255.0f,
			Colour.B / 255.0f, 1.0f);
	}

	UStaticMesh* SettlementMesh(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("%s%s.%s"),
			LedgerMesh::MeshPackageRoot, Name, Name);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		if (Mesh == nullptr)
		{
			UE_LOG(LogLedger, Error,
				TEXT("no baked mesh at %s. Run: UnrealEditor.exe <project> -game -bakemeshes"),
				*Path);
		}
		return Mesh;
	}
}

void ALedgerSettlement::PlaceBuildings(const FTransform& PadTransform)
{
	UStaticMesh* Building = SettlementMesh(TEXT("SM_Building"));
	UStaticMesh* PadMesh = SettlementMesh(TEXT("SM_Pad"));

	UMaterialInterface* Roof = LedgerSurface::CreateFlatMaterial(
		this, SettlementTint(RoofColour), 0.82f);
	int32 Count = 0;
	for (int32 Variant = 0; Variant < WallVariants; ++Variant)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = Buildings[Variant];
		if (Component == nullptr)
		{
			continue;
		}
		Component->ClearInstances();
		if (Building == nullptr)
		{
			continue;
		}
		Component->SetStaticMesh(Building);
		Component->SetMaterial(0, LedgerSurface::CreateFlatMaterial(
			this, SettlementTint(WallColours[Variant]), 0.82f));
		Component->SetMaterial(1, Roof);
		Component->AddInstances(BuildingTransforms[Variant],
			/*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
		Count += Component->GetInstanceCount();
	}

	if (Pad != nullptr && PadMesh != nullptr)
	{
		Pad->SetStaticMesh(PadMesh);
		Pad->SetWorldTransform(PadTransform);
		Pad->SetMaterial(0, LedgerSurface::CreateFlatMaterial(
			this, SettlementTint(PadColour), 0.82f));
		Pad->SetMaterial(1, LedgerSurface::CreateFlatMaterial(
			this, SettlementTint(PadStripe), 0.82f));
	}

	// The vane: a twelve-metre pole just off the pad's corner and a
	// three-metre streamer at its top, both the unit building box scaled.
	if (Building != nullptr && VanePole != nullptr && Vane != nullptr)
	{
		const FVector Base = VaneTop - FVector(VaneUp) * 1200.0;
		VanePole->SetStaticMesh(Building);
		VanePole->SetWorldTransform(FTransform(
			FRotationMatrix::MakeFromZX(FVector(VaneUp), FVector(VaneNorth)).ToQuat(),
			Base, FVector(0.25, 0.25, 12.0)));
		VanePole->SetMaterial(0, LedgerSurface::CreateFlatMaterial(
			this, SettlementTint(RoofColour), 0.6f));
		VanePole->SetMaterial(1, LedgerSurface::CreateFlatMaterial(
			this, SettlementTint(RoofColour), 0.6f));
		Vane->SetStaticMesh(Building);
		UMaterialInterface* Streamer = LedgerSurface::CreateFlatMaterial(
			this, SettlementTint(PadStripe), 0.7f);
		Vane->SetMaterial(0, Streamer);
		Vane->SetMaterial(1, Streamer);
	}

	UE_LOG(LogLedger, Log, TEXT("buildings: %d instances of %s across %d colours, pad %s"),
		Count, Building != nullptr ? *Building->GetName() : TEXT("<none>"),
		WallVariants, PadMesh != nullptr ? *PadMesh->GetName() : TEXT("<none>"));
}

// The instances, into two hierarchical instanced components.
//
// Hierarchical rather than plain instanced: it builds a cluster tree, so three
// hundred trees behind a hill are culled as a group instead of individually,
// and each cluster picks its own detail. The merged procedural mesh this
// replaces could do neither -- it was one primitive, drawn whole or not at all.
//
// Two components, one per canopy colour, because the variation lives in vertex
// colour and an instance cannot carry its own. Two draws for the whole forest.
void ALedgerSettlement::PlaceTrees()
{
	const TCHAR* Names[2] = { TEXT("SM_Tree_A"), TEXT("SM_Tree_B") };

	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = TreeInstances[Variant];
		if (Component == nullptr)
		{
			continue;
		}

		Component->ClearInstances();

		const FString Path = FString::Printf(TEXT("%s%s.%s"),
			LedgerMesh::MeshPackageRoot, Names[Variant], Names[Variant]);

		// Split, because the two halves have very different fixes. Loading is a
		// synchronous asset load that the procedural path never had to do and
		// that a shipped build would do once, ahead of time. Placing is the part
		// that replaced generating forty-three thousand vertices.
		const double LoadStarted = FPlatformTime::Seconds();
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		const double LoadMs = (FPlatformTime::Seconds() - LoadStarted) * 1000.0;
		if (Mesh == nullptr)
		{
			// Loud, and specific about the fix. A silent empty forest is the
			// kind of absence that gets attributed to the scatter rules.
			UE_LOG(LogLedger, Error,
				TEXT("no baked tree mesh at %s. Run: tools/generate_assets.py --only meshes"),
				*Path);
			continue;
		}

		const double PlaceStarted = FPlatformTime::Seconds();
		Component->SetStaticMesh(Mesh);
		Component->AddInstances(TreeTransforms[Variant], /*bShouldReturnIndices*/ false,
			/*bWorldSpace*/ true);
		// The impostors: the same trees, from where the full ones stop.
		{
			const FString ImpostorName = FString(Names[Variant]) + TEXT("_Impostor");
			UStaticMesh* ImpostorMesh = LoadObject<UStaticMesh>(nullptr,
				*FString::Printf(TEXT("%s%s.%s"), LedgerMesh::MeshPackageRoot, *ImpostorName, *ImpostorName));
			UHierarchicalInstancedStaticMeshComponent* Far = TreeImpostors[Variant];
			if (ImpostorMesh != nullptr && Far != nullptr)
			{
				Far->ClearInstances();
				Far->SetStaticMesh(ImpostorMesh);
				Far->AddInstances(TreeTransforms[Variant], /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
				Component->SetCullDistances(0, TreeImpostorFromCm);
				Far->SetCullDistances(TreeImpostorFromCm, 0);
				const FStaticMeshRenderData* FarRender = ImpostorMesh->GetRenderData();
				UE_LOG(LogLedger, Log, TEXT("trees %d: impostor %s, %d triangles, from %d m"), Variant, *ImpostorName,
					FarRender != nullptr && FarRender->LODResources.Num() > 0 ? FarRender->LODResources[0].GetNumTriangles() : 0,
					TreeImpostorFromCm / 100);
			}
			else
			{
				UE_LOG(LogLedger, Error, TEXT("no baked tree impostor %s: every tree is drawn in full at every distance. Run -bakemeshes"), *ImpostorName);
			}
		}

		// The LODs the asset actually has, so an impostor that is not there is
		// a line in the log rather than a photograph that looks the same.
		const FStaticMeshRenderData* Render = Mesh->GetRenderData();
		const int32 Lods = Render != nullptr ? Render->LODResources.Num() : 0;
		UE_LOG(LogLedger, Log,
			TEXT("trees %d: load %.1f ms, place %.1f ms, %d instances, %d LODs (%d and %d triangles)"),
			Variant, LoadMs, (FPlatformTime::Seconds() - PlaceStarted) * 1000.0,
			Component->GetInstanceCount(), Lods,
			Lods > 0 ? Render->LODResources[0].GetNumTriangles() : 0,
			Lods > 1 ? Render->LODResources[1].GetNumTriangles() : 0);

	}
}


FVector ALedgerSettlement::Build(ALedgerPlanet* InPlanet, const FVector3d& SiteDirection, uint32 InSeed)
{
	Planet = InPlanet;
	if (Planet == nullptr)
	{
		return FVector::ZeroVector;
	}

	// A local tangent basis. `East` is arbitrary but must not be parallel to the
	// site direction, which is why the fallback exists.
	FVector3d East = FVector3d::CrossProduct(SiteDirection, FVector3d::UpVector).GetSafeNormal();
	if (East.IsNearlyZero())
	{
		East = FVector3d::CrossProduct(SiteDirection, FVector3d::ForwardVector).GetSafeNormal();
	}
	const FVector3d North = FVector3d::CrossProduct(East, SiteDirection).GetSafeNormal();

	FScatterRandom Random(InSeed ^ 0x70770000u);
	const double BuildStarted = FPlatformTime::Seconds();
	TreeTransforms[0].Reset();
	TreeTransforms[1].Reset();
	for (TArray<FTransform>& Transforms : BuildingTransforms)
	{
		Transforms.Reset();
	}

	// ---- landing pad ----------------------------------------------------
	FVector PadCentre;
	FVector PadUp;
	SurfaceFrame(SiteDirection, East, North, 0.0, 0.0, PadCentre, PadUp);

	// Where the vane stands: just off the pad's north-east corner.
	{
		FVector VaneGround;
		FVector VaneSurfaceUp;
		SurfaceFrame(SiteDirection, East, North, 3400.0, 3400.0, VaneGround, VaneSurfaceUp);
		VaneUp = FVector3d(VaneSurfaceUp);
		VaneEast = East;
		VaneNorth = North;
		VaneTop = VaneGround + VaneSurfaceUp * 1200.0;
	}

	// A transform, not geometry: the shape is the baked SM_Pad.
	const FTransform PadTransform(
		FRotationMatrix::MakeFromZX(PadUp, FVector(North)).ToQuat(), PadCentre);
	PadLocation = PadCentre + PadUp * 400.0;

	// ---- buildings ------------------------------------------------------
	//
	// Laid out along two streets rather than scattered: a settlement is the one
	// thing on a procedural planet that must not look procedural, because the
	// eye knows exactly what a town is supposed to look like.
	int32 Placed = 0;
	for (int32 Street = 0; Street < 2 && Placed < BuildingCount; ++Street)
	{
		const double StreetOffset = (Street == 0 ? -1.0 : 1.0) * 5200.0;
		const int32 PerSide = BuildingCount / 4 + 1;

		for (int32 Side = 0; Side < 2 && Placed < BuildingCount; ++Side)
		{
			const double SideOffset = (Side == 0 ? -1.0 : 1.0) * 2600.0;

			for (int32 Index = 0; Index < PerSide && Placed < BuildingCount; ++Index)
			{
				const double Along = (Index - PerSide * 0.5) * 3400.0 + Random.Range(-500.0, 500.0);
				const double Across = StreetOffset + SideOffset + Random.Range(-300.0, 300.0);

				// Keep the pad clear.
				if (FMath::Abs(Along) < 3400.0 && FMath::Abs(Across) < 3400.0)
				{
					continue;
				}

				FVector Location;
				FVector Up;
				SurfaceFrame(SiteDirection, East, North, Across, Along, Location, Up);

				const double Width = Random.Range(700.0, 1500.0);
				const double Depth = Random.Range(700.0, 1400.0);
				const double Height = Random.Range(900.0, 3200.0);

				// Yaw variation only — buildings stand up, whatever the slope,
				// and the ground is embedded rather than followed.
				const double Yaw = Random.Range(-6.0, 6.0);
				const FQuat Rotation =
					FRotationMatrix::MakeFromZX(Up, FVector(North)).ToQuat()
					* FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw));

				// Sunk by a third of a storey so a sloped footprint never shows
				// daylight under a wall.
				const FVector Base = Location - Up * 260.0;

				// SM_Building is a hundred centimetres a side standing on its
				// origin, so the scale is the size over a hundred. The colour is
				// drawn here, in the same place in the sequence it always was,
				// so the town is laid out exactly as before.
				BuildingTransforms[Random.Below(WallVariants)].Add(FTransform(
					Rotation, Base,
					FVector(Depth * 2.0 / 100.0, Width * 2.0 / 100.0, Height / 100.0)));

				++Placed;
			}
		}
	}

	// ---- trees ----------------------------------------------------------
	//
	// Scattered on a jittered grid rather than uniformly at random: pure random
	// clumps and leaves bald patches, and both read as a bug.
	const int32 GridSide = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<double>(TreeCount))));
	const double Spacing = (TreeRadius * 2.0) / GridSide;
	int32 Grown = 0;

	for (int32 Row = 0; Row < GridSide && Grown < TreeCount; ++Row)
	{
		for (int32 Column = 0; Column < GridSide && Grown < TreeCount; ++Column)
		{
			const double CellEast = -TreeRadius + (Column + 0.5) * Spacing + Random.Range(-Spacing * 0.4, Spacing * 0.4);
			const double CellNorth = -TreeRadius + (Row + 0.5) * Spacing + Random.Range(-Spacing * 0.4, Spacing * 0.4);

			const double DistanceFromCentre = FMath::Sqrt(CellEast * CellEast + CellNorth * CellNorth);
			if (DistanceFromCentre > TreeRadius)
			{
				continue;
			}
			// Nothing grows in the town or on the pad.
			if (DistanceFromCentre < TownRadius * 0.62)
			{
				continue;
			}
			// Thin out toward the edge so the wood has a boundary rather than a
			// circle cut out of it.
			if (Random.Unit() < FMath::Pow(DistanceFromCentre / TreeRadius, 2.2))
			{
				continue;
			}

			FVector Location;
			FVector Up;
			SurfaceFrame(SiteDirection, East, North, CellEast, CellNorth, Location, Up);

			// Slope test: sample two nearby surface points and reject anything
			// steep. Trees on a cliff face are the classic scatter giveaway.
			FVector NearEast, NearNorth, IgnoredUp;
			SurfaceFrame(SiteDirection, East, North, CellEast + 400.0, CellNorth, NearEast, IgnoredUp);
			SurfaceFrame(SiteDirection, East, North, CellEast, CellNorth + 400.0, NearNorth, IgnoredUp);
			const double RiseEast = FMath::Abs(FVector::DotProduct(NearEast - Location, Up));
			const double RiseNorth = FMath::Abs(FVector::DotProduct(NearNorth - Location, Up));
			if (FMath::Max(RiseEast, RiseNorth) > 400.0 * 0.55)
			{
				continue;
			}

			const FQuat Rotation = FRotationMatrix::MakeFromZX(Up, FVector(North)).ToQuat();
			const double Scale = Random.Range(0.72, 1.45);

			// A transform, not geometry. The shape is a baked static mesh; this
			// only says where one goes.
			const bool bAlt = Random.Unit() < 0.5;
			TreeTransforms[bAlt ? 1 : 0].Add(
				FTransform(Rotation, Location, FVector(Scale)));

			++Grown;
		}
	}

	SetActorLocation(FVector::ZeroVector);
	PlaceBuildings(PadTransform);
	PlaceTrees();

	// What building the settlement costs the game thread. Measured because the
	// instancing task is judged on this number falling, and "it felt faster" is
	// not a measurement.
	UE_LOG(LogLedger, Log,
		TEXT("settlement: %d buildings, %d trees, %d instances, built in %.1f ms"),
		Placed, Grown, TreeTransforms[0].Num() + TreeTransforms[1].Num(),
		(FPlatformTime::Seconds() - BuildStarted) * 1000.0);

	{
		// The instanced foliage, which needs it just as much. Dropping this
		// when the merged foliage mesh was removed left three hundred and
		// forty-two trees rendering as black cut-outs -- correctly placed,
		// correctly instanced, and lit by nothing.
		for (UHierarchicalInstancedStaticMeshComponent* Component : { TreeInstances[0].Get(), TreeInstances[1].Get(), TreeImpostors[0].Get(), TreeImpostors[1].Get() })
		{
			if (Component == nullptr)
			{
				continue;
			}

			// Slot 0 is the trunk, slot 1 the canopy, and each gets a material
			// of its own colour. The white-base-plus-vertex-colour arrangement
			// the merged foliage mesh used does not survive being baked and
			// instanced, and three attempts to make it survive were three wrong
			// guesses -- a missing assignment, an sRGB decode, and Nanite.
			if (UMaterialInterface* Bark = LedgerSurface::CreateFlatMaterial(
				this, FLinearColor(TrunkColour), 0.92f))
			{
				Component->SetMaterial(0, Bark);
			}
			// The canopy leans with the wind; the trunk does not, because a
			// trunk's deflection is the base of the cantilever and a wood
			// whose trunks sway reads as rubber.
			if (UMaterialInterface* Leaf = LedgerSurface::CreateFoliageMaterial(
				this, FLinearColor(Component == TreeInstances[1] || Component == TreeImpostors[1] ? CanopyColourAlt : CanopyColour),
				0.88f))
			{
				Component->SetMaterial(1, Leaf);
			}
			UE_LOG(LogLedger, Log, TEXT("trees: %d instances, %d material slots"),
				Component->GetInstanceCount(), Component->GetNumMaterials());
		}
	}

	UE_LOG(LogLedger, Log, TEXT("settlement: %d buildings, %d trees, pad at %s"),
		Placed, Grown, *PadLocation.ToCompactString());

	return PadLocation;
}

FTransform ALedgerSettlement::TreeNearestPad() const
{
	FTransform Nearest = FTransform::Identity;
	double Best = TNumericLimits<double>::Max();
	for (const TArray<FTransform>& Variant : TreeTransforms)
	{
		for (const FTransform& Tree : Variant)
		{
			const double Distance = FVector::DistSquared(Tree.GetLocation(), PadLocation);
			if (Distance < Best)
			{
				Best = Distance;
				Nearest = Tree;
			}
		}
	}
	return Nearest;
}
