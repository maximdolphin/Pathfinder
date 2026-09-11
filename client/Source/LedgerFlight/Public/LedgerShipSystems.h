// A ship's systems, running on its component graph. T110 onwards.
//
// The definition says what a ship is; this says what it is doing: which
// components are on, how whole they are, what they are drawing, and -- as the
// milestone goes on -- how hot, how fuelled and how damaged. Pure, like the
// flight model, so a ship's failure modes can be exercised in a test without
// flying anything.

#pragma once

#include "CoreMinimal.h"
#include "LedgerShipDefinition.h"

/// Something in a hold. T122.
struct LEDGERFLIGHT_API FLedgerCargoItem
{
	FString Name;
	double MassKg = 0.0;
	double VolumeM3 = 0.0;
};

/// One component, live.
struct LEDGERFLIGHT_API FLedgerComponentState
{
	/// Switched on.
	bool bOn = true;

	/// 1 whole, 0 destroyed.
	double Condition = 1.0;

	/// What the power solve decided: whether it is getting what it asks for,
	/// and how much that is, kilowatts. A plant's figure is what it produced.
	bool bPowered = false;
	double PowerKw = 0.0;

	/// T111. Kelvin, and whether a radiator is out: a retracted radiator still
	/// circulates its coolant and rejects almost none of it.
	double TemperatureK = 293.0;
	bool bDeployed = true;

	/// T112 and T113. What it holds, kilograms: fuel in a tank, cargo in a hold.
	double ContentsKg = 0.0;

	/// T116. Hours it has run, and whether it is asking for service.
	double OperatingHours = 0.0;

	/// T120. The heat it made last thermal step, kilowatts.
	double HeatKw = 0.0;

	/// T119. A shield's four sectors, kilojoules: fore, aft, port, starboard.
	double SectorKj[4] = { 0.0, 0.0, 0.0, 0.0 };

	/// T122. What a hold has in it.
	TArray<FLedgerCargoItem> Cargo;

	/// T123. An actuator: where it is between stowed (0) and deployed (1),
	/// and where it has been told to go.
	double Deployment = 0.0;
	double DeployTarget = 0.0;

	bool IsWorking() const { return bOn && Condition > 0.0; }
	bool NeedsService() const { return Condition > 0.0 && Condition < ServiceBelow; }
	static constexpr double ServiceBelow = 0.35;
};

/// The thermal numbers, in one place so a test can predict what they do.
struct LEDGERFLIGHT_API FLedgerThermal
{
	/// Where heat is measured from, and where components throttle and fail.
	static constexpr double AmbientK = 293.0;
	static constexpr double ThrottleK = 400.0;
	static constexpr double FailK = 500.0;

	/// Heat capacity per kilogram of component, kilojoules per kelvin.
	static constexpr double KjPerKgK = 0.5;

	/// What a component loses on its own to the hull around it, kilowatts per
	/// kelvin over ambient.
	static constexpr double PassiveKwPerK = 0.05;

	/// The share of a radiator's rating a retracted one still rejects.
	static constexpr double RetractedShare = 0.05;

	/// The share of the power a component handles that ends up as its heat,
	/// when its file does not say: a plant sheds half of what it makes, a
	/// consumer a third of what it draws.
	static double HeatShare(const FLedgerComponent& Component);

	/// Output as a fraction of full at a temperature: whole to the throttle
	/// point, falling to a fifth at failure.
	static double Throttle(double TemperatureK);
};

/// One compartment, live. T115.
struct LEDGERFLIGHT_API FLedgerCompartmentState
{
	double PressurePa = 101325.0;
	double TemperatureK = 293.0;

	/// Holes in it to the outside, square metres.
	double BreachM2 = 0.0;

	/// T118. The breathable part of the pressure, and the part that is not.
	double OxygenPa = 21200.0;
	double CarbonDioxidePa = 40.0;
};

struct LEDGERFLIGHT_API FLedgerShipState
{
	/// Parallel to the definition's components.
	TArray<FLedgerComponentState> Components;

	/// Parallel to the definition's compartments.
	TArray<FLedgerCompartmentState> Compartments;

	/// The hull as a structure, 1 whole to 0 gone. Past StructuralFailure it
	/// has failed, whatever the compartments are doing.
	double HullIntegrity = 1.0;
	static constexpr double StructuralFailure = 0.2;

	/// T117. Spare parts aboard, kilograms.
	double SparesKg = 0.0;

	/// T118. Breathing oxygen held in reserve, kilograms.
	double OxygenReserveKg = 0.0;

	/// T120. What the radiators rejected last thermal step, kilowatts.
	double RejectedKw = 0.0;

	static FLedgerShipState For(const FLedgerShipDefinition& Ship);
};

/// Mass, centre of mass and inertia, from what the ship is made of and what it
/// is carrying. T113.
struct LEDGERFLIGHT_API FLedgerMassProperties
{
	double MassKg = 0.0;

