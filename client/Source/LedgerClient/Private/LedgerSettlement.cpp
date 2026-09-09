#include "LedgerSettlement.h"

#include "LedgerMeshBuilder.h"
#include "LedgerPlanet.h"
#include "LedgerLog.h"
#include "LedgerSurface.h"
#include "LedgerTerrainMath.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "LedgerMeshBake.h"
#include "ProceduralMeshComponent.h"

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
}

ALedgerSettlement::ALedgerSettlement()
{
	PrimaryActorTick.bCanEverTick = false;

	Structures = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Structures"));
	SetRootComponent(Structures);
	// The town is small and static: one cook, at build time, is affordable and
	// means the ship can actually land on the pad.
	Structures->bUseAsyncCooking = true;

	Foliage = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Foliage"));

	// Two instanced components replace the merged foliage mesh above, which is
	// kept only so an unbaked clone still shows nothing rather than crashing.
	const TCHAR* InstanceNames[2] = { TEXT("TreesA"), TEXT("TreesB") };
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		TreeInstances[Variant] = CreateDefaultSubobject<
			UHierarchicalInstancedStaticMeshComponent>(InstanceNames[Variant]);
		TreeInstances[Variant]->SetupAttachment(Structures);
		TreeInstances[Variant]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TreeInstances[Variant]->SetCastShadow(true);
	}
	Foliage->SetupAttachment(Structures);
	Foliage->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
int32 ALedgerSettlement::DescribeTree(FLedgerMeshBuilder& Builder, bool bAltCanopy)
{
	// The file-scope colours, used directly rather than shadowed.
	constexpr double TrunkHeight = 620.0;
	constexpr double TrunkRadius = 42.0;
	constexpr double CanopyHeight = 1180.0;
	constexpr double CanopyRadius = 400.0;

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
		UE_LOG(LogLedger, Log,
			TEXT("trees %d: load %.1f ms, place %.1f ms, %d instances"),
			Variant, LoadMs, (FPlatformTime::Seconds() - PlaceStarted) * 1000.0,
			Component->GetInstanceCount());

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
	FLedgerMeshBuilder Town;
	const double BuildStarted = FPlatformTime::Seconds();
	TreeTransforms[0].Reset();
	TreeTransforms[1].Reset();

	// ---- landing pad ----------------------------------------------------
	FVector PadCentre;
	FVector PadUp;
	SurfaceFrame(SiteDirection, East, North, 0.0, 0.0, PadCentre, PadUp);

	{
		const FQuat PadRotation = FRotationMatrix::MakeFromZX(PadUp, FVector(North)).ToQuat();
		constexpr double PadHalf = 2600.0;   // 52 m square
		constexpr double PadThickness = 120.0;

		// Sunk slightly, so uneven ground under it does not show a gap at the rim.
		Town.AddBox(
			FTransform(PadRotation, PadCentre - PadUp * (PadThickness * 0.4)),
			FVector(PadHalf, PadHalf, PadThickness),
			PadColour);

		// Two stripes, so it reads as a pad rather than a slab.
		for (int32 Stripe = -1; Stripe <= 1; Stripe += 2)
		{
			Town.AddBox(
				FTransform(PadRotation, PadCentre + PadUp * (PadThickness * 0.62)
					+ FVector(East) * (Stripe * PadHalf * 0.55)),
				FVector(PadHalf * 0.78, 90.0, 16.0),
				PadStripe);
		}
	}
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

				Town.AddBox(
					FTransform(Rotation, Base + Up * (Height * 0.5)),
					FVector(Depth, Width, Height * 0.5),
					WallColours[Random.Below(5)]);

				// A flat roof cap, slightly overhanging.
				Town.AddBox(
					FTransform(Rotation, Base + Up * (Height + 60.0)),
					FVector(Depth * 1.08, Width * 1.08, 60.0),
					RoofColour);

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
	Town.Upload(Structures, 0, /*bCreateCollision*/ true);
	PlaceTrees();

	// What building the settlement costs the game thread. Measured because the
	// instancing task is judged on this number falling, and "it felt faster" is
	// not a measurement.
	UE_LOG(LogLedger, Log,
		TEXT("settlement: %d buildings, %d trees, %d instances, built in %.1f ms"),
		Placed, Grown, TreeTransforms[0].Num() + TreeTransforms[1].Num(),
		(FPlatformTime::Seconds() - BuildStarted) * 1000.0);

	if (UMaterialInterface* Material = LedgerSurface::CreateFlatMaterial(this, FLinearColor::White, 0.82f))
	{
		// White base colour: the per-vertex colours carry the variation, and a
		// tinted base would fight them.
		Structures->SetMaterial(0, Material);

		// And the instanced foliage, which needs it just as much. Dropping this
		// when the merged foliage mesh was removed left three hundred and
		// forty-two trees rendering as black cut-outs -- correctly placed,
		// correctly instanced, and lit by nothing.
		for (UHierarchicalInstancedStaticMeshComponent* Component : TreeInstances)
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
			if (UMaterialInterface* Leaf = LedgerSurface::CreateFlatMaterial(
				this, FLinearColor(Component == TreeInstances[1] ? CanopyColourAlt : CanopyColour),
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
