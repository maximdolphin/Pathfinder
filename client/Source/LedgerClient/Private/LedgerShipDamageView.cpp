#include "LedgerShipDamageView.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerShip.h"
#include "LedgerShipSystemsComponent.h"
#include "LedgerSurface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	/// The hull's own colour and finish, and what burnt looks like.
	const FLinearColor DamageHullColour(0.34f, 0.36f, 0.40f);
	const FLinearColor DamageBurntColour(0.05f, 0.045f, 0.04f);
}

bool ULedgerShipDamageView::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerShipDamageView::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerShipDamageView, STATGROUP_Tickables);
}

UPointLightComponent* ULedgerShipDamageView::Light(int32 Index)
{
	while (Lights.Num() <= Index)
	{
		UPointLightComponent* Made = NewObject<UPointLightComponent>(GetWorld());
		Made->SetMobility(EComponentMobility::Movable);
		Made->SetCastShadows(false);
		Made->SetAttenuationRadius(1500.0f);
		Made->SetIntensityUnits(ELightUnits::Candelas);
		Made->RegisterComponentWithWorld(GetWorld());
		Lights.Add(Made);
	}
	return Lights[Index];
}

UStaticMeshComponent* ULedgerShipDamageView::Plume(TArray<TObjectPtr<UStaticMeshComponent>>& Pool, int32 Index,
	const FLinearColor& Colour)
{
	while (Pool.Num() <= Index)
	{
		UStaticMeshComponent* Made = NewObject<UStaticMeshComponent>(GetWorld());
		Made->SetMobility(EComponentMobility::Movable);
		Made->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone")));
		Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Made->SetCastShadow(false);
		if (UMaterialInterface* Paint = LedgerSurface::CreateFlatMaterial(Made, Colour, 0.9f))
		{
			Made->SetMaterial(0, Paint);
		}
		Made->RegisterComponentWithWorld(GetWorld());
		Pool.Add(Made);
	}
	return Pool[Index];
}