	/// Metres, in the ship's frame: x forward, y right, z up.
	FVector3d CentreMetres = FVector3d::ZeroVector;

	/// The inertia tensor about the centre of mass, kilogram square metres,
	/// row by row.
	double Inertia[3][3] = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };

	/// Angular acceleration from a torque, radians per second squared.
	FVector3d Respond(const FVector3d& TorqueNewtonMetres) const;
};

/// What a hit on the hull did. T115.
struct LEDGERFLIGHT_API FLedgerHullHit
{
	bool bPenetrated = false;
	int32 Compartment = INDEX_NONE;
	double HoleM2 = 0.0;
};

/// What a repair did. T117.
struct LEDGERFLIGHT_API FLedgerRepair
{
	bool bDone = false;
	double ConditionBefore = 0.0;
	double ConditionAfter = 0.0;
	double PartsKg = 0.0;
	FString Why;
};

/// What the air inside says. T118.
enum class ELedgerAirWarning : uint8
{
	None = 0,
	LowPressure = 1,
	LowOxygen = 2,
	HighCarbonDioxide = 4,
};
ENUM_CLASS_FLAGS(ELedgerAirWarning);

/// What a ship looks like to a sensor. T120.
struct LEDGERFLIGHT_API FLedgerSignature
{
	/// Heat reaching space, kilowatts: what the radiators reject and what
	/// the hull sheds on its own.
	double ThermalKw = 0.0;

	/// Emitted, kilowatts: shields, drives and sensors at work.
	double EmissionKw = 0.0;

	/// What a radar sees, square metres: the hull and whatever radiators are
	/// out.
	double CrossSectionM2 = 0.0;

	/// The range each gets it seen at, kilometres, and the longest of them.
	double ThermalRangeKm = 0.0;
	double EmissionRangeKm = 0.0;
	double RadarRangeKm = 0.0;
	double DetectionRangeKm() const { return FMath::Max3(ThermalRangeKm, EmissionRangeKm, RadarRangeKm); }
};

/// What an alarm is about. T126.
enum class ELedgerAlarm : uint8
{
	ReactorOverheat,
	PowerShed,
	FuelLow,
	HullBreach,
	LowOxygen,
	HighCarbonDioxide,
	StructuralFailure,
	ComponentDestroyed,
	ServiceDue,
};

/// One alarm raised: what, about which component, and how it sounds -- a
/// tone and a rhythm, different for every kind, so a listener can tell which
/// system is in trouble without looking. T126.
struct LEDGERFLIGHT_API FLedgerAlarm
{
	ELedgerAlarm Kind = ELedgerAlarm::ServiceDue;
	int32 Component = INDEX_NONE;
	double ToneHz = 0.0;
	double OnSeconds = 0.0;
	double OffSeconds = 0.0;
	/// Lower is more urgent: the one a single voice plays.
	int32 Urgency = 0;
	FString Says;
};

/// A gauge, read. T125.
struct LEDGERFLIGHT_API FLedgerGaugeReading
{
	bool bAvailable = false;
	double Value = 0.0;
	FString Why;
};

/// What one power solve found.
struct LEDGERFLIGHT_API FLedgerPowerReport
{
	double SupplyKw = 0.0;
	double DemandKw = 0.0;
	double DeliveredKw = 0.0;

	/// Consumers dropped, in the order they were dropped.
	TArray<int32> Shed;

	/// T119. What is made less what everything but the thrusters takes:
	/// the power left for thrust, which is what raising a shield spends.
	double ThrustHeadroomKw = 0.0;
};

namespace LedgerShipSystems
{
	/// Whether a component's required inputs are all fed by something that is
	/// working. A plant with its coolant cut is a plant that is not producing.
	LEDGERFLIGHT_API bool IsFed(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Component);

	/// Power, generated and distributed. T110.
	///
	/// Plants that are on, whole and fed produce their output scaled by their
	/// condition. Power reaches a consumer only along power connections, through
	/// buses and couplings that are themselves working. Where what the working
	/// plants make is less than what the consumers they reach ask for, the
	/// consumers are shed **in the order the configuration states**: the
	/// highest priority number first, then the next, until what is left fits.
	LEDGERFLIGHT_API FLedgerPowerReport SolvePower(const FLedgerShipDefinition& Ship, FLedgerShipState& State);

	/// Heat, for one step. T111.
	///
	/// Every component makes heat as a share of the power it handles. Those on
	/// a coolant loop -- a coolant input joined to a radiator -- have it carried
	/// away, up to what the radiator can reject, shared in proportion to what
	/// each put in; the rest, and everything a component not on a loop makes,
	/// warms it against its heat capacity, less what it loses to the hull. Over
	/// the throttle point it gives less; over the failure point it is gone.
	LEDGERFLIGHT_API void StepThermal(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds);

