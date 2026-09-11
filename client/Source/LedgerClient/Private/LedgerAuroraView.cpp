#include "LedgerAuroraView.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "LedgerAir.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerSurface.h"
#include "LedgerWorld.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/// Where the green line glows, metres up: a hundred to a hundred and twenty
	/// kilometres, where oxygen is thin enough to stay excited long enough.
	constexpr double AuroraHeightMetres = 110000.0;

	/// The engine's sphere is a metre across.
	constexpr double AuroraSphereRadiusCm = 50.0;
}

bool ULedgerAuroraView::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerAuroraView::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerAuroraView, STATGROUP_Tickables);
}

void ULedgerAuroraView::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Controller == nullptr || Planet == nullptr || DeltaSeconds <= 0.0f)
	{
		return;
	}
	const FLedgerSystem& System = Builder->GetSystem();
	const int32 Home = Builder->GetHomeBodyIndex();
	const double When = Builder->GetWhenSeconds();
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());

	if (Shell == nullptr)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* Base = LedgerSurface::CreateAuroraMaterial(this);
		if (Sphere == nullptr || Base == nullptr)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Holder = World->SpawnActor<AActor>(AActor::StaticClass(), FVector(Centre), FRotator::ZeroRotator, Params);
		Shell = NewObject<UStaticMeshComponent>(Holder);
		Shell->SetStaticMesh(Sphere);
		Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shell->SetCastShadow(false);
		Material = UMaterialInstanceDynamic::Create(Base, this);
		Shell->SetMaterial(0, Material);
		Holder->SetRootComponent(Shell);
		Shell->RegisterComponent();
		const double Scale = (Planet->Radius + AuroraHeightMetres * 100.0) / AuroraSphereRadiusCm;
		Shell->SetWorldLocationAndRotation(FVector(Centre), FRotator::ZeroRotator);
		Shell->SetWorldScale3D(FVector(Scale));
		// A shell six and a half thousand kilometres across is never outside
		// the view frustum's reach in any way culling should decide.
		Shell->SetBoundsScale(1.0f);
	}

	FVector Eye = FVector::ZeroVector;
	FRotator Ignored = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(Eye, Ignored);
	const FVector3d Up = (FVector3d(Eye) - Centre).GetSafeNormal();
	Last = LedgerAurora::At(System, Home, LedgerAir::For(System, Home, When),
		FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0)), FMath::Atan2(Up.Y, Up.X), When);

	if (Material != nullptr)
	{
		const FVector3d Pole = LedgerAurora::MagneticPole(System, Home);
		const FVector3d FromCamera = Centre - FVector3d(Eye);
		Material->SetVectorParameterValue(TEXT("PlanetFromCamera"),
			FLinearColor(static_cast<float>(FromCamera.X), static_cast<float>(FromCamera.Y), static_cast<float>(FromCamera.Z), 0.0f));
		Material->SetVectorParameterValue(TEXT("AuroraPole"),
			FLinearColor(static_cast<float>(Pole.X), static_cast<float>(Pole.Y), static_cast<float>(Pole.Z), 0.0f));
		Material->SetScalarParameterValue(TEXT("AuroraSinOval"), static_cast<float>(FMath::Sin(Last.OvalMagneticLatitude)));
		Material->SetScalarParameterValue(TEXT("AuroraSinWidth"),
			static_cast<float>(FMath::Cos(Last.OvalMagneticLatitude) * Last.OvalHalfWidth));
		Material->SetScalarParameterValue(TEXT("AuroraStrength"), static_cast<float>(Last.Strength));
	}
	if (Shell != nullptr)
	{
		Shell->SetVisibility(Last.Strength > 0.0);
	}

	if (FMath::Abs(Last.Kp - LastLoggedKp) > 0.5)
	{
		LastLoggedKp = Last.Kp;
		UE_LOG(LogLedger, Log, TEXT("aurora: Kp %.1f%s, oval at %.1f degrees magnetic, strength %.2f, %.2f overhead"),
			Last.Kp, Last.bEvent ? TEXT(" (a solar event)") : TEXT(""),
			FMath::RadiansToDegrees(Last.OvalMagneticLatitude), Last.Strength, Last.Overhead);
	}
}
