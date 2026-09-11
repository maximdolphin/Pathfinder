#include "LedgerGiantWorld.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerAtmosphere.h"
#include "LedgerBody.h"
#include "LedgerEphemeris.h"
#include "LedgerGas.h"
#include "LedgerGiant.h"
#include "LedgerLog.h"
#include "LedgerMeshBake.h"
#include "LedgerPlanet.h"
#include "LedgerRings.h"
#include "LedgerSettlement.h"
#include "LedgerShip.h"
#include "LedgerSky.h"
#include "LedgerSurface.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"

namespace
{
	/// A hull built to hold one atmosphere in, not fifty out. The number
	/// Ledger.Gas.TheHullFailsWhereThePredictionSaysItWill uses.
	constexpr double GiantHullLimitPascals = 50.0 * LedgerGas::OneBarPascals;

	/// How far the scene sits from the home planet's centre, cm: 300,000 km, so
	/// its terrain streams only the coarsest patches and is hidden anyway.
	const FVector GiantSceneOffset(3.0e10, 0.0, 0.0);

	constexpr int32 GlobeLongitudes = 384;
	constexpr int32 GlobeLatitudes = 192;
	constexpr int32 RingBands = 320;
	constexpr int32 RingSegments = 256;

	/// The descent's cap: cells a side, over whatever width the height sees.
	constexpr int32 CapCells = 384;

	/// The rubble lattice: 15 m cells, 300 m out and 60 m either side of the
	/// plane -- a real ring is tens of metres to a kilometre thick.
	constexpr double RubbleCellCm = 1500.0;
	constexpr int32 RubbleAcross = 20;
	constexpr int32 RubbleThick = 4;

	constexpr double GiantSettleSeconds = 4.0;

	uint32 Hash3(int32 X, int32 Y, int32 Z, uint32 Salt)
	{
		uint32 H = static_cast<uint32>(X) * 73856093u ^ static_cast<uint32>(Y) * 19349663u
			^ static_cast<uint32>(Z) * 83492791u ^ Salt * 2654435761u;
		H ^= H >> 13;
		H *= 0x5bd1e995u;
		H ^= H >> 15;
		return H;
	}

	double Unit(uint32 H)
	{
		return (H & 0xFFFFFF) / static_cast<double>(0x1000000);
	}

	/// Belt, zone and storm as a colour: warm brown belts, cream zones, a
	/// darker rust where a storm sits.
	FColor GiantColour(double Surface, double Storm, double Shadow)
	{
		const FLinearColor Belt(0.42f, 0.28f, 0.18f);
		const FLinearColor Zone(0.86f, 0.80f, 0.66f);
		const FLinearColor Rust(0.55f, 0.22f, 0.12f);
		FLinearColor Colour = FMath::Lerp(Belt, Zone, static_cast<float>(FMath::Clamp(Surface, 0.0, 1.0)));
		Colour = FMath::Lerp(Colour, Rust, static_cast<float>(FMath::Clamp(Storm, 0.0, 1.0)) * 0.8f);
		Colour *= static_cast<float>(1.0 - 0.85 * FMath::Clamp(Shadow, 0.0, 1.0));
		return Colour.ToFColor(true);
	}

	/// Adds the triangle wound so its front faces away from the centre, in the
	/// convention every mesh in this project renders by: the front normal is
	/// (P2 - P0) x (P1 - P0).
	void AddOutward(TArray<int32>& Triangles, const TArray<FVector>& Vertices, int32 A, int32 B, int32 C, const FVector& Out)
	{
		const FVector Front = FVector::CrossProduct(Vertices[C] - Vertices[A], Vertices[B] - Vertices[A]);
		if (FVector::DotProduct(Front, Out) >= 0.0)
		{
			Triangles.Append({ A, B, C });
		}
		else
		{
			Triangles.Append({ A, C, B });
		}
	}
}

bool ULedgerGiantWorld::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerGiantWorld::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerGiantWorld, STATGROUP_Tickables);
}

void ULedgerGiantWorld::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("giant"));
}

