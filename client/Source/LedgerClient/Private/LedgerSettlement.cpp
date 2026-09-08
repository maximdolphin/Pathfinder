#include "LedgerSettlement.h"

#include "LedgerMeshBuilder.h"
#include "LedgerPlanet.h"
#include "LedgerSimSubsystem.h"
#include "LedgerSurface.h"
#include "LedgerTerrainMath.h"
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
	FLedgerMeshBuilder Trees;

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
			const double TrunkHeight = 620.0 * Scale;
			const double TrunkRadius = 42.0 * Scale;
			const double CanopyHeight = 1180.0 * Scale;
			const double CanopyRadius = 400.0 * Scale;

			// Sunk so the trunk meets the ground on a slope.
			const FTransform Base(Rotation, Location - Up * 90.0);
			Trees.AddCylinder(Base, TrunkRadius, TrunkRadius * 0.72, TrunkHeight, 6, TrunkColour);

			const FTransform CanopyBase(Rotation, Location + Up * (TrunkHeight * 0.55));
			Trees.AddCone(
				CanopyBase, CanopyRadius, CanopyHeight, 7,
				Random.Unit() < 0.5 ? CanopyColour : CanopyColourAlt);
			// A second, smaller cone above so the silhouette is not a single
			// pyramid repeated nine hundred times.
			Trees.AddCone(
				FTransform(Rotation, Location + Up * (TrunkHeight * 0.55 + CanopyHeight * 0.42)),
				CanopyRadius * 0.66, CanopyHeight * 0.7, 7,
				CanopyColourAlt);

			++Grown;
		}
	}

	SetActorLocation(FVector::ZeroVector);
	Town.Upload(Structures, 0, /*bCreateCollision*/ true);
	Trees.Upload(Foliage, 0, /*bCreateCollision*/ false);

	if (UMaterialInterface* Material = LedgerSurface::CreateFlatMaterial(this, FLinearColor::White, 0.82f))
	{
		// White base colour: the per-vertex colours carry the variation, and a
		// tinted base would fight them.
		Structures->SetMaterial(0, Material);
		Foliage->SetMaterial(0, Material);
	}

	UE_LOG(LogLedger, Log, TEXT("settlement: %d buildings, %d trees, pad at %s"),
		Placed, Grown, *PadLocation.ToCompactString());

	return PadLocation;
}
