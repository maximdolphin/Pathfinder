#include "LedgerShipSystemsComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LedgerEnvironment.h"
#include "LedgerLog.h"

ULedgerShipSystemsComponent::ULedgerShipSystemsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Before the ship integrates, so the shares it reads are this frame's.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void ULedgerShipSystemsComponent::Configure(const FLedgerShipDefinition& InDefinition)
{
	Definition = InDefinition;
	State = FLedgerShipState::For(Definition);
	// ponytail: the main engine and the manoeuvring thrusters are found by the
	// ids every shipped file uses; a "role" on the component when a ship wants
	// two main engines.
	MainEngine = Definition.FindComponent(TEXT("main_engine"));
	Manoeuvring = Definition.FindComponent(TEXT("rcs"));
	Power = LedgerShipSystems::SolvePower(Definition, State);
	UE_LOG(LogLedger, Log, TEXT("ship systems: %s running, %.0f kW made for %.0f kW asked"),
		*Definition.Name, Power.SupplyKw, Power.DemandKw);
}

void ULedgerShipSystemsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsConfigured() || DeltaTime <= 0.0f)
	{
		return;
	}
	const double Step = DeltaTime;
	Power = LedgerShipSystems::SolvePower(Definition, State);
	LedgerShipSystems::StepThermal(Definition, State, Step);
	LedgerShipSystems::StepShields(Definition, State, Step);
	LedgerShipSystems::StepActuators(Definition, State, Step);
	LedgerShipSystems::FuelReactors(Definition, State, Step);
	LedgerShipSystems::Wear(Definition, State, Step);

	// The air outside is the one environment field's; in space it is nothing.
	double Outside = 0.0;
	if (const AActor* Owner = GetOwner())
	{
		const FLedgerAirHere Here = LedgerEnvironment::At(GetWorld(), Owner->GetActorLocation());
		Outside = Here.bValid ? Here.Pascals : 0.0;
	}
	LedgerShipSystems::StepAtmosphere(Definition, State, Outside, Step);
	AirWarnings = LedgerShipSystems::StepLifeSupport(Definition, State, Outside, Step);

	MainShare = (MainEngine != INDEX_NONE ? LedgerShipSystems::ThrustShare(Definition, State, MainEngine) : 1.0) * MainFuel;
	ManoeuvringShare = (Manoeuvring != INDEX_NONE ? LedgerShipSystems::ThrustShare(Definition, State, Manoeuvring) : 1.0) * ManoeuvringFuel;
}

void ULedgerShipSystemsComponent::Burned(double MainNewtons, double ManoeuvringNewtons, double DeltaSeconds)
{
	if (!IsConfigured())
	{
		return;
	}
	// The share of what was asked that the tanks could give; a dry tank reads
	// zero here, and the next frame's thrust share is zero with it.
	if (MainEngine != INDEX_NONE && MainNewtons > 0.0)
	{
		MainFuel = LedgerShipSystems::Burn(Definition, State, MainEngine, MainNewtons, DeltaSeconds);
	}
	else if (MainEngine != INDEX_NONE)
	{
		MainFuel = LedgerShipSystems::FuelKg(Definition, State, 0) > 0.0 ? 1.0 : 0.0;
	}
	if (Manoeuvring != INDEX_NONE && ManoeuvringNewtons > 0.0)
	{
		ManoeuvringFuel = LedgerShipSystems::Burn(Definition, State, Manoeuvring, ManoeuvringNewtons, DeltaSeconds);
	}
	else if (Manoeuvring != INDEX_NONE)
	{
		ManoeuvringFuel = LedgerShipSystems::FuelKg(Definition, State, 0) > 0.0 ? 1.0 : 0.0;
	}
}

double ULedgerShipSystemsComponent::MassKg() const
{
	return IsConfigured() ? LedgerShipSystems::MassProperties(Definition, State).MassKg : 0.0;
}

void ULedgerShipSystemsComponent::TakeHit(const FVector3d& PointMetres, const FVector3d& FromDirection, double EnergyJoules)
{
	if (!IsConfigured())
	{
		return;
	}
	const double Through = LedgerShipSystems::AbsorbHit(Definition, State, FromDirection, EnergyJoules);
	if (Through <= 0.0)
	{
		return;
	}
	LedgerShipSystems::HitHull(Definition, State, PointMetres, Through);
	// What gets through the armour reaches whatever is nearest: a joule past
	// the armour per ten kilojoules of condition.
	int32 Nearest = INDEX_NONE;
	double Closest = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Definition.Components.Num(); ++Index)
	{
		const double Distance = FVector3d::DistSquared(Definition.Components[Index].PositionMetres, PointMetres);
		if (Distance < Closest)
		{
			Closest = Distance;
			Nearest = Index;
		}
	}
	const double Past = FMath::Max(Through - Definition.Hull.ArmourJoules, 0.0);
	if (Nearest != INDEX_NONE && Past > 0.0)
	{
		LedgerShipSystems::Damage(Definition, State, Nearest, Past / 1.0e4 / FMath::Max(Definition.Components[Nearest].MassKg, 1.0));
	}
}
