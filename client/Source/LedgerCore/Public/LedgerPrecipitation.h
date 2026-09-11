// What falls out of the sky, and where it changes from one thing to another.
// T095.
//
// **A function of time, like the weather it comes from.** T092's lows are a
// birth time, a place, a size and a lifetime; a low passing over a site is
// arithmetic on those, and so is the rain it brings. Nothing here accumulates,
// so asking about next Tuesday costs what asking about now costs.
//
// The one idea worth stating: *rain and snow are the same event at different
// temperatures*. There is no snow model beside a rain model to fall out of step
// with it. There is a rate, which comes from how hard the air is being made to
// rise, and a temperature at the altitude in question, which comes from the
// surface temperature and the lapse rate -- and the boundary between them is
// wherever that temperature crosses freezing. It cannot be anywhere else,
// because nothing else decides it.

#pragma once

#include "CoreMinimal.h"

#include "LedgerAir.h"
#include "LedgerBody.h"

/// What is falling.
enum class ELedgerPrecipitation : uint8
{
	None,
	Rain,
	Snow,
	/// Lifted rather than fallen. A dry world with no condensable water has no
	/// rain and no snow, and a strong enough wind still fills the air.
	Dust,
};

LEDGERCORE_API const TCHAR* LexToString(ELedgerPrecipitation Kind);

struct FLedgerPrecipitation
{
	ELedgerPrecipitation Kind = ELedgerPrecipitation::None;

	/// Millimetres of liquid water an hour. Snow is quoted the same way --
	/// melted -- because that is how it is measured and because quoting depth
	/// of snow would need a compaction model nobody has asked for.
	double RateMillimetresPerHour = 0.0;

	/// Where the air is at freezing, metres above the datum. Negative when it
	/// is already below freezing at the ground, in which case it snows all the
	/// way down.
	double FreezingLevelMetres = 0.0;

	/// The deck it is falling out of. Nothing falls above the cloud top.
	double CloudBaseMetres = 0.0;
	double CloudTopMetres = 0.0;

	/// Terminal velocity, metres per second, downwards. The visible difference
	/// between rain and snow is mostly this.
	double FallSpeedMetresPerSecond = 0.0;

	bool IsFalling() const
	{
		return Kind != ELedgerPrecipitation::None && RateMillimetresPerHour > 0.0;
	}
};

namespace LedgerPrecip
{
	/// Where the air is at freezing, metres above the datum.
	///
	/// The environmental lapse rate, not the dry adiabatic one: the boundary
	/// belongs to the atmosphere a drop is falling through rather than to a
	/// parcel rising off the ground.
	///
	/// `SurfaceKelvin` is the temperature *at this place*, which is not the
	/// profile's datum temperature -- a pole and an equator on the same planet
	/// have the same air and different freezing levels. Pass zero for the
	/// datum. The caller supplies it rather than this working it out because
	/// the latitude model lives a layer up, in the terrain, and a second copy
	/// of it down here would be a second climate.
	LEDGERCORE_API double FreezingLevelMetres(
		const FLedgerAirProfile& Air, double SurfaceKelvin = 0.0);

	/// Rain or snow at an altitude, from the temperature there.
	///
	/// **Decided from the temperature and not from the freezing level**, which
	/// is what makes the boundary a consequence rather than a declaration: this
	/// walks the lapse rate down from the surface and asks whether the answer
	/// is below 273.15 K, and anything that wants to know where the changeover
	/// is has to go and find it.
	LEDGERCORE_API ELedgerPrecipitation FrozenOrNot(
		const FLedgerAirProfile& Air, double AltitudeMetres,
		double SurfaceKelvin = 0.0);

	/// Terminal velocity for a kind, metres per second.
	LEDGERCORE_API double FallSpeedMetresPerSecond(ELedgerPrecipitation Kind);

	/// Everything, at a place and an altitude and a time.
	LEDGERCORE_API FLedgerPrecipitation At(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double AltitudeMetres,
		double SecondsFromEpoch, double SurfaceKelvin = 0.0);
}
