// Temperature and moisture as continuous functions of position. Design §6.8,
// Living World §7.2.
//
// **Biomes are read from these rather than painted.** A painted biome map and
// a procedural height field disagree the moment either changes, and the
// disagreement shows up as rainforest on a glacier. Deriving climate from the
// same height field the terrain came from means they cannot drift apart:
// change the mountains and the rain shadow moves with them.
//
// Pure functions on the same terms as LedgerTerrainMath — no engine state, no
// allocation, safe on a worker thread.

#pragma once

#include "CoreMinimal.h"
#include "LedgerTerrainMath.h"

/// What the climate is at a point.
struct FLedgerClimate
{
	/// Degrees Celsius at the surface, altitude included.
	double TemperatureC = 0.0;

	/// The same latitude band at sea level, with no altitude term.
	///
	/// Kept separate because it is the quantity the pole-to-equator acceptance
	/// is about. Surface temperature is *not* monotonic in latitude and should
	/// not be — a mountain at the equator is colder than a beach at 40° — so
	/// asserting monotonicity on it would be asserting that the lapse rate is
	/// broken.
	double SeaLevelTemperatureC = 0.0;

	/// Relative humidity of the air arriving here, in [0,1]. One is saturated
	/// ocean air, zero is a desert that has had everything wrung out of it.
	double Moisture = 0.0;

	/// Metres above sea level. Negative under water.
	double AltitudeMetres = 0.0;
};

namespace LedgerClimate
{
	/// Degrees at the equator and at the poles, at sea level.
	constexpr double EquatorC = 30.0;
	constexpr double PoleC = -25.0;

	/// Degrees lost per kilometre of altitude. 6.5 is the standard atmosphere's
	/// environmental lapse rate.
	constexpr double LapseRateCPerKm = 6.5;

	/// How far upwind the moisture march looks, and in how many steps.
	///
	/// **The step length is the whole ballgame and 25 km was too long.** With
	/// sixteen steps over 400 km the march walked straight over the ridges it
	/// was supposed to notice: a paired test over 754 ridges found the lee side
	/// drier on 54% of them, which is a coin toss. Orographic lift only fires
	/// when consecutive samples differ, so a ridge narrower than a step is a
	/// ridge that does not exist as far as the moisture is concerned.
	///
	/// 300 km in 40 steps is 7.5 km a step. Height sampling is the expensive
	/// thing in this project (docs/comparisons/terrain-component.md), so this
	/// is not free — but climate is read per patch, not per pixel.
	constexpr int32 UpwindSteps = 40;
	constexpr double UpwindFetchMetres = 300000.0;

	/// The prevailing wind at a latitude, as a unit vector tangent to the
	/// sphere.
	///
	/// Banded the way the real circulation is: easterlies in the tropics,
	/// westerlies in the mid latitudes, easterlies again at the poles. The
	/// bands are what make a rain shadow fall on the *west* side of a range at
	/// 20° and the *east* side at 45°, which is the thing a single global wind
	/// direction cannot produce and which anybody who has looked at a map of
	/// deserts will notice is missing.
	LEDGERTERRAIN_API FVector3d PrevailingWind(const FVector3d& UnitSphere);

	/// How much colder a winter is than the annual mean, at the pole, in
	/// degrees. Zero at the equator and scaled by the sine of latitude, which
	/// is the shape a seasonal swing actually has: the tropics barely notice a
	/// year and the poles are a different world twice in one.
	constexpr double SeasonalSwingC = 20.0;

	/// The tilt SeasonalSwingC is quoted at, radians. Earth's 23.44 degrees.
	///
	/// **A reference rather than the body's own tilt, and the difference is the
	/// whole point of T073.** Normalising the seasonal anomaly by the actual
	/// tilt divides out the thing being modelled: a planet leaning 5 degrees
	/// and one leaning 35 come out with identical seasons, which is what the
	/// drawn sine did and what this replaced. Against a fixed reference, 20 C
	/// is the polar swing of an Earth-tilted planet and everything else scales
	/// from it -- this system's 17.2 degree planet gets about 15.
	constexpr double ReferenceTiltRadians = 0.40910518;

	/// The temperature this latitude gains or loses at this point in the year.
	///
	/// SeasonPhase runs 0 to 1 through a year, with 0.25 the northern summer.
	/// The two hemispheres are opposite by construction -- the term is odd in
	/// the sine of latitude -- rather than by a rule somebody has to remember.
	LEDGERTERRAIN_API double SeasonalOffsetC(
		const FVector3d& UnitSphere, double SeasonPhase, double TiltRadians);

	/// Climate at a point on the unit sphere, at a point in the year.
	///
	/// SeasonPhase defaults to zero, which is neither summer nor winter, so
	/// everything that does not care about the season reads the annual mean and
	/// nothing had to change when the season arrived.
	LEDGERTERRAIN_API FLedgerClimate At(
		const FVector3d& UnitSphere, const FLedgerTerrainParams& Params,
		double SeasonPhase = 0.0);

	/// How much snow is lying, 0 to 1.
	///
	/// **The snow line is not a parameter.** It is where this crosses zero, and
	/// it moves with altitude because the surface temperature does -- the lapse
	/// rate was already there and this is the first thing to read it. It moves
	/// with latitude and with the season for the same reason.
	///
	/// Moisture matters as well as cold: a bone-dry basin at minus five has
	/// nothing to fall out of the sky, and the coldest deserts on Earth are
	/// bare rock rather than snowfields.
	LEDGERTERRAIN_API double SnowCover(const FLedgerClimate& Climate);
}
