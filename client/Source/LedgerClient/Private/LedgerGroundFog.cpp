#include "LedgerGroundFog.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "LedgerAtmosphere.h"
#include "LedgerSurface.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	// How far volumetric fog reaches: past the ridge, and nowhere near orbit.
	constexpr double GroundFogVolumetricMetres = 20000.0;
}

ALedgerGroundFog::ALedgerGroundFog()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

int32 ALedgerGroundFog::Configure(
	ALedgerPlanet* Planet, const FVector3d& AnchorDirection, const FLedgerFog& Fog)
{
	for (UStaticMeshComponent* Volume : Volumes)
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

	// **One box, with its top at the fill level.** T091.
	//
	// A local fog volume is a sphere whose height term is an exponential, and
	// this pool is not a shape it has -- see LedgerGroundFogMaterial.cpp. A box
	// voxelised into volumetric fog is: density up to the level, nothing above,
	// soft sides. It ends at its own floor, so no ground lower than the valley
	// meets an exponential grown under it, and volumetric fog ends at its own
	// distance, which is what keeps the pool out of the view from orbit.
	constexpr double SoftMetres = 10.0;
	constexpr double FloorMarginMetres = 50.0;
	constexpr float EdgeFraction = 0.15f;
	const double BottomMetres = ValleyFloorMetres - FloorMarginMetres;
	const double TopOfBoxMetres = FilledToMetres + 4.0 * SoftMetres;
	const double HeightMetres = TopOfBoxMetres - BottomMetres;
	const double WidthMetres = GroundFogExtentMetres / (1.0 - 2.0 * EdgeFraction);

	// `-fogscale=N` multiplies the density, for looking at the fog rather than
	// the model.
	double FogScale = 1.0;
	FParse::Value(FCommandLine::Get(), TEXT("fogscale="), FogScale);
	const double PerMetre = Fog.ExtinctionPerMetre * Fog.Fraction * FogScale;

	UMaterialInstanceDynamic* Material = nullptr;
	if (UMaterialInterface* Parent = LedgerSurface::CreateGroundFogMaterial(this))
	{
		Material = UMaterialInstanceDynamic::Create(Parent, this);
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Material == nullptr || Cube == nullptr)
	{
		UE_LOG(LogLedger, Warning, TEXT("ground fog: no %s, so no fog"),
			Material == nullptr ? TEXT("material") : TEXT("cube mesh"));
		return 0;
	}
	Material->SetScalarParameterValue(TEXT("Density"), static_cast<float>(PerMetre));
	Material->SetScalarParameterValue(TEXT("TopFraction"),
		static_cast<float>((FilledToMetres - BottomMetres) / HeightMetres));
	Material->SetScalarParameterValue(TEXT("SoftFraction"), static_cast<float>(SoftMetres / HeightMetres));
	Material->SetScalarParameterValue(TEXT("EdgeFraction"), EdgeFraction);

	UStaticMeshComponent* Box = NewObject<UStaticMeshComponent>(this);
	Box->SetupAttachment(GetRootComponent());
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetStaticMesh(Cube);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetCastShadow(false);
	Box->bAffectDistanceFieldLighting = false;
	Box->RegisterComponent();
	Box->SetMaterial(0, Material);
	Box->SetWorldLocation(FVector(Centre
		+ Up * ((BottomMetres + TopOfBoxMetres) * 0.5 * GroundFogCentimetresPerMetre)));
	Box->SetWorldRotation(FRotationMatrix::MakeFromZ(FVector(Up)).Rotator());
	// The engine's cube is a metre on a side, so the scale is the size in metres.
	Box->SetWorldScale3D(FVector(WidthMetres, WidthMetres, HeightMetres));
	Volumes.Add(Box);

	// Volumetric fog is switched on by Tick, while the camera is near.
	int32 Atmospheres = 0;
	for (TActorIterator<ALedgerAtmosphere> It(GetWorld()); It; ++It)
	{
		Atmosphere = *It;
		++Atmospheres;
	}
	PoolCentre = Box->GetComponentLocation();
	bVolumetricOn = false;

	int32 Below = 0;
	for (const double Surface : Ground)
	{
		Below += Surface < FilledToMetres ? 1 : 0;
	}
	UE_LOG(LogLedger, Log,
		TEXT("ground fog: valley floor %.0f m, filled to %.0f m (%.0f m deep); a box %.1f km wide from "
			"%.0f to %.0f m, %.4f per metre below the level, softened over %.0f m; %d of %d sampled cells "
			"under the level, %.0f%% remaining, visibility %.0f m; volumetric fog to %.0f km on %d atmosphere(s)"),
		ValleyFloorMetres - Planet->Radius / GroundFogCentimetresPerMetre,
		FilledToMetres - Planet->Radius / GroundFogCentimetresPerMetre,
		Fog.DepthMetres, WidthMetres / 1000.0,
		BottomMetres - Planet->Radius / GroundFogCentimetresPerMetre,
		TopOfBoxMetres - Planet->Radius / GroundFogCentimetresPerMetre,
		PerMetre, SoftMetres, Below, Ground.Num(), Fog.Fraction * 100.0,
		3.0 / FMath::Max(PerMetre, 1e-9), GroundFogVolumetricMetres / 1000.0, Atmospheres);
	return Below;
}

void ALedgerGroundFog::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ALedgerAtmosphere* Air = Atmosphere.Get();
	const APlayerController* Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Air == nullptr || Volumes.Num() == 0 || Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		return;
	}
	// Three times the distance volumetric fog reaches: close enough that the
	// froxels are there before the pool is in them, far short of orbit.
	const bool bNear = FVector::Dist(Controller->PlayerCameraManager->GetCameraLocation(), PoolCentre)
		< 3.0 * GroundFogVolumetricMetres * GroundFogCentimetresPerMetre;
	if (bNear != bVolumetricOn)
	{
		bVolumetricOn = bNear;
		if (bNear)
		{
			Air->UseVolumetricFogOnly(GroundFogVolumetricMetres);
		}
		else
		{
			Air->StopVolumetricFog();
		}
		UE_LOG(LogLedger, Log, TEXT("ground fog: volumetric fog %s"), bNear ? TEXT("on, camera near the pool") : TEXT("off, camera far from the pool"));
	}
}