void ULedgerShipDamageView::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	UWorld* World = GetWorld();
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	const ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;
	const ULedgerShipSystemsComponent* Systems = Ship != nullptr ? Ship->GetSystems() : nullptr;
	Fires = 0;
	Vents = 0;
	if (Systems == nullptr || !Systems->IsConfigured())
	{
		return;
	}
	const FLedgerShipDefinition& Definition = Systems->GetDefinition();
	const FLedgerShipState& State = Systems->GetState();
	const FVector Origin = Ship->GetActorLocation();
	const FRotator Facing = Ship->GetActorRotation();
	auto World3 = [&Origin, &Facing](const FVector3d& Metres) { return Origin + Facing.RotateVector(FVector(Metres * 100.0)); };

	// ---- scorch: the hull's integrity, and how battered its components are
	double Battered = 0.0;
	for (const FLedgerComponentState& Component : State.Components)
	{
		Battered += 1.0 - FMath::Clamp(Component.Condition, 0.0, 1.0);
	}
	Battered /= FMath::Max(State.Components.Num(), 1);
	LastScorch = FMath::Clamp((1.0 - State.HullIntegrity) + 0.5 * Battered, 0.0, 1.0);
	if (UProceduralMeshComponent* Hull = Ship->GetHullMesh())
	{
		if (UMaterialInstanceDynamic* Paint = Cast<UMaterialInstanceDynamic>(Hull->GetMaterial(0)))
		{
			Paint->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(DamageHullColour, DamageBurntColour, static_cast<float>(LastScorch)));
			Paint->SetScalarParameterValue(TEXT("Roughness"), FMath::Lerp(0.42f, 0.85f, static_cast<float>(LastScorch)));
		}
	}

	// ---- fire where a component is destroyed: on the hull over it, where it can
	// be seen -- a light at the component itself is inside the hull and lit
	// nothing anyone saw -- with the smoke it makes standing above it.
	const FVector Up = Facing.RotateVector(FVector::UpVector);
	const double Top = Definition.Hull.HeightMetres * 0.5;
	int32 LightIndex = 0;
	int32 FireIndex = 0;
	for (int32 Index = 0; Index < Definition.Components.Num(); ++Index)
	{
		if (State.Components[Index].Condition > 0.0)
		{
			continue;
		}
		const FVector3d Inside = Definition.Components[Index].PositionMetres;
		const FVector Surface = World3(FVector3d(Inside.X, Inside.Y, Top));
		const float Flicker = 0.6f + 0.4f * FMath::Abs(FMath::Sin(static_cast<float>(Clock) * (9.0f + Index)));

		// The flame: a cone standing on the hull, its base on the skin.
		UStaticMeshComponent* Flame = Plume(Flames, FireIndex, FLinearColor(1.0f, 0.42f, 0.08f));
		Flame->SetWorldLocation(Surface + Up * (40.0f * Flicker));
		Flame->SetWorldRotation(Facing);
		Flame->SetWorldScale3D(FVector(0.5f, 0.5f, 0.8f * Flicker));
		Flame->SetVisibility(true);

		// The smoke: a cone upside down, so it widens as it rises from the fire.
		const float Tall = 5.0f + 0.6f * static_cast<float>(FMath::Sin(Clock * 1.3 + Index));
		UStaticMeshComponent* Smoke = Plume(Smokes, FireIndex, FLinearColor(0.07f, 0.07f, 0.07f));
		Smoke->SetWorldLocation(Surface + Up * (50.0f * Tall + 60.0f));
		Smoke->SetWorldRotation(Facing.Quaternion() * FQuat(FVector::ForwardVector, PI));
		Smoke->SetWorldScale3D(FVector(1.6f, 1.6f, Tall));
		Smoke->SetVisibility(true);

		UPointLightComponent* Fire = Light(LightIndex++);
		Fire->SetWorldLocation(Surface + Up * 80.0f);
		Fire->SetLightColor(FLinearColor(1.0f, 0.45f, 0.12f));
		Fire->SetIntensity(4.0e4f * Flicker);
		Fire->SetVisibility(true);
		++FireIndex;
		++Fires;
	}

	// ---- venting where a compartment is holed and still has air to lose
	int32 PlumeIndex = 0;
	for (int32 Index = 0; Index < State.Compartments.Num() && Index < Definition.Compartments.Num(); ++Index)
	{
		const FLedgerCompartmentState& Air = State.Compartments[Index];
		if (Air.BreachM2 <= 0.0 || Air.PressurePa < 1000.0)
		{
			continue;
		}
		const FLedgerCompartment& Box = Definition.Compartments[Index];
		const FVector3d Outside((Box.MinMetres.X + Box.MaxMetres.X) * 0.5, (Box.MinMetres.Y + Box.MaxMetres.Y) * 0.5, Box.MaxMetres.Z);
		const float Strength = static_cast<float>(FMath::Clamp(Air.PressurePa / 101325.0, 0.0, 1.0));
		UStaticMeshComponent* Jet = Plume(Plumes, PlumeIndex, FLinearColor(0.85f, 0.88f, 0.92f));
		Jet->SetWorldLocation(World3(Outside + FVector3d(0.0, 0.0, 0.6)));
		Jet->SetWorldRotation(Facing);
		Jet->SetWorldScale3D(FVector(0.6f, 0.6f, 1.2f + 2.5f * Strength));
		Jet->SetVisibility(true);
		UPointLightComponent* Glow = Light(LightIndex++);
		Glow->SetWorldLocation(World3(Outside));
		Glow->SetLightColor(FLinearColor(0.7f, 0.8f, 1.0f));
		Glow->SetIntensity(1.5e4f * Strength);
		Glow->SetVisibility(true);
		++PlumeIndex;
		++Vents;
	}

	for (int32 Index = LightIndex; Index < Lights.Num(); ++Index)
	{
		Lights[Index]->SetVisibility(false);
	}
	for (int32 Index = PlumeIndex; Index < Plumes.Num(); ++Index)
	{
		Plumes[Index]->SetVisibility(false);
	}
	for (int32 Index = FireIndex; Index < Flames.Num(); ++Index)
	{
		Flames[Index]->SetVisibility(false);
	}
	for (int32 Index = FireIndex; Index < Smokes.Num(); ++Index)
	{
		Smokes[Index]->SetVisibility(false);
	}
}
