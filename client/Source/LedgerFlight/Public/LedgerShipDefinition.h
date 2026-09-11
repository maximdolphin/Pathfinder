// A ship is data. T108 and T109.
//
// Hull, flight numbers, components and the ports that join them, read from one
// JSON file in Config/Ships. Adding a ship is adding a file: nothing here is
// compiled per ship. The component graph is the spine of M05 -- power, coolant,
// fuel and data flow along its connections, and damage, heat and failure will
// propagate along the same ones.

#pragma once

#include "CoreMinimal.h"

/// What flows through a port.
enum class ELedgerPortKind : uint8
{
	Power,
	Coolant,
	Fuel,
	Data,
};

LEDGERFLIGHT_API const TCHAR* LexToString(ELedgerPortKind Kind);

struct LEDGERFLIGHT_API FLedgerPort
{
	FString Name;
	ELedgerPortKind Kind = ELedgerPortKind::Power;
	bool bOutput = false;

	/// An input the component cannot work without. Left unconnected it is a
	/// load error, not a component that silently does nothing.
	bool bRequired = false;
};

struct LEDGERFLIGHT_API FLedgerComponent
{
	FString Id;

	/// What it is: "PowerPlant", "Bus", "Thruster", "FuelTank" and so on. The
	/// systems that give a type its behaviour arrive task by task.
	FString Type;

	double MassKg = 0.0;

	/// Where it sits, metres, in the ship's frame: x forward, y right, z up.
	FVector3d PositionMetres = FVector3d::ZeroVector;

	TArray<FLedgerPort> Ports;
	TMap<FString, double> Params;

	int32 FindPort(const FString& Name) const;
	double Param(const FString& Key, double Default = 0.0) const;
};

/// One output joined to one input.
struct LEDGERFLIGHT_API FLedgerConnection
{
	int32 FromComponent = INDEX_NONE;
	int32 FromPort = INDEX_NONE;
	int32 ToComponent = INDEX_NONE;
	int32 ToPort = INDEX_NONE;
	ELedgerPortKind Kind = ELedgerPortKind::Power;
};

/// A sealed space inside the hull: a box in the ship's frame. T115.
struct LEDGERFLIGHT_API FLedgerCompartment
{
	FString Id;
	FVector3d MinMetres = FVector3d::ZeroVector;
	FVector3d MaxMetres = FVector3d::ZeroVector;

	double VolumeM3() const
	{
		const FVector3d Size = MaxMetres - MinMetres;
		return FMath::Max(Size.X * Size.Y * Size.Z, 0.0);
	}

	bool Contains(const FVector3d& Point) const
	{
		return Point.X >= MinMetres.X && Point.X <= MaxMetres.X
			&& Point.Y >= MinMetres.Y && Point.Y <= MaxMetres.Y
			&& Point.Z >= MinMetres.Z && Point.Z <= MaxMetres.Z;
	}
};

/// Where something can be bolted on, and how big a thing. T121.
struct LEDGERFLIGHT_API FLedgerHardpoint
{
	FString Id;

	/// 1 small, 2 medium, 3 large. A mount takes its own class and no other.
	int32 SizeClass = 1;

	FVector3d PositionMetres = FVector3d::ZeroVector;

	/// What is mounted there, by component id; empty when free.
	FString Occupant;
};

/// A gauge: one reading of one component, through one sensor. T125.
struct LEDGERFLIGHT_API FLedgerInstrument
{
	FString Id;

	/// Whose reading, and which: temperatureK, contentsKg, condition, powerKw,
	/// deployment -- or, with no component, hullIntegrity, or pressurePa and
	/// oxygenPa of the compartment named.
	FString Component;
	FString Reading;
	FString Compartment;

	/// What measures it. A gauge whose sensor is dead or unpowered has no
	/// reading at all rather than a wrong one.
	FString Sensor;
};

struct LEDGERFLIGHT_API FLedgerShipHull
{
	/// Which parametric hull the art comes from, and its size and bare mass.
	FString Kit = TEXT("courier");
	double LengthMetres = 9.0;
	double WidthMetres = 1.9;
	double HeightMetres = 1.5;
	double MassKg = 8000.0;

	/// Energy the skin stops before it holes, joules.
	double ArmourJoules = 50000.0;

	/// Spare parts carried, kilograms, and the people aboard. T117, T118.
	double SparesKg = 40.0;
	int32 Crew = 1;
};