bool ULedgerGiantWorld::Build()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	const FLedgerSystem& System = Builder->GetSystem();
	Giant = LedgerBodies::FirstOfKind(System, ELedgerBodyKind::GasGiant);
	if (!System.Bodies.IsValidIndex(Giant))
	{
		UE_LOG(LogLedger, Error, TEXT("giant: the system has no gas giant"));
		return false;
	}
	const FLedgerBody& Body = System.Bodies[Giant];
	When = Builder->GetWhenSeconds();
	RadiusCm = Body.RadiusMetres * 100.0;
	if (System.Bodies.IsValidIndex(Body.ParentIndex))
	{
		HalfYear = 0.5 * LedgerEphemeris::PeriodSeconds(
			System.Bodies[Body.ParentIndex].MassKg, Body.Orbit.SemiMajorAxisMetres);
	}

	const FLedgerRings Rings = LedgerRings::For(System, Giant);
	for (int32 Step = 0; Step <= 400; ++Step)
	{
		const double R = FMath::Lerp(Rings.InnerRadiusMetres, Rings.OuterRadiusMetres, Step / 400.0);
		const double Tau = LedgerRings::OpticalDepthAt(Rings, System, R);
		if (Tau > DenseTau)
		{
			DenseTau = Tau;
			DenseRadiusCm = R * 100.0;
		}
	}

	// The descent goes down over the sharpest band boundary under the star, so
	// what resolves on the way is the edge between two belts rather than the
	// middle of one, which from 3,000 km is a uniform cream.
	{
		const FVector3d ToStar = LedgerSky::SunDirectionInBody(System, Giant, When);
		const double Longitude = FMath::Atan2(ToStar.Y, ToStar.X);
		double Sharpest = -1.0;
		for (double Degrees = -40.0; Degrees <= 40.0; Degrees += 0.25)
		{
			const double La = FMath::DegreesToRadians(Degrees);
			const double Step = FMath::Abs(LedgerGiant::SurfaceAt(La + 0.01, Longitude, Body.Seed, When)
				- LedgerGiant::SurfaceAt(La - 0.01, Longitude, Body.Seed, When));
			if (Step > Sharpest)
			{
				Sharpest = Step;
				DescentLatitude = La;
			}
		}
	}

	// The camera starts beside the densest ring; the giant's centre goes where
	// that puts it.
	Centre = GiantSceneOffset - FVector(DenseRadiusCm, 0.0, 0.0);

	// Everything built for the home planet steps aside.
	if (ALedgerPlanet* Planet = Builder->GetPlanet())
	{
		Planet->SetActorHiddenInGame(true);
		Planet->SetActorTickEnabled(false);
	}
	if (ALedgerSettlement* Town = Builder->GetSettlement())
	{
		Town->SetActorHiddenInGame(true);
	}
	if (ALedgerAtmosphere* Air = Builder->GetAtmosphere())
	{
		Air->SetActorHiddenInGame(true);
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Holder = World->SpawnActor<AActor>(AActor::StaticClass(), Params);
	Holder->SetRootComponent(NewObject<USceneComponent>(Holder, TEXT("GiantRoot")));
	Holder->GetRootComponent()->RegisterComponent();
	Holder->SetActorLocation(Centre);

	UMaterialInterface* Flat = LedgerSurface::CreateFlatMaterial(Holder, FLinearColor::White, 0.9f);

	Globe = NewObject<UProceduralMeshComponent>(Holder, TEXT("Globe"));
	Globe->SetupAttachment(Holder->GetRootComponent());
	Globe->SetCastShadow(false);
	Globe->RegisterComponent();
	PaintGlobe(When);
	Globe->SetMaterial(0, Flat);

	// ---- the ring: an annulus in the body's equator, holes where the gaps are
	Ring = NewObject<UProceduralMeshComponent>(Holder, TEXT("Ring"));
	Ring->SetupAttachment(Holder->GetRootComponent());
	Ring->SetCastShadow(false);
	Ring->RegisterComponent();
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FColor> Colours;
		const FVector3d ToStar = LedgerSky::SunDirectionInBody(System, Giant, When);
		int32 Gaps = 0;
		TArray<FVector2D> GapRuns;
		for (int32 Band = 0; Band < RingBands; ++Band)
		{
			const double R0 = FMath::Lerp(Rings.InnerRadiusMetres, Rings.OuterRadiusMetres, Band / static_cast<double>(RingBands));
			const double R1 = FMath::Lerp(Rings.InnerRadiusMetres, Rings.OuterRadiusMetres, (Band + 1) / static_cast<double>(RingBands));
			const double Mid = 0.5 * (R0 + R1);
			const double Tau = LedgerRings::OpticalDepthAt(Rings, System, Mid);
			if (LedgerRings::InGap(Rings, System, Mid) || Tau < 0.02)
			{
				++Gaps;
				// Runs of skipped bands, for the report: where the photograph should show sky.
				if (GapRuns.Num() > 0 && GapRuns.Last().Y == R0)
				{
					GapRuns.Last().Y = R1;
				}
				else
				{
					GapRuns.Add(FVector2D(R0, R1));
				}
				continue;
			}
			// Opaque, so the depth shows as brightness: against black space a thin
			// ring reads dim the way a translucent one would.
			const float Bright = static_cast<float>(1.0 - FMath::Exp(-1.5 * Tau));
			for (int32 Segment = 0; Segment < RingSegments; ++Segment)
			{
				const double A0 = 2.0 * PI * Segment / RingSegments;
				const double A1 = 2.0 * PI * (Segment + 1) / RingSegments;
				const int32 Base = Vertices.Num();
				const FVector Corners[4] = {
					FVector(FMath::Cos(A0) * R0, FMath::Sin(A0) * R0, 0.0) * 100.0,
					FVector(FMath::Cos(A1) * R0, FMath::Sin(A1) * R0, 0.0) * 100.0,
					FVector(FMath::Cos(A1) * R1, FMath::Sin(A1) * R1, 0.0) * 100.0,
					FVector(FMath::Cos(A0) * R1, FMath::Sin(A0) * R1, 0.0) * 100.0 };
				// Both sides, each with its own corners and its normal towards its own
				// side, so the face the star is on is lit and the other is not. (One
				// shared normal lit both faces from above and neither from below.)
				for (const double Side : { 1.0, -1.0 })
				{
					for (const FVector& Corner : Corners)
					{
						// In the giant's shadow where the ray to the star passes through it.
						const FVector3d P = FVector3d(Corner);
						const double Along = FVector3d::DotProduct(-P, ToStar);
						const double Miss = (P + ToStar * Along).Length();
						const bool bNight = Along > 0.0 && Miss < RadiusCm;
						FLinearColor Colour = FLinearColor(0.80f, 0.74f, 0.63f) * Bright * (bNight ? 0.12f : 1.0f);
						Vertices.Add(Corner);
						Normals.Add(FVector(0.0, 0.0, Side));
						Colours.Add(Colour.ToFColor(true));
					}
				}
				Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
				Triangles.Append({ Base + 4, Base + 6, Base + 5, Base + 4, Base + 7, Base + 6 });
			}
		}
		const TArray<FVector2D> NoUVs;
		const TArray<FProcMeshTangent> NoTangents;
		Ring->CreateMeshSection(0, Vertices, Triangles, Normals, NoUVs, Colours, NoTangents, false);
		Ring->SetMaterial(0, Flat);
		Lines.Add(FString::Printf(TEXT("rings: %.0f to %.0f km (%.2f to %.2f radii), peak optical depth %.2f; %d of %d radial bands in gaps or clear"),
			Rings.InnerRadiusMetres / 1000.0, Rings.OuterRadiusMetres / 1000.0,
			Rings.InnerRadiusMetres / Body.RadiusMetres, Rings.OuterRadiusMetres / Body.RadiusMetres,
			Rings.PeakOpticalDepth, Gaps, RingBands));
		FString Runs;
		for (const FVector2D& Run : GapRuns)
		{
			Runs += FString::Printf(TEXT(" %.0f-%.0f"), Run.X / 1000.0, Run.Y / 1000.0);
		}
		Lines.Add(FString::Printf(TEXT("rings: open (km from the centre)%s"), *Runs));
	}

	// ---- rubble in the ring plane: the ring's density resolved into pieces
	Rubble = NewObject<UInstancedStaticMeshComponent>(Holder, TEXT("Rubble"));
	Rubble->SetupAttachment(Holder->GetRootComponent());
	Rubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// No shadows: the slab would lay its whole 600 m footprint on whatever is
	// behind it as one black square.
	Rubble->SetCastShadow(false);
	Rubble->RegisterComponent();
	if (UStaticMesh* Rock = LoadObject<UStaticMesh>(nullptr,
		*FString::Printf(TEXT("%sSM_Rock_A.SM_Rock_A"), LedgerMesh::MeshPackageRoot)))
	{
		Rubble->SetStaticMesh(Rock);
		Rubble->SetMaterial(0, LedgerSurface::CreateFlatMaterial(Holder, FLinearColor(0.78f, 0.76f, 0.72f), 0.8f));
	}

	Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), GiantSceneOffset, FRotator::ZeroRotator, Params);
	if (Camera != nullptr)
	{
		// Metered on the brightest tenth of the frame -- the giant, or the rubble --
		// rather than on the black around it, which is most of every shot and
		// blew the disc out to white.
		FPostProcessSettings& Post = Camera->GetCameraComponent()->PostProcessSettings;
		Post.bOverride_AutoExposureMethod = true;
		Post.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
		Post.bOverride_AutoExposureLowPercent = true;
		Post.AutoExposureLowPercent = 88.0f;
		Post.bOverride_AutoExposureHighPercent = true;
		Post.AutoExposureHighPercent = 98.0f;
		Post.bOverride_AutoExposureBias = true;
		Post.AutoExposureBias = 1.0f;
		Post.bOverride_AutoExposureSpeedUp = true;
		Post.AutoExposureSpeedUp = 10.0f;
		Post.bOverride_AutoExposureSpeedDown = true;
		Post.AutoExposureSpeedDown = 10.0f;
		// The camera jumps between shots, and motion blur smeared the frame after.
		Post.bOverride_MotionBlurAmount = true;
		Post.MotionBlurAmount = 0.0f;
		if (APlayerController* Controller = World->GetFirstPlayerController())
		{
			Controller->SetViewTarget(Camera);
		}
	}

	const double Temperature = LedgerSky::EquilibriumTemperatureKelvin(System, Giant, When);
	PredictedCrushMetres = LedgerGas::DepthForPressure(Body, Temperature, GiantHullLimitPascals);
	Lines.Insert(FString::Printf(TEXT("giant: body %d (%s), radius %.0f km, %.2f m/s^2 at one bar, %.0f K, scale height %.1f km"),
		Giant, *Body.Name, Body.RadiusMetres / 1000.0, LedgerGas::GravityAt(Body, 0.0), Temperature,
		LedgerGas::ScaleHeightMetres(Body, Temperature) / 1000.0), 0);
	UE_LOG(LogLedger, Log, TEXT("giant: body %d, radius %.0f km, rings %.0f-%.0f km, densest at %.0f km (tau %.2f)"),
		Giant, Body.RadiusMetres / 1000.0, Rings.InnerRadiusMetres / 1000.0, Rings.OuterRadiusMetres / 1000.0,
		DenseRadiusCm / 1.0e5, DenseTau);
	return true;
}

