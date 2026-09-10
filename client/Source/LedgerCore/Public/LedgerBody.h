// What a planet, moon, station or asteroid is, before anything renders it. T069.
//
// One description, in double precision, deterministic from a seed. Everything
// M03 builds -- the ephemeris, the reference frames, day and night, seasons,
// eclipses -- reads these numbers, and the terrain the game already has is one
// body's worth of them.
//
// **Physical quantities in SI, and stored as such.** The renderer works in
// centimetres and the terrain carries a radius in them; that is a rendering
// unit and it belongs at the boundary, not in the description. A body's mass in
// kilogrammes and radius in metres are facts about the body, and converting
// them at the point of use is one multiplication against a whole category of
// unit confusion.
//
// Angles in radians for the same reason: degrees are for reports.

#pragma once

#include "CoreMinimal.h"

#include "LedgerMath.h"

/// What kind of thing this is. Decides what is built for it, not how it moves --
/// an asteroid and a planet obey the same ephemeris.
enum class ELedgerBodyKind : uint8
{
	Star,
	Planet,
	Moon,
	GasGiant,
	Asteroid,
	Station,
};

LEDGERCORE_API const TCHAR* LexToString(ELedgerBodyKind Kind);
LEDGERCORE_API bool LexFromString(ELedgerBodyKind& Out, const TCHAR* Text);

/// A Keplerian orbit, in the classical six elements.
///
/// Six numbers and no state: where a body is at a given time is a function of
/// these and the time, which is what makes a system reproducible from a seed
/// and what lets the map and the sky agree without either telling the other.
struct FLedgerOrbit
{
	/// Metres. Zero for a body that does not orbit -- the system's primary.
	double SemiMajorAxisMetres = 0.0;

	/// 0 is a circle, below 1 is an ellipse. Nothing here is hyperbolic: a body
	/// on an escape trajectory is not part of a system description.
	double Eccentricity = 0.0;

	/// Radians from the system's reference plane.
	double InclinationRadians = 0.0;

	/// Radians. Where the orbit crosses the reference plane going north.
	double AscendingNodeRadians = 0.0;

	/// Radians from the ascending node to periapsis.
	double PeriapsisArgumentRadians = 0.0;

	/// Radians at the epoch. The one element that is a clock rather than a
	/// shape, and the only reason a system needs an epoch at all.
	double MeanAnomalyAtEpochRadians = 0.0;

	bool operator==(const FLedgerOrbit& Other) const;
};

/// One body.
struct FLedgerBody
{
	/// What it is called. Bodies have names; the game does not yet
	/// (docs/naming.md), and these are unrelated.
	FString Name;

	ELedgerBodyKind Kind = ELedgerBodyKind::Planet;

	/// Everything generated about this body descends from this: its terrain,
	/// its surface detail, and whatever M12 eventually scatters over it.
	uint32 Seed = 0;

	double MassKg = 0.0;
	double RadiusMetres = 0.0;

	/// Seconds for one rotation about its own axis. Negative is retrograde,
	/// which is a real thing two planets in this solar system do.
	double RotationPeriodSeconds = 0.0;

	/// Radians between the rotation axis and the orbit's normal. This is what
	/// makes seasons (T073), so a body with zero tilt has none.
	double AxialTiltRadians = 0.0;

	/// Radians of rotation already turned at the epoch. Without it every body
	/// in a system starts with the same face towards the same place, which is
	/// a coincidence nobody would believe.
	double RotationAtEpochRadians = 0.0;

	/// Index into the system's body array, or INDEX_NONE for the primary.
	/// A moon's parent is its planet, not the star.
	int32 ParentIndex = INDEX_NONE;

	FLedgerOrbit Orbit;

	bool operator==(const FLedgerBody& Other) const;
};

/// A whole system, and the epoch its orbits and rotations are measured from.
struct FLedgerSystem
{
	uint32 Seed = 0;

