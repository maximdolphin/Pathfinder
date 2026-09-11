#include "LedgerGroundFog.h"

#include "Components/LocalFogVolumeComponent.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "Misc/CommandLine.h"

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

	// **One volume, with its top at the fill level.**
	//
	// A local fog volume has a rotation and ONE scale -- LocalFogVolumeCommon.ush
	// unpacks a single UniformScale -- so the flat lenses this used to lay, 3.2 km
	// across and 86 m tall, were each a sphere 3.2 km in radius, reaching three
	// kilometres up through the lookout. That was the opaque orange frame every
	// version of this produced with the height term on, and with it off the fog
	// was a faint haze because the radial term is all that was left.
	//
	// A cold pool has a level top, and the component can give that directly:
	// its height term is Extinction * exp(-Falloff * (z - Offset)) in the unit
	// sphere's own up. So: radial term off, and the Offset put at the fill level.
	// Below it, fog; above it, clear air; and the ground that stands above the
	// level -- the ridge -- stands out of the fog on its own.
	//
	// **The top sits low in the sphere, because the bottom must stay a number.**
	// The density grows as exp(Falloff * (Offset - z)) below the top and is
	// largest at z = -1; a float stops at e^88, and e^120 was the NaN that
	// poisoned the whole view. With the top at z = -0.8 the bottom is only 0.2 of
	// the radius below it, so a falloff of 400 -- an e-fold every ~35 m, a sharp
	// top -- puts e^80 there. The sphere is sized so the circle where it meets the
	// level (0.6 of its radius) still covers the whole sampled square.
	constexpr double TopInUnits = -0.8;
	constexpr double Falloff = 400.0;
	const double PoolRadiusMetres = GroundFogExtentMetres * 0.71 / FMath::Sqrt(1.0 - TopInUnits * TopInUnits);
	const double PerMetre = Fog.ExtinctionPerMetre * Fog.Fraction;

	ULocalFogVolumeComponent* Volume = NewObject<ULocalFogVolumeComponent>(this);
	Volume->SetupAttachment(GetRootComponent());
	Volume->SetMobility(EComponentMobility::Movable);
	Volume->RegisterComponent();
	Volume->SetWorldLocation(FVector(Centre
		+ Up * ((FilledToMetres - TopInUnits * PoolRadiusMetres) * GroundFogCentimetresPerMetre)));
	Volume->SetWorldRotation(FRotationMatrix::MakeFromZ(FVector(Up)).Rotator());
	Volume->SetWorldScale3D(FVector(PoolRadiusMetres * GroundFogCentimetresPerMetre / GroundFogBaseSize));

	// Per unit of the sphere's radius: the shader integrates optical depth in the
	// unit sphere's own space, so a rate per metre becomes that rate times the
	// radius in metres. It is packed as an 11-bit float, whose top is 65,024.
	const float UnitExtinction = static_cast<float>(
		FMath::Clamp(PerMetre * PoolRadiusMetres, 0.0, 60000.0));
	Volume->SetRadialFogExtinction(0.0f);
	Volume->SetHeightFogExtinction(UnitExtinction);
	Volume->SetHeightFogFalloff(static_cast<float>(Falloff));
	Volume->SetHeightFogOffset(static_cast<float>(TopInUnits));
	Volume->SetFogPhaseG(0.35f);
	Volume->SetFogAlbedo(FLinearColor(0.92f, 0.94f, 0.97f));
	Volumes.Add(Volume);

	int32 Below = 0;
	for (const double Surface : Ground)
	{
		Below += Surface < FilledToMetres ? 1 : 0;
	}
	UE_LOG(LogLedger, Log,
		TEXT("ground fog: valley floor %.0f m, filled to %.0f m (%.0f m deep); one volume %.1f km in "
			 "radius, top %.0f m above its centre, %.0f per unit radius, an e-fold every %.0f m above "
			 "the top; %d of %d sampled cells under the level, %.0f%% remaining, visibility %.0f m"),
		ValleyFloorMetres - Planet->Radius / GroundFogCentimetresPerMetre,
		FilledToMetres - Planet->Radius / GroundFogCentimetresPerMetre,
		Fog.DepthMetres, PoolRadiusMetres / 1000.0, TopInUnits * PoolRadiusMetres, UnitExtinction,
		PoolRadiusMetres / Falloff, Below, Ground.Num(), Fog.Fraction * 100.0,
		3.0 / FMath::Max(PerMetre, 1e-9));
	return Below;
}