void ULedgerGiantWorld::PaintGlobe(double Seconds, double HalfWidth)
{
	const ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>();
	const FLedgerSystem& System = Builder->GetSystem();
	const FLedgerBody& Body = System.Bodies[Giant];
	const FLedgerRings Rings = LedgerRings::For(System, Giant);

	// The whole globe, or -- for the descent -- a cap HalfWidth radians either
	// way of where the descent goes down, as a second section 100 m above the
	// first at the density the field has from close in. The globe's facets sag
	// up to 2 km inside the sphere, so the cap is always in front of them.
	const bool bCap = HalfWidth > 0.0;
	const FVector3d ToStar = LedgerSky::SunDirectionInBody(System, Giant, Seconds);
	const double MidLongitude = bCap ? FMath::Atan2(ToStar.Y, ToStar.X) : 0.0;
	const double MidLatitude = bCap ? DescentLatitude : 0.0;
	const double LatitudeSpan = bCap ? HalfWidth : 0.5 * PI;
	const double LongitudeSpan = bCap ? HalfWidth : PI;
	const int32 Rows = bCap ? CapCells : GlobeLatitudes;
	const int32 Columns = bCap ? CapCells : GlobeLongitudes;
	const double Radius = bCap ? RadiusCm + 10000.0 : RadiusCm;

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FColor> Colours;
	TArray<int32> Triangles;
	for (int32 Row = 0; Row <= Rows; ++Row)
	{
		const double Latitude = MidLatitude - LatitudeSpan + 2.0 * LatitudeSpan * Row / Rows;
		for (int32 Column = 0; Column <= Columns; ++Column)
		{
			const double Longitude = MidLongitude - LongitudeSpan + 2.0 * LongitudeSpan * Column / Columns;
			const FVector3d Direction(FMath::Cos(Latitude) * FMath::Cos(Longitude),
				FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
			Vertices.Add(FVector(Direction * Radius));
			Normals.Add(FVector(Direction));
			Colours.Add(GiantColour(
				LedgerGiant::SurfaceAt(Latitude, Longitude, Body.Seed, Seconds, bCap ? 2.0 * HalfWidth / CapCells : 0.0),
				LedgerGiant::StormAt(Latitude, Longitude, Body.Seed, Seconds),
				LedgerRings::ShadowAt(Rings, System, Direction, Seconds)));
		}
	}
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const int32 A = Row * (Columns + 1) + Column;
			const int32 B = A + 1;
			const int32 C = A + Columns + 1;
			const int32 D = C + 1;
			const FVector Out = Normals[A] + Normals[D];
			AddOutward(Triangles, Vertices, A, B, D, Out);
			AddOutward(Triangles, Vertices, A, D, C, Out);
		}
	}
	const TArray<FVector2D> NoUVs;
	const TArray<FProcMeshTangent> NoTangents;
	Globe->CreateMeshSection(bCap ? 1 : 0, Vertices, Triangles, Normals, NoUVs, Colours, NoTangents, false);
	if (bCap)
	{
		Globe->SetMaterial(1, Globe->GetMaterial(0));
	}
}

