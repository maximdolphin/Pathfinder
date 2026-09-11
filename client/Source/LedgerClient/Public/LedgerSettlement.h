// A town. Design §3 scale for MVP is "one star system, three to five
// planets/moons" — this is the ground truth underneath one dot on that map.
//
// Everything here is placed on a *sphere*, which is the only thing that makes it
// harder than scattering props on a plane: there is no global up, so every
// building has its own, and a street laid out in a straight line in the tangent
// plane bends over the horizon. Positions are therefore built in a local tangent
// basis and projected back onto the surface height function — the same function
// the terrain mesh comes from, so nothing floats and nothing sinks.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LedgerSettlement.generated.h"

struct FLedgerMeshBuilder;

class ALedgerPlanet;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;

UCLASS()
class LEDGERCLIENT_API ALedgerSettlement : public AActor
{
	GENERATED_BODY()

public:
	ALedgerSettlement();

	/// Builds the town around a direction on the planet. Returns the world
	/// location of the landing pad, which is where the ship wants to arrive.
	FVector Build(ALedgerPlanet* InPlanet, const FVector3d& SiteDirection, uint32 InSeed);

	UPROPERTY(EditAnywhere, Category = "Ledger|Town")
	int32 BuildingCount = 34;

	UPROPERTY(EditAnywhere, Category = "Ledger|Town")
	int32 TreeCount = 900;

	/// Radius of the built-up area, in centimetres. 400 m across.
	UPROPERTY(EditAnywhere, Category = "Ledger|Town")
	double TownRadius = 20000.0;

	/// Trees scatter out to here.
	UPROPERTY(EditAnywhere, Category = "Ledger|Town")
	double TreeRadius = 90000.0;

	FVector GetPadLocation() const { return PadLocation; }

	virtual void Tick(float DeltaSeconds) override;

	/// Where the vane points, degrees clockwise from north: the way the air
	/// is going. T093's settlement consumer.
	double VaneBearingDegrees() const { return VaneBearing; }

	/// The wind the vane last read, metres per second in world space, and
	/// where it read it. The proof compares this against the field.
	FVector3d VaneWindMetres() const { return LastVaneWind; }
	FVector VaneLocation() const;

public:
	/// One tree at the origin, unit scale, +Z up. Static, because the mesh bake
	/// runs before any settlement exists. `bAltCanopy` picks the lower cone's
	/// colour, which is the only variation between the two baked trees.
	/// Returns the triangle index where the canopy begins, so the mesh bake
	/// can give the trunk and the canopy a material slot each.
	static int32 DescribeTree(FLedgerMeshBuilder& Builder, bool bAltCanopy);

	/// One building at unit size: a hundred-centimetre box standing on the
	/// origin, with a roof cap overhanging it by four per cent. Every building
	/// in the town is this mesh, scaled per instance, so the walls are slot 0
	/// and the roof slot 1 and the colour is the material's. Returns where the
	/// roof starts, in triangles.
	static int32 DescribeBuilding(FLedgerMeshBuilder& Builder);

	/// The landing pad at its real size, centred on the origin with +Z up:
	/// the slab in slot 0 and its two stripes in slot 1. Returns where the
	/// stripes start, in triangles.
	static int32 DescribePad(FLedgerMeshBuilder& Builder);

	/// How many wall colours the town has, and so how many instanced building
	/// components: an instance cannot carry its own colour through the flat
	/// material, so a colour is a component.
	static constexpr int32 WallVariants = 5;

private:
	void PlaceTrees();

	/// Loads the baked building and pad meshes and hands them their instances.
	void PlaceBuildings(const FTransform& PadTransform);

	/// Where trees go, gathered during placement and handed to the instanced
	/// components in one call. Split by canopy variant.
	TArray<FTransform> TreeTransforms[2];

	UPROPERTY()
	TObjectPtr<class UHierarchicalInstancedStaticMeshComponent> TreeInstances[2];

	/// Where the town hangs. A plain scene component: nothing in the town is
	/// generated geometry any more, so there is nothing for a root to draw.
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	/// The buildings, one instanced component per wall colour, drawing the
	/// baked SM_Building. ADR-0006: this was a procedural mesh rebuilt on the
	/// game thread each launch, with no Nanite, no distance field for Lumen and
	/// no instancing.
	UPROPERTY()
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Buildings[WallVariants];

	/// The landing pad, the baked SM_Pad. It carries collision, because the
	/// ship lands on it.
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Pad;

	/// **A wind vane at the pad.** The one thing on the town the wind moves,
	/// and so the settlement layer's reading of the one wind: a pole and a
	/// streamer, both the baked building box scaled, turned each tick to
	/// stream downwind from whatever ULedgerWind says is blowing where it
	/// stands. The same number the ship's drag, the grass, the rain and the
	/// audio are reading.
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> VanePole;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Vane;

	FVector3d VaneUp = FVector3d::UnitZ();
	FVector3d VaneNorth = FVector3d::UnitY();
	FVector3d VaneEast = FVector3d::UnitX();
	FVector VaneTop = FVector::ZeroVector;
	double VaneBearing = 0.0;
	FVector3d LastVaneWind = FVector3d::ZeroVector;

	/// Where buildings go, gathered during layout and handed over in one call.
	TArray<FTransform> BuildingTransforms[WallVariants];

	UPROPERTY()
	TObjectPtr<ALedgerPlanet> Planet;

	FVector PadLocation = FVector::ZeroVector;

	/// Surface point and local frame for a tangent-plane offset from the site.
	/// This is the one piece of maths the whole file depends on: a town laid out
	/// in flat coordinates has to be bent back onto the sphere, or it drifts
	/// off the ground within a few hundred metres.
	void SurfaceFrame(
		const FVector3d& SiteDirection,
		const FVector3d& East,
		const FVector3d& North,
		double OffsetEast,
		double OffsetNorth,
		FVector& OutLocation,
		FVector& OutUp) const;
};
