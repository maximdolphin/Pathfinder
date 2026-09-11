// Weather as a function of time. T092.
//
// **The same decision T070 made about the solar system, made again about the
// air.** A weather model that is integrated has to be stepped from the epoch to
// the moment you care about, so asking what next Tuesday looks like costs a
// simulated week and the answer depends on every frame in between. A weather
// model that is a function of time costs the same to ask about next Tuesday as
// about now, gives the same answer twice, and cannot drift.
//
// So a storm here is not a thing that is updated. It is a birth time, a place,
// a size and a lifetime, and where it is at any moment is arithmetic on those.
// The path it traces is a consequence rather than a record.

#pragma once

#include "CoreMinimal.h"

#include "LedgerAir.h"
#include "LedgerBody.h"

/// One pressure anomaly: a high or a low, somewhere, for a while.
struct FLedgerPressureCell
{
	/// Where it is *now*, radians. Latitude from the equator, longitude from
	/// the body's prime meridian.
	double LatitudeRadians = 0.0;
	double LongitudeRadians = 0.0;

	/// How far off the background pressure at its centre, pascals. Negative is
	/// a low, which is the interesting one -- lows are where the weather is.
	double AnomalyPascals = 0.0;

	/// The radius at which the anomaly has fallen to a third of its centre
	/// value, metres. A mid-latitude cyclone is a thousand kilometres or so.
	double RadiusMetres = 0.0;

	/// Zero at birth, one at death. A cell fades in and out rather than
	/// appearing, because a pressure field with steps in it has infinite wind.
	double Age = 0.0;

	/// How strong it is right now, after the fade. 0 to 1.
	double Strength = 0.0;

	/// Which way it turns. A low turns cyclonically -- anticlockwise in the
	/// northern hemisphere -- and a high the other way, and that is Coriolis
	/// rather than a convention.
	bool bLow = true;

	int32 Index = INDEX_NONE;
};

namespace LedgerWeather
{
	/// How far the tropical cell reaches from the equator, radians.
	///
	/// **The number of wind bands is set by how fast the body turns.** Held and
	/// Hou's result has the Hadley cell's edge scaling as the inverse square
	/// root of the rotation rate: a slow rotator has one enormous cell reaching
	/// to the pole, which is why Venus has no trade winds and no jet stream,
	/// and a fast one is banded, which is why Jupiter has a dozen.
	LEDGERCORE_API double HadleyEdgeRadians(const FLedgerBody& Body);

	/// How many circulation cells there are between the equator and the pole.
	/// Three on Earth: Hadley, Ferrel, polar.
	LEDGERCORE_API int32 CirculationCells(const FLedgerBody& Body);

	/// The prevailing wind at a latitude, metres per second, positive eastward.
	///
	/// Alternating bands: easterly in the tropics, westerly in the mid
	/// latitudes, easterly at the pole. The alternation is the circulation cells
	/// and the strength comes from the rotation, because what turns a
	/// north-south overturning into an east-west wind is Coriolis.
	LEDGERCORE_API double ZonalWindAt(const FLedgerBody& Body, double LatitudeRadians);

	/// The Coriolis parameter at a latitude, per second. 2 Omega sin(lat).
	LEDGERCORE_API double CoriolisAt(const FLedgerBody& Body, double LatitudeRadians);

	/// Every pressure cell alive at a time.
	///
	/// Deterministic from the system's seed, the body and the time, and nothing
	/// else. Cells are born on a fixed schedule, live a fixed span, and move by
	/// a closed form -- so this is a query and not a state.
	LEDGERCORE_API void CellsAt(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch,
		TArray<FLedgerPressureCell>& Out);

	/// One cell at a time, whether or not it is currently alive. The path a
	/// storm traces is this, sampled.
	LEDGERCORE_API FLedgerPressureCell CellAt(
		const FLedgerSystem& System, int32 BodyIndex, int32 CellIndex,
		double SecondsFromEpoch);

	/// How many cells the schedule has, alive or not.
	LEDGERCORE_API int32 CellCount();

	/// How far the cells alone push the pressure at a place, pascals.
	///
	/// The same sum PressureAt makes, without the zonal background. Negative
	/// inside a low. **This is the number that says where the weather is** --
	/// the background field is a climate and the anomaly is a forecast -- and
	/// it is separate so that rain, which happens where air is being made to
	/// rise, does not have to subtract one from the other and get the
	/// subtropical ridge in the answer.
	LEDGERCORE_API double CellAnomalyPascals(
		const FLedgerSystem& System, int32 BodyIndex,
		double LatitudeRadians, double LongitudeRadians, double SecondsFromEpoch);

	/// Sea-level pressure at a place, pascals.
	///
	/// The background field plus every cell's anomaly. The background is the
	/// zonal mean -- highs under the descending branch of each circulation cell
	/// and lows under the ascending one -- which is why the subtropics are dry
	/// and the temperate latitudes are not.
	LEDGERCORE_API double PressureAt(
		const FLedgerSystem& System, int32 BodyIndex,
		const FLedgerAirProfile& Air, double LatitudeRadians,
		double LongitudeRadians, double SecondsFromEpoch);

	/// Wind at a place, metres per second, as (east, north).
	///
	/// **Geostrophic**, which is to say the wind blows along the isobars rather
	/// than down the gradient: the pressure gradient force and Coriolis balance,
	/// and the leftover is a flow at right angles to both. That is why weather
	/// maps are read as flow maps, and why a low turns rather than filling.
	///
	/// Geostrophy fails near the equator where the Coriolis parameter goes to
	/// zero, so within the tropics it is blended into a gradient flow, which is
	/// the honest version of what actually happens there.
	LEDGERCORE_API FVector2D WindAt(
		const FLedgerSystem& System, int32 BodyIndex,
		const FLedgerAirProfile& Air, double LatitudeRadians,
		double LongitudeRadians, double SecondsFromEpoch);

	/// How deep the friction layer is, metres. T093.
	///
	/// Above it the wind is the geostrophic one and the ground might as well
	/// not be there; inside it, drag slows the air and Coriolis turns what is
	/// left. The depth goes as the friction velocity over the Coriolis
	/// parameter, which is why the boundary layer is a kilometre thick in the
	/// mid latitudes and unbounded at the equator.
	LEDGERCORE_API double BoundaryLayerMetres(
		const FLedgerBody& Body, double LatitudeRadians);

	/// Wind at a height above the ground, metres per second, as (east, north).
	///
	/// **Two things happen on the way down and they are not the same thing.**
	/// The speed falls, logarithmically rather than linearly, because a
	/// turbulent boundary layer over a rough surface has a log profile -- that
	/// is why a ten-metre mast reads two thirds of what a hundred-metre one
	/// does and not a tenth. And the direction *backs*, turning across the
	/// isobars towards the low, because friction breaks the balance between
	/// pressure and Coriolis and the leftover points down the gradient. Twenty
	/// to thirty degrees over land is the textbook figure and it is what makes
	/// surface weather converge into a low rather than circle it forever.
	///
	/// Above the boundary layer this is exactly WindAt.
	LEDGERCORE_API FVector2D WindAtAltitude(
		const FLedgerSystem& System, int32 BodyIndex,
		const FLedgerAirProfile& Air, double LatitudeRadians,
		double LongitudeRadians, double AltitudeMetres, double SecondsFromEpoch);

	/// The roughness length of the ground, metres: the height at which the log
	/// profile says the wind is zero. Open country is a few centimetres, a
	/// forest is a metre, open water is a fraction of a millimetre.
	LEDGERCORE_API double RoughnessMetres();
}