void ULedgerGiantWorld::AimSun(double Seconds)
{
	const ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>();
	const FVector3d ToStar = LedgerSky::SunDirectionInBody(Builder->GetSystem(), Giant, Seconds);
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		// The light travels away from the star: forward is minus the direction to it.
		It->SetActorRotation(FRotationMatrix::MakeFromX(-FVector(ToStar)).Rotator());
	}
}

void ULedgerGiantWorld::PlaceRubble(const FVector& Eye)
{
	if (Rubble == nullptr || Rubble->GetStaticMesh() == nullptr)
	{
		return;
	}
	const ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>();
	const FLedgerSystem& System = Builder->GetSystem();
	const FLedgerRings Rings = LedgerRings::For(System, Giant);
	const FVector Local = Eye - Centre;
	const FIntVector Cell(FMath::FloorToInt32(Local.X / RubbleCellCm), FMath::FloorToInt32(Local.Y / RubbleCellCm),
		FMath::FloorToInt32(Local.Z / RubbleCellCm));
	if (Cell == RubbleCell)
	{
		return;
	}
	RubbleCell = Cell;
	TArray<FTransform> Pieces;
	for (int32 Z = -RubbleThick; Z <= RubbleThick; ++Z)
	{
		// The ring is a sheet: the pieces are in the plane's slab, not around the eye.
		const int32 PlaneZ = Z;
		for (int32 Y = -RubbleAcross; Y <= RubbleAcross; ++Y)
		{
			for (int32 X = -RubbleAcross; X <= RubbleAcross; ++X)
			{
				const int32 CX = Cell.X + X;
				const int32 CY = Cell.Y + Y;
				const FVector Point((CX + Unit(Hash3(CX, CY, PlaneZ, 1u))) * RubbleCellCm,
					(CY + Unit(Hash3(CX, CY, PlaneZ, 2u))) * RubbleCellCm,
					(PlaneZ + Unit(Hash3(CX, CY, PlaneZ, 3u)) - 0.5) * RubbleCellCm);
				const double RadiusM = FVector2D(Point.X, Point.Y).Length() / 100.0;
				const double Tau = LedgerRings::OpticalDepthAt(Rings, System, RadiusM);
				if (Tau <= 0.0 || LedgerRings::InGap(Rings, System, RadiusM)
					|| Unit(Hash3(CX, CY, PlaneZ, 4u)) >= FMath::Min(0.8, Tau * 0.5))
				{
					continue;
				}
				const double Size = 0.2 + FMath::Pow(Unit(Hash3(CX, CY, PlaneZ, 5u)), 3.0) * 3.8;
				Pieces.Add(FTransform(FRotator(Unit(Hash3(CX, CY, PlaneZ, 6u)) * 360.0, Unit(Hash3(CX, CY, PlaneZ, 7u)) * 360.0, 0.0),
					Centre + Point, FVector(Size)));
			}
		}
	}
	Rubble->ClearInstances();
	Rubble->AddInstances(Pieces, false, true);
	RubbleNear = Pieces.Num();
	RubbleInView = 0;
	if (Camera != nullptr)
	{
		const FVector Forward = Camera->GetActorForwardVector();
		for (const FTransform& Piece : Pieces)
		{
			const FVector To = Piece.GetLocation() - Eye;
			RubbleInView += FVector::DotProduct(To.GetSafeNormal(), Forward) > 0.5 ? 1 : 0;
		}
	}
}

void ULedgerGiantWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	if (Builder == nullptr || Builder->GetPlanet() == nullptr)
	{
		return;
	}
	if (!bBuilt)
	{
		bBuilt = true;
		if (!Build())
		{
			bRunning = false;
			FPlatformMisc::RequestExit(false);
			return;
		}
	}

	const FLedgerSystem& System = Builder->GetSystem();
	const FLedgerBody& Body = System.Bodies[Giant];

	// The shots: from outside, the shadow at two seasons, through the ring plane,
	// and down towards the cloud tops.
	const double ShotTime = Shot == 2 ? When + HalfYear : When;
	const FVector3d ToStar = LedgerSky::SunDirectionInBody(System, Giant, ShotTime);
	AimSun(ShotTime);
	FVector3d Sunward = FVector3d(ToStar.X, ToStar.Y, 0.0).GetSafeNormal();
	if (Sunward.IsNearlyZero())
	{
		Sunward = FVector3d::UnitX();
	}
	const FVector3d Tangent = FVector3d::CrossProduct(FVector3d::UnitZ(), Sunward);
	const double R = RadiusCm;
	const TCHAR* Names[] = { TEXT("giant-rings.png"), TEXT("giant-ring-shadow.png"), TEXT("giant-ring-shadow-later.png"),
		TEXT("giant-ringplane-above.png"), TEXT("giant-ringplane-inside.png"), TEXT("giant-ringplane-below.png"),
		TEXT("giant-descent-high.png"), TEXT("giant-descent-cloudtops.png") };
	constexpr int32 ShotCount = UE_ARRAY_COUNT(Names);

	FVector3d Eye;
	FVector3d Target;
	const double WinterSide = ToStar.Z > 0.0 ? -1.0 : 1.0;
	switch (FMath::Min(Shot, ShotCount - 1))
	{
	case 0:
		// From the star's side of the plane: the face of the ring that is lit.
		Eye = (ToStar - FVector3d(0.0, 0.0, 0.45 * WinterSide)).GetSafeNormal() * 4.0 * R;
		Target = FVector3d::ZeroVector;
		break;
	case 1:
	case 2:
		Eye = (ToStar + FVector3d(0.0, 0.0, 0.55 * WinterSide)).GetSafeNormal() * 2.6 * R;
		Target = FVector3d::ZeroVector;
		break;
	case 3:
	case 4:
	case 5:
	{
		// Above and below: 150 m out from a slab 60 m thick, looking 60 degrees
		// into it, so all of what is seen is inside the 300 m the rubble is
		// resolved to. Inside: along the plane.
		const double Height = Shot == 3 ? 15000.0 : Shot == 4 ? 0.0 : -15000.0;
		Eye = Sunward * DenseRadiusCm + FVector3d(0.0, 0.0, Height);
		Target = Eye + Tangent * (Shot == 4 ? 1.0e6 : 8700.0) - FVector3d(0.0, 0.0, Height);
		break;
	}
	case 6:
	default:
	{
		// Looking poleward at the descent's boundary: from 3,000 km up and 2,000 km
		// short of it, then from 20 km up and 300 km short.
		const double Cos = FMath::Cos(DescentLatitude);
		const double Sin = FMath::Sin(DescentLatitude);
		const FVector3d Nadir(Cos * Sunward.X, Cos * Sunward.Y, Sin);
		const FVector3d Poleward = FVector3d(-Sin * Sunward.X, -Sin * Sunward.Y, Cos) * (DescentLatitude >= 0.0 ? 1.0 : -1.0);
		Eye = Shot == 6 ? Nadir * (R + 3.0e8) - Poleward * 2.0e8 : Nadir * (R + 2.0e6) - Poleward * 3.0e7;
		Target = Nadir * R;
		break;
	}
	}

	if (Shot == 2 && Settle == 0.0)
	{
		PaintGlobe(ShotTime);
	}
	if (Shot == 3 && Settle == 0.0)
	{
		PaintGlobe(When);
	}
	// The descent: a cap under the camera, as wide as the height sees.
	if ((Shot == 6 || Shot == 7) && Settle == 0.0)
	{
		PaintGlobe(When, Shot == 6 ? 0.12 : 0.035);
	}
	// Within the plane the rubble is the ring. The sheet, a surface of no
	// thickness, would be a floor under it.
	if (Ring != nullptr)
	{
		Ring->SetVisibility(Shot < 3 || Shot > 5);
	}

	const FVector EyeWorld = Centre + FVector(Eye);
	if (Camera != nullptr)
	{
		const FVector Up = Shot >= 6 ? FVector(Eye.GetSafeNormal()) : FVector::UpVector;
		Camera->SetActorLocationAndRotation(EyeWorld, FRotationMatrix::MakeFromXZ(FVector(Target - Eye), Up).Rotator());
	}
	APlayerController* Controller = World->GetFirstPlayerController();
	if (ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr)
	{
		Ship->SetFlightEnabled(false);
		Ship->SetActorHiddenInGame(true);
		Ship->SetActorLocation(EyeWorld + FVector(0.0, 0.0, 5.0e5));
	}
	if (Shot >= 3 && Shot <= 5)
	{
		PlaceRubble(EyeWorld);
	}
	else if (Rubble != nullptr && Rubble->GetInstanceCount() > 0)
	{
		Rubble->ClearInstances();
		RubbleCell = FIntVector(TNumericLimits<int32>::Max());
	}

	if (Shot < ShotCount)
	{
		Settle += DeltaSeconds;
		if (Settle < GiantSettleSeconds)
		{
			return;
		}
		if (!bCaptured)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectDir(), TEXT(".."), TEXT("out"), Names[Shot])), false, false);
			bCaptured = true;
			if (Shot >= 3 && Shot <= 5)
			{
				Lines.Add(FString::Printf(TEXT("ring plane, %s: %d pieces of rubble within 300 m, %d in front of the camera"),
					Names[Shot], RubbleNear, RubbleInView));
			}
			return;
		}
		++Shot;
		Settle = 0.0;
		bCaptured = false;
		return;
	}

	// ---- the descent: down from 20 km above the one-bar level until the hull gives
	const double Temperature = LedgerSky::EquilibriumTemperatureKelvin(System, Giant, When);
	if (!bDescending)
	{
		bDescending = true;
		Lines.Add(FString::Printf(TEXT("descent: predicted crush depth %.2f km below one bar (50 bar, %.0f K)"),
			PredictedCrushMetres / 1000.0, Temperature));
	}
	const double Before = LedgerGas::PressurePascals(Body, Temperature, DepthMetres);
	DepthMetres += 100.0;
	const double Pressure = LedgerGas::PressurePascals(Body, Temperature, DepthMetres);
	if (Pressure < Before)
	{
		Lines.Add(FString::Printf(TEXT("descent: the pressure FELL between %.1f and %.1f km"), (DepthMetres - 100.0) / 1000.0, DepthMetres / 1000.0));
	}
	if (FMath::IsNearlyZero(FMath::Fmod(DepthMetres, 30000.0), 1.0) && DepthMetres >= 0.0)
	{
		Lines.Add(FString::Printf(TEXT("descent: %.0f km down, %.1f bar"), DepthMetres / 1000.0, Pressure / LedgerGas::OneBarPascals));
	}
	if (Pressure >= GiantHullLimitPascals)
	{
		CrushedAtMetres = DepthMetres;
		if (ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr)
		{
			Ship->CrushHull();
			Lines.Add(FString::Printf(TEXT("descent: the hull failed at %.2f km, %.1f bar; hull integrity now %.0f%%"),
				CrushedAtMetres / 1000.0, Pressure / LedgerGas::OneBarPascals, Ship->HullIntegrity() * 100.0));
		}
		Finish();
	}
}