	/// Fuel for thrust, one step. T112.
	///
	/// Propellant, not reactor fuel: tanks with kind 0. The mass that leaves is
	/// the thrust over the exhaust velocity -- the thruster's Isp times g0 --
	/// drawn evenly from the working tanks that feed it. Returns the share of
	/// the thrust asked for that the fuel could give: one while there is fuel,
	/// less as the last of it goes, zero dry.
	LEDGERFLIGHT_API double Burn(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		int32 Thruster, double ThrustNewtons, double DeltaSeconds);

	/// Reactor fuel, one step: each working plant draws its rate, scaled by
	/// what it is making, from the tanks that feed it. A plant whose tanks are
	/// empty is no longer fed, and so no longer makes anything.
	LEDGERFLIGHT_API void FuelReactors(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds);

	/// Fuel aboard of one kind, kilograms: 0 propellant, 1 reactor fuel.
	LEDGERFLIGHT_API double FuelKg(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Kind);

	/// Mass properties from the hull, the components and their contents. T113.
	LEDGERFLIGHT_API FLedgerMassProperties MassProperties(const FLedgerShipDefinition& Ship, const FLedgerShipState& State);

	/// Everything downstream of a component: what it feeds, and what those
	/// feed, along outputs of every kind. T114: the failure order is this, not
	/// a table.
	LEDGERFLIGHT_API TArray<int32> DownstreamOf(const FLedgerShipDefinition& Ship, int32 Component, bool bPowerOnly = false);

	/// A hit. T114.
	///
	/// Takes condition off a component; what a hit has left over once the
	/// component is destroyed goes on along its connections -- a quarter of it
	/// to each thing it is joined to, either way -- which is how a reactor that
	/// goes up takes its coupling and its coolant loop with it. What that does
	/// to power, heat and thrust is the power and thermal models' business,
	/// which is why nothing here says which systems fail.
	LEDGERFLIGHT_API void Damage(const FLedgerShipDefinition& Ship, FLedgerShipState& State, int32 Component, double Amount);

	/// A hit on the hull at a point in the ship's frame, carrying so many
	/// joules. T115.
	///
	/// What the armour does not stop makes a hole, and the hole is in the
	/// compartment the point is in -- that one and no other. Every hit also
	/// takes something off the hull as a structure.
	LEDGERFLIGHT_API FLedgerHullHit HitHull(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		const FVector3d& PointMetres, double EnergyJoules);

	/// Air through the holes, one step: choked flow out of each breached
	/// compartment to the pressure outside. Compartments are sealed from each
	/// other. ponytail: hatches stay shut; opening one is a door, and doors are
	/// M09's.
	LEDGERFLIGHT_API void StepAtmosphere(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		double OutsidePa, double DeltaSeconds);

	/// The venting time constant of a compartment through a hole in vacuum,
	/// seconds: V over (Cd A c*), with c* the choked flow speed of air at the
	/// compartment's temperature. Pressure falls as exp(-t / this).
	LEDGERFLIGHT_API double VentSeconds(double VolumeM3, double HoleM2, double TemperatureK);

	/// Wear, one step. T116.
	///
	/// Everything working and carrying load loses condition with its hours --
	/// its own rate, times how hard it is being run, times how much hotter than
	/// comfortable it is -- and gives less as it goes: a plant makes its output
	/// times its condition, a thruster pushes with its condition. It asks for
	/// service below ServiceBelow, which is well before it fails.
	LEDGERFLIGHT_API void Wear(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds);

	/// The share of its rated thrust a thruster can give now: whole, powered,
	/// worn as it is. The fuel's share is Burn's.
	LEDGERFLIGHT_API double ThrustShare(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Thruster);

	/// Repair a component. T117.
	///
	/// In the field, from the spares aboard, and only so far: to FieldCeiling,
	/// and not a destroyed component at all. In a workshop, from its stock
	/// (WorkshopPartsKg, which this takes from), to whole -- replacing a
	/// destroyed component outright at half its mass in parts. Parts are a
	/// twentieth of the mass for every whole condition restored. Short of
	/// parts, it repairs as far as the parts go.
	static constexpr double FieldCeiling = 0.7;
	LEDGERFLIGHT_API FLedgerRepair Repair(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		int32 Component, bool bWorkshop, double* WorkshopPartsKg = nullptr);

	/// The air inside, one step, and what it warns of. T118.
	///
	/// The crew breathe oxygen out and carbon dioxide in, in the first
	/// compartment; every compartment also leaks to the outside through its
	/// seals over LeakHours. A working, powered life support scrubs the carbon
	/// dioxide and tops the oxygen and the pressure up from its reserve until
	/// the reserve is gone. Cut it and the air runs down on a schedule a test
	/// can write down.
	static constexpr double LeakHours = 48.0;
	LEDGERFLIGHT_API ELedgerAirWarning StepLifeSupport(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		double OutsidePa, double DeltaSeconds);