	/// Seconds since the simulation's zero. Not a wall-clock date: the
	/// simulation has its own time and borrowing a calendar would imply this
	/// is Earth's sky.
	double EpochSeconds = 0.0;

	/// The primary is index 0 by convention, and nothing enforces it -- a
	/// system read from disk is checked, not trusted (see Validate).
	TArray<FLedgerBody> Bodies;

	bool operator==(const FLedgerSystem& Other) const;
};

namespace LedgerBodies
{
	/// JSON, and every double written at full precision.
	///
	/// **`%.17g` rather than the default.** A double needs seventeen
	/// significant digits to survive a round trip, and anything less silently
	/// moves a planet: at one astronomical unit, the fifteenth digit is about
	/// a hundred metres. The acceptance for this task is that a description
	/// rebuilds *identically*, and identical means the same bits.
	LEDGERCORE_API FString ToJson(const FLedgerSystem& System);

	/// Parses what ToJson wrote. False, with a reason, on anything it did not.
	LEDGERCORE_API bool FromJson(const FString& Json, FLedgerSystem& Out, FString& OutError);

	/// Whether a system is internally consistent: a primary that orbits
	/// nothing, parents that exist and are not the body itself, no cycles, and
	/// no body with a mass or radius of zero.
	///
	/// Separate from parsing because they answer different questions. A file
	/// can be valid JSON describing an impossible system, and saying which of
	/// those went wrong is the difference between a five-minute fix and an
	/// afternoon.
	LEDGERCORE_API bool Validate(const FLedgerSystem& System, FString& OutError);

	/// A system from a seed. Deterministic: the same seed is the same system,
	/// on any machine, forever.
	///
	/// **Two indices are guaranteed and the rest are not.** Body 0 is the star
	/// and body 1 is the world the game is about; everything after depends on
	/// what the seed produced, because a system with four planets and one with
	/// seven cannot both put a gas giant at index 3. Ask for the others by role.
	LEDGERCORE_API FLedgerSystem Generate(uint32 Seed);

	/// How bright a star is, relative to the Sun, from its mass alone.
	///
	/// The main-sequence mass-luminosity relation, piecewise because one
	/// exponent does not cover the sequence. It lives here rather than in
	/// LedgerSky because the GENERATOR needs it -- the frost line and the
	/// habitable zone are both set by it, and a system has to be laid out
	/// before anything can look at its sky. LedgerSky's watts are this times
	/// the Sun's output.
	LEDGERCORE_API double StarLuminosityRelative(const FLedgerBody& Star);

	/// The body that orbits nothing. Index 0 by convention and by check.
	LEDGERCORE_API int32 PrimaryIndex(const FLedgerSystem& System);

	/// The world the game is about: index 1, the one in the habitable zone.
	LEDGERCORE_API int32 HomeIndex(const FLedgerSystem& System);

	/// The first body of a kind, or INDEX_NONE.
	LEDGERCORE_API int32 FirstOfKind(
		const FLedgerSystem& System, ELedgerBodyKind Kind);

	/// The first body orbiting a given one, or INDEX_NONE.
	LEDGERCORE_API int32 FirstChildOf(const FLedgerSystem& System, int32 ParentIndex);

	/// The first body of a kind orbiting a given one, or INDEX_NONE.
	LEDGERCORE_API int32 FirstChildOfKind(
		const FLedgerSystem& System, int32 ParentIndex, ELedgerBodyKind Kind);

	/// Whether a system could exist: one star, planets that will not scatter
	/// each other, giants only where giants form, moons outside the Roche limit
	/// and inside the Hill sphere, and a habitable world somewhere it could be
	/// habitable.
	///
	/// Separate from Validate, which asks whether a description is
	/// self-consistent. A system can be perfectly well-formed and still be one
	/// that would have torn itself apart in its first million years.
	LEDGERCORE_API bool Plausible(const FLedgerSystem& System, FString& OutWhy);
}