void ULedgerGiantWorld::Finish()
{
	const ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>();
	const FLedgerSystem& System = Builder->GetSystem();
	const FLedgerRings Rings = LedgerRings::For(System, Giant);

	// Where the ring's shadow lies on the sunward meridian, at the two times.
	auto ShadowBand = [&](double Seconds, double& Low, double& High, double& StarLatitude)
	{
		const FVector3d ToStar = LedgerSky::SunDirectionInBody(System, Giant, Seconds);
		StarLatitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(ToStar.Z, -1.0, 1.0)));
		const double Longitude = FMath::Atan2(ToStar.Y, ToStar.X);
		Low = 1000.0;
		High = -1000.0;
		for (double Latitude = -85.0; Latitude <= 85.0; Latitude += 0.25)
		{
			const double La = FMath::DegreesToRadians(Latitude);
			const FVector3d Direction(FMath::Cos(La) * FMath::Cos(Longitude), FMath::Cos(La) * FMath::Sin(Longitude), FMath::Sin(La));
			if (LedgerRings::ShadowAt(Rings, System, Direction, Seconds) > 0.05)
			{
				Low = FMath::Min(Low, Latitude);
				High = FMath::Max(High, Latitude);
			}
		}
	};
	double Low0 = 0.0, High0 = 0.0, Star0 = 0.0, Low1 = 0.0, High1 = 0.0, Star1 = 0.0;
	ShadowBand(When, Low0, High0, Star0);
	ShadowBand(When + HalfYear, Low1, High1, Star1);
	const bool bShadow0 = Low0 <= High0;
	const bool bShadow1 = Low1 <= High1;
	Lines.Add(FString::Printf(TEXT("shadow: now the star is %.1f degrees out of the ring plane and the shadow covers latitudes %s; half a year later %.1f degrees and %s"),
		Star0, bShadow0 ? *FString::Printf(TEXT("%.1f to %.1f"), Low0, High0) : TEXT("none"),
		Star1, bShadow1 ? *FString::Printf(TEXT("%.1f to %.1f"), Low1, High1) : TEXT("none")));

	// T080: rings drawn with their gaps; the ring plane resolved into rubble; the
	// shadow in the hemisphere the star is not over, at both times, and moved.
	const bool bWinter0 = bShadow0 && (Star0 > 0.0 ? High0 < 0.0 : Low0 > 0.0);
	const bool bWinter1 = bShadow1 && (Star1 > 0.0 ? High1 < 0.0 : Low1 > 0.0);
	const bool bMoved = bShadow0 && bShadow1 && FMath::Abs(0.5 * (Low0 + High0) - 0.5 * (Low1 + High1)) > 5.0;
	const bool bRubble = RubbleNear > 100;
	const bool bRings = bRubble && bWinter0 && bWinter1 && bMoved;
	const bool bCrush = CrushedAtMetres > 0.0 && FMath::Abs(CrushedAtMetres - PredictedCrushMetres) <= 100.0;
	FString Body = TEXT("The gas giant and its rings (T079, T080).\n\n");
	for (const FString& Line : Lines)
	{
		Body += Line + TEXT("\n");
	}
	Body += FString::Printf(TEXT("\nT080 rings: rubble in the plane %s, shadow on the winter side now %s and half a year later %s, moved %s -> %s\n"),
		bRubble ? TEXT("yes") : TEXT("NO"), bWinter0 ? TEXT("yes") : TEXT("NO"), bWinter1 ? TEXT("yes") : TEXT("NO"),
		bMoved ? TEXT("yes") : TEXT("NO"), bRings ? TEXT("PASS") : TEXT("FAIL"));
	Body += FString::Printf(TEXT("T079 descent: hull failed at %.2f km against a prediction of %.2f km (within the 100 m step: %s) -> %s\n"),
		CrushedAtMetres / 1000.0, PredictedCrushMetres / 1000.0, bCrush ? TEXT("yes") : TEXT("NO"), bCrush ? TEXT("PASS") : TEXT("FAIL"));
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("giant.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("giant: T080 %s, T079 %s -> %s"), bRings ? TEXT("PASS") : TEXT("FAIL"),
		bCrush ? TEXT("PASS") : TEXT("FAIL"), *Path);
	bRunning = false;
	FPlatformMisc::RequestExit(false);
}
