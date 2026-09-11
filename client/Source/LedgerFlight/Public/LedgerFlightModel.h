// The flight model: gravity, drag and ground contact, integrated by hand.
//
// **Not Chaos, and not an actor.** Design §6.9 gives the first reason: Chaos is
// a single simulation space with no nested reference frames, so a ship near a
// planet is engine-level surgery rather than a physics body. This header gives
// the second: a model that is a plain struct can be integrated ten thousand
// times in a unit test in a millisecond, with no world, no actor and no frame.
//
// Everything it needs about the planet arrives through FLedgerGravityField,
// which is an interface with exactly one implementation today and will have a
// second the first time a ship flies near two bodies at once (M03). It exists
// now because it is what removes the dependency on ALedgerPlanet, and that
// dependency is the whole reason this was untestable.
//
// This is where M05's component graph and M06's thruster allocation land. It is
// deliberately small today and will not stay that way.

#pragma once

#include "CoreMinimal.h"

/// What the flight model needs to know about the body it is near.
///
/// Two functions rather than a pointer to the planet: gravity at a distance,
/// and the radius of the ground under a direction. A test supplies a sphere;
/// the game supplies the terrain's own height function, which is the same
/// function the mesh was built from and therefore never disagrees with it.
struct LEDGERFLIGHT_API FLedgerGravityField
{
	/// Centre of the body, in world space.
	FVector3d Centre = FVector3d::ZeroVector;

	/// Reference radius, centimetres.
	double Radius = 0.0;

	/// Acceleration at the reference radius, cm/s^2. Earth is 981.
	double SurfaceGravity = 981.0;

	/// Scale height of the atmospheric density falloff, centimetres.
	double DragScaleHeight = 800000.0;

	/// Fraction of velocity bled off per second at sea-level density.
	double AtmosphericDrag = 0.55;

	/// What the air itself is doing, centimetres per second, world frame. T093.
	///
	/// **Drag acts on the speed through the AIR, not over the ground**, and
	/// those differ by exactly this. Leaving it out is the same as asserting
	/// that the atmosphere is nailed to the planet, which is the assumption
	/// that makes a headwind and a tailwind cost the same -- and any pilot can
	/// tell you they do not. It costs one subtraction.
	FVector3d WindCmPerSecond = FVector3d::ZeroVector;

	/// Mass over drag area, kilograms per square metre: the ballistic
	/// coefficient. T100. Zero leaves it out, which is what every test that
	/// predates it wants.
	///
	/// **The drag that matters at orbital speed.** AtmosphericDrag is a
	/// fraction of the speed per second, which flies well at a few hundred
	/// metres a second and is nothing at seven kilometres a second: a ship
	/// entering on it alone reaches the dense air still at orbital speed, and
	/// every entry burns whatever its angle. Real drag goes as rho v squared,
	/// so an entry sheds its speed high up, where the air is thin -- and how
	/// high depends on how steeply it comes in.
	double BallisticKgPerM2 = 0.0;

	/// Air density at the datum, kilograms per cubic metre, for the drag above.
	double SeaLevelDensity = 1.225;

	/// Ground radius under a unit direction. Defaults to the reference sphere;
	/// the game replaces it with the terrain's height function.
	TFunction<double(const FVector3d&)> SurfaceRadiusAt;

	double GroundAt(const FVector3d& UnitDirection) const
	{
		return SurfaceRadiusAt ? SurfaceRadiusAt(UnitDirection) : Radius;
	}
};

/// State the model owns and integrates.
struct LEDGERFLIGHT_API FLedgerFlightState
{
	FVector3d Position = FVector3d::ZeroVector;
	FVector3d Velocity = FVector3d::ZeroVector;

	/// Set by the model when the ship is resting on the ground, cleared once it
	/// is clear of it by a margin — a single threshold would flicker.
	bool bLanded = false;

	/// Height of the landing gear above the hull's origin, centimetres.
	double GearHeight = 140.0;

	/// Speed retained per contact with the ground.
	double GroundFriction = 0.86;
};

/// What the hull is made to take. T100.
struct LEDGERFLIGHT_API FLedgerHeatShield
{
	/// Nose radius, metres. Blunter is cooler: the shock stands further off.
	double NoseRadiusMetres = 2.0;

	/// Heat the skin holds per square metre per kelvin. Thin, so it follows
	/// the flux within seconds rather than averaging a whole entry away.
	double HeatCapacity = 5000.0;

	/// For radiating the heat back out, which is most of how a hull survives.
	double Emissivity = 0.85;

	/// Skin temperature past which the hull starts to fail, kelvin.
	double FailKelvin = 2150.0;

	/// Hull lost per second for every hundred kelvin over the limit.
	double DamagePerSecondPer100K = 0.02;
};

/// The skin through an entry.
struct LEDGERFLIGHT_API FLedgerHeatState
{
	/// Zero until the first step, which starts it at the ambient temperature.
	double SkinKelvin = 0.0;
	double FluxWattsPerM2 = 0.0;
	double PeakFluxWattsPerM2 = 0.0;
	double PeakKelvin = 0.0;

	/// The heat taken in and the heat radiated away, joules per square metre.
	/// Their difference is what the skin is holding.
	double LoadJoulesPerM2 = 0.0;
	double RadiatedJoulesPerM2 = 0.0;

	/// Hull lost to heat, 0 to 1.
	double Damage = 0.0;
};

namespace LedgerFlight
{
	/// Advances the state by one step under gravity, drag and ground contact.
	///
	/// Thrust is applied by the caller before this is called: the model does not
	/// know what a throttle is, which is what keeps it independent of input,
	/// of autopilots, and of whatever M21's directives turn out to want.
	LEDGERFLIGHT_API void Integrate(
		FLedgerFlightState& State,
		const FLedgerGravityField& Field,
		double DeltaSeconds);

	/// Seconds per integration step. Physics runs at this rate whatever the
	/// frame rate is.
	constexpr double FixedStep = 1.0 / 120.0;

	/// Advances by a frame's worth of time, in fixed steps.
	///
	/// **Frame time varies and physics must not.** Integrating with the frame's
	/// own delta makes the trajectory depend on how fast the machine is, which
	/// is a bug that only shows up on somebody else's computer — or, as it
	/// happened here, on the same computer on a quieter afternoon. It was found
	/// by a capture comparison: the ship fell a different distance before the
	/// first screenshot, because the run was faster than the one before it.
	///
	/// `Accumulator` carries the remainder between frames and belongs to the
	/// caller, so two ships do not share one.
	LEDGERFLIGHT_API void Advance(
		FLedgerFlightState& State,
		const FLedgerGravityField& Field,
		double DeltaSeconds,
		double& Accumulator);

	/// Height above the ground beneath, in centimetres.
	/// Stagnation-point heat flux, watts per square metre: Sutton and Graves,
	/// k sqrt(rho / r) v cubed. A function of the density and the speed, and of
	/// nothing else about where the ship is -- in particular not its altitude.
	LEDGERFLIGHT_API double HeatFlux(
		double DensityKgPerM3, double SpeedMetresPerSecond, double NoseRadiusMetres);

	/// One step of the skin: heated by the flux, cooled by radiating, and
	/// failing while it is over the shield's limit.
	LEDGERFLIGHT_API void Heat(FLedgerHeatState& State, const FLedgerHeatShield& Shield,
		double DensityKgPerM3, double AirspeedMetresPerSecond, double AmbientKelvin,
		double DeltaSeconds);

	LEDGERFLIGHT_API double AltitudeAbove(
		const FLedgerFlightState& State,
		const FLedgerGravityField& Field);
}