struct LEDGERFLIGHT_API FLedgerShipFlight
{
	/// Thrust as acceleration, metres per second squared, and turn rates in
	/// degrees per second: what the flight model is handed today.
	double MainThrust = 9000.0;
	double ManoeuvringThrust = 2600.0;
	double PitchRate = 55.0;
	double YawRate = 45.0;
	double RollRate = 90.0;
	double AtmosphericDrag = 0.55;
	double BallisticKgPerM2 = 300.0;
};

/// One nozzle of a thruster: where it is, which way it pushes the ship and how
/// hard at most. The thruster component is the plumbing -- its power, fuel and
/// wear -- and its nozzles are what the allocator steers. T133.
struct LEDGERFLIGHT_API FLedgerNozzle
{
	/// The thruster component it belongs to.
	int32 Component = INDEX_NONE;

	/// Metres, in the ship frame: x forward, y right, z up.
	FVector3d PositionMetres = FVector3d::ZeroVector;

	/// The way it pushes the ship, against its exhaust: a unit vector.
	FVector3d Push = FVector3d::UnitX();

	double ThrustNewtons = 0.0;
};

struct LEDGERFLIGHT_API FLedgerShipDefinition
{
	FString Name;
	FLedgerShipHull Hull;
	FLedgerShipFlight Flight;
	TArray<FLedgerComponent> Components;
	TArray<FLedgerConnection> Connections;
	TArray<FLedgerCompartment> Compartments;
	TArray<FLedgerHardpoint> Hardpoints;
	TArray<FLedgerInstrument> Instruments;
	TArray<FLedgerNozzle> Nozzles;

	int32 FindComponent(const FString& Id) const;

	/// The compartment a point in the ship's frame is in, or none.
	int32 CompartmentAt(const FVector3d& PointMetres) const;

	/// Every (component, port) a component's port is joined to, either way:
	/// the discoverable half of T109.
	TArray<TPair<int32, int32>> ConnectedTo(int32 Component, int32 Port) const;

	/// Hull and components together, kilograms.
	double MassKg() const;
};

namespace LedgerShips
{
	/// Config/Ships.
	LEDGERFLIGHT_API FString DefaultDirectory();

	/// One ship from its JSON.
	///
	/// **Every way a file can be wrong is a message and a false, never a
	/// crash**: a missing name, an unknown port kind or direction, two
	/// components with one id, a connection to a port that is not there, an
	/// output wired to an output, power wired to coolant, or a required input
	/// left open. All of them are reported, not just the first.
	LEDGERFLIGHT_API bool Parse(const FString& Json, FLedgerShipDefinition& Out, TArray<FString>& OutErrors);

	/// A ship by name from a directory: "courier" is courier.json.
	LEDGERFLIGHT_API bool Load(const FString& Directory, const FString& Name,
		FLedgerShipDefinition& Out, TArray<FString>& OutErrors);

	/// Bolt a component onto a hardpoint. T121.
	///
	/// The component's "mountClass" must be the hardpoint's class, and the
	/// hardpoint free. Its required inputs are joined to the ship's own
	/// sources -- power from the bus, coolant from a radiator, data from the
	/// computer -- so its draw and its heat are in the ship's totals from then
	/// on. Refused, with the reason, rather than half-fitted.
	LEDGERFLIGHT_API bool Mount(FLedgerShipDefinition& Ship, FLedgerComponent Item, const FString& HardpointId, FString& OutError);

	/// The ship as the JSON Parse reads, every number at full precision. T129:
	/// what a refit or a mount has made of it survives a save.
	LEDGERFLIGHT_API FString ToJson(const FLedgerShipDefinition& Ship);
}

/// What a swap would do, worked out before it is done. T128.
struct LEDGERFLIGHT_API FLedgerOutfitPreview
{
	bool bFits = false;
	FString Why;

	/// The ship as it would be, and the three things the screen shows.
	FLedgerShipDefinition After;
	double MassDeltaKg = 0.0;
	double HeatDeltaKw = 0.0;
	double PowerMarginDeltaKw = 0.0;

	/// Handling: main-engine acceleration before and after, metres per second
	/// squared -- the same engine pushing a different mass -- and roll inertia.
	double AccelerationBefore = 0.0;
	double AccelerationAfter = 0.0;
	double RollInertiaBefore = 0.0;
	double RollInertiaAfter = 0.0;
};

namespace LedgerShips
{
	/// Swap one component for another in its slot, on paper. T128.
	///
	/// It fits if it is the same type, its slot class is no bigger than the
	/// slot's size, and it has every port the old one was connected through;
	/// the consequences are computed, not listed.
	LEDGERFLIGHT_API FLedgerOutfitPreview PreviewSwap(const FLedgerShipDefinition& Ship, const FString& ComponentId,
		const FLedgerComponent& Replacement);
}