	/// A person's oxygen, kilograms a day; and the thresholds warned at, Pa.
	static constexpr double OxygenKgPerPersonDay = 0.84;
	static constexpr double LowOxygenPa = 16000.0;
	static constexpr double HighCarbonDioxidePa = 1000.0;
	static constexpr double LowPressurePa = 60000.0;

	/// Shields, one step. T119.
	///
	/// A raised shield on a powered bus puts its draw into its four sectors --
	/// fore, aft, port and starboard -- up to its capacity; a lowered or
	/// unpowered one bleeds away over a few seconds.
	LEDGERFLIGHT_API void StepShields(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds);

	/// A hit arriving from a direction in the ship's frame (pointing from the
	/// ship to where it came from): the sector facing it takes what it has;
	/// what is left, joules, reaches the hull.
	LEDGERFLIGHT_API double AbsorbHit(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		const FVector3d& FromDirection, double EnergyJoules);

	/// What the ship looks like to a sensor now, from what its power and
	/// thermal models say it is doing. T120.
	LEDGERFLIGHT_API FLedgerSignature Signature(const FLedgerShipDefinition& Ship, const FLedgerShipState& State);

	/// A ship with everything switched off. T124.
	LEDGERFLIGHT_API FLedgerShipState Cold(const FLedgerShipDefinition& Ship);

	/// Whether a component can start now: every required input live -- fuel
	/// and coolant from a source that is running, power actually arriving,
	/// data from something powered. Reason, when it cannot, says which input.
	LEDGERFLIGHT_API bool CanStart(const FLedgerShipDefinition& Ship, const FLedgerShipState& State,
		int32 Component, FString* Reason = nullptr);

	/// Switch a component on if it can start, and refuse, saying why, if not.
	LEDGERFLIGHT_API bool TryStart(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		int32 Component, FString* Reason = nullptr);

	/// The order a cold ship comes up in: everything that can start, then
	/// everything that can start after that, until nothing more can. The graph
	/// decides it; nothing lists it.
	LEDGERFLIGHT_API TArray<int32> StartupOrder(const FLedgerShipDefinition& Ship);

	/// What a consumer asks of the bus now: its draw, except an actuator, which
	/// draws only while it is moving. T123.
	LEDGERFLIGHT_API double DrawKw(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Component);

	/// Put cargo in a hold. T122.
	///
	/// Refused, saying which, when it would take the hold past its volume or
	/// its mass limit -- whichever binds first. What is stowed is the hold's
	/// contents, and so counts in the ship's mass at the hold's position.
	LEDGERFLIGHT_API bool Stow(const FLedgerShipDefinition& Ship, FLedgerShipState& State, int32 Hold,
		const FLedgerCargoItem& Item, FString* Why = nullptr);
	LEDGERFLIGHT_API double HoldVolumeUsedM3(const FLedgerShipState& State, int32 Hold);

	/// Gear, ramps, doors: one step. T123.
	///
	/// An actuator moves towards its target at its travel rate while it is
	/// whole and powered, and drawing its power only while it moves. Without
	/// power it stops where it is -- a gear cut mid-cycle stays half down -- and
	/// finishes when power comes back.
	LEDGERFLIGHT_API void StepActuators(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds);

	/// Read a gauge through its sensor. T125.
	///
	/// The value is the component's own number, not a copy kept somewhere
	/// else, so an instrument cannot lie; and with its sensor dead or
	/// unpowered there is no value at all -- unavailable, with the reason --
	/// rather than the last one or a wrong one.
	LEDGERFLIGHT_API FLedgerGaugeReading ReadGauge(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Instrument);

	/// Everything about a ship's condition, as JSON, every number at full
	/// precision, and back. T129. The configuration is LedgerShips::ToJson's.
	LEDGERFLIGHT_API FString SaveState(const FLedgerShipDefinition& Ship, const FLedgerShipState& State);
	LEDGERFLIGHT_API bool LoadState(const FLedgerShipDefinition& Ship, const FString& Json, FLedgerShipState& Out, TArray<FString>& OutErrors);

	/// How an alarm of a kind sounds. T126.
	LEDGERFLIGHT_API void AlarmVoice(ELedgerAlarm Kind, double& OutToneHz, double& OutOnSeconds, double& OutOffSeconds, int32& OutUrgency);

	/// Every alarm the ship's state raises now, most urgent first: raised by
	/// the system that is in trouble, from its own numbers. T126.
	LEDGERFLIGHT_API TArray<FLedgerAlarm> Alarms(const FLedgerShipDefinition& Ship, const FLedgerShipState& State,
		const FLedgerPowerReport& Power, ELedgerAirWarning Air);
}
