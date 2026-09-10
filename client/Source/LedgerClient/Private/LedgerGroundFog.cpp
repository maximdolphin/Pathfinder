#include "LedgerGroundFog.h"

#include "Components/LocalFogVolumeComponent.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"

namespace
{
	/// How far across the fog is laid, metres, and how many cells that is
	/// divided into. Twelve kilometres is further than anything is visible
	/// through fog, and a kilometre a cell is finer than the terrain features
	/// that decide where it pools.
	constexpr double GroundFogExtentMetres = 12000.0;

	/// **Six cells across, not twelve.**
	///
	/// The renderer draws a bounded number of local fog volumes per view and
	/// drops the rest, and a hundred and forty-four cells of a kilometre each
	/// put every volume beyond that budget when the camera is on a ridge twelve
	/// kilometres away -- the fog was there in the model, present in the log,
	/// and identical to the no-fog control in the photograph. Thirty-six cells
	/// of two kilometres is inside the budget and each one is large enough to
	/// read at that distance.
	constexpr int32 GroundFogCells = 6;

	/// A local fog volume is a unit sphere of this many units before scaling.
	constexpr double GroundFogBaseSize = 500.0;

	constexpr double GroundFogCentimetresPerMetre = 100.0;
}

ALedgerGroundFog::ALedgerGroundFog()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

int32 ALedgerGroundFog::Configure(
	ALedgerPlanet* Planet, const FVector3d& AnchorDirection, const FLedgerFog& Fog)
{
	for (ULocalFogVolumeComponent* Volume : Volumes)
	{
		if (Volume != nullptr)
		{
			Volume->DestroyComponent();
		}
	}
	Volumes.Reset();

	if (Planet == nullptr || !Fog.bForms || !(Fog.DepthMetres > 0.0))
	{
		return 0;
	}

	const FVector3d Up = AnchorDirection.GetSafeNormal();
	// A pair of axes along the ground at the anchor, so the grid is laid on the
	// surface rather than on a plane that happens to be near it.
	FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up);
	if (East.IsNearlyZero())
	{
		East = FVector3d::CrossProduct(FVector3d::UnitX(), Up);
	}
	East.Normalize();
	const FVector3d North = FVector3d::CrossProduct(Up, East).GetSafeNormal();

	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	const double RadiusMetres = Planet->Radius / GroundFogCentimetresPerMetre;
	const double Step = GroundFogExtentMetres / GroundFogCells;

	// **Sample first, decide the level, then fill.** Two passes, because the
	// whole point is that the top is one number for the whole area -- a fog
	// that found its own level per cell would be a fog that draped the
	// mountains, which is the thing this exists not to do.
	TArray<FVector3d> Directions;
	TArray<double> Ground;
	Directions.Reserve(GroundFogCells * GroundFogCells);
	Ground.Reserve(GroundFogCells * GroundFogCells);

	ValleyFloorMetres = TNumericLimits<double>::Max();
	for (int32 Y = 0; Y < GroundFogCells; ++Y)
	{
		for (int32 X = 0; X < GroundFogCells; ++X)
		{
			const double Across = (X - (GroundFogCells - 1) * 0.5) * Step;
			const double Along = (Y - (GroundFogCells - 1) * 0.5) * Step;
			const FVector3d Direction =
				(Up * RadiusMetres + East * Across + North * Along).GetSafeNormal();
			const double Surface =
				Planet->SurfaceRadiusAt(Direction) / GroundFogCentimetresPerMetre;
			Directions.Add(Direction);
			Ground.Add(Surface);
			ValleyFloorMetres = FMath::Min(ValleyFloorMetres, Surface);
		}
	}

	FilledToMetres = ValleyFloorMetres + Fog.DepthMetres;

	int32 Filled = 0;
	for (int32 Index = 0; Index < Directions.Num(); ++Index)
	{
		const double Surface = Ground[Index];
		if (Surface >= FilledToMetres)
		{
			// This cell's ground is above the level. No fog, and that is the
			// ridge in the acceptance.
			continue;
		}

		const double Deep = FilledToMetres - Surface;
		const double MiddleMetres = Surface + Deep * 0.5;

		ULocalFogVolumeComponent* Volume = NewObject<ULocalFogVolumeComponent>(this);
		Volume->SetupAttachment(GetRootComponent());
		Volume->SetMobility(EComponentMobility::Movable);
		Volume->RegisterComponent();

		Volume->SetWorldLocation(FVector(
			Centre + Directions[Index] * (MiddleMetres * GroundFogCentimetresPerMetre)));
		// The sphere is squashed to the shape of the pool in this cell: a
		// kilometre across and a hundred metres tall is a flat lens, which is
		// what a fog bank is.
		Volume->SetWorldScale3D(FVector(
			Step * 0.8 * GroundFogCentimetresPerMetre / GroundFogBaseSize,
			Step * 0.8 * GroundFogCentimetresPerMetre / GroundFogBaseSize,
			Deep * 0.5 * GroundFogCentimetresPerMetre / GroundFogBaseSize));

		// **The component's density is per unit sphere, not per metre.** A local
		// fog volume is a unit sphere scaled by the transform, and its
		// extinction is quoted at the centre of that unit sphere -- so the
		// number to hand it is the optical depth across the pool, tau = k * d,
		// and not k itself. Handing it k (0.008) produced a fog nobody could
		// photograph; the same scene with the fog switched off was
		// indistinguishable, which is how the units error was found rather than
		// argued about.
		//
		// Both terms get it. The radial one is what gives the bank a soft edge;
		// setting it to zero, as the first version did, removes the volume's
		// coverage almost entirely whatever the height term says.
		const float OpticalDepth = static_cast<float>(
			FMath::Clamp(Fog.ExtinctionPerMetre * Fog.Fraction * Deep, 0.0, 8.0));
		Volume->SetRadialFogExtinction(OpticalDepth);
		Volume->SetHeightFogExtinction(OpticalDepth);
		// **A sharp top, because the top is the visible thing about a fog bank.**
		//
		// The component's falloff runs backwards from what its name suggests: a
		// large number is a *thin* transition. Flattening it to 4 to make the
		// pool uniform -- which physically it nearly is -- turned the bank into
		// a lit slab that occluded the sky from inside it. At 120 the density
		// is concentrated in the bottom of each cell and the result reads as a
		// fog bank both from above and from within it, which is what this is
		// for. The physical profile of a cold pool is a thing T099 can model
		// properly when there is a temperature field to hang it on.
		Volume->SetHeightFogFalloff(120.0f);
		Volume->SetHeightFogOffset(0.0f);
		Volume->SetFogPhaseG(0.35f);
		Volume->SetFogAlbedo(FLinearColor(0.92f, 0.94f, 0.97f));

		Volumes.Add(Volume);
		++Filled;
	}

	UE_LOG(LogLedger, Log,
		TEXT("ground fog: valley floor %.0f m, filled to %.0f m (%.0f m deep), "
			 "%d of %d cells, %.0f%% remaining, visibility %.0f m"),
		ValleyFloorMetres - Planet->Radius / GroundFogCentimetresPerMetre,
		FilledToMetres - Planet->Radius / GroundFogCentimetresPerMetre,
		Fog.DepthMetres, Filled, Directions.Num(), Fog.Fraction * 100.0,
		3.0 / FMath::Max(Fog.ExtinctionPerMetre * Fog.Fraction, 1e-9));
	return Filled;
}
