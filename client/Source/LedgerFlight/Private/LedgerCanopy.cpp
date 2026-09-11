#include "LedgerCanopy.h"

namespace
{
	constexpr double CanopyFreezingKelvin = 273.15;

	/// Glass wetted per second by a millimetre an hour of rain, standing still.
	/// Moving, the glass sweeps up more of it: drops fall at nine metres a
	/// second, so at ninety the glass meets ten times as many.
	constexpr double CanopyWetting = 0.08;
	constexpr double CanopyDropFallMetresPerSecond = 9.0;

	/// Water leaving: running off under its own weight, and blown off.
	constexpr double CanopyRunOffPerSecond = 0.3;
	constexpr double CanopyBlowOffPerMetre = 1.0 / 40.0;

	/// The airspeed at which a drop on the glass stops running down and starts
	/// running back -- the air's drag on it matching its weight -- and how much
	/// faster it takes to be running straight back.
	constexpr double CanopyStallMetresPerSecond = 25.0;
	constexpr double CanopyFlowSpanMetresPerSecond = 30.0;

	/// The glass follows the air outside over about a minute, sooner in a
	/// stream of it; the heater drives it to a warm hand's temperature faster.
	constexpr double CanopyGlassSeconds = 60.0;
	constexpr double CanopyHeaterKelvin = 313.0;
	constexpr double CanopyHeaterSeconds = 20.0;

	/// Fog forms per second per kelvin the glass is under the dew point, and
	/// dries per second per kelvin over it.
	constexpr double CanopyFoggingPerKelvin = 0.01;
	constexpr double CanopyClearingPerKelvin = 0.02;

	constexpr double CanopyFreezePerSecond = 0.2;
	constexpr double CanopyMeltPerSecondPer10K = 0.15;
	constexpr double CanopyDusting = 0.05;
	constexpr double CanopyWipePerSecond = 3.0;
}

namespace LedgerFlight
{
	void Expose(FLedgerCanopy& Canopy, const FLedgerCanopyWeather& Outside, double DeltaSeconds)
	{
		if (DeltaSeconds <= 0.0)
		{
			return;
		}
		if (Canopy.GlassKelvin <= 0.0)
		{
			Canopy.GlassKelvin = Outside.AmbientKelvin;
		}
		const double Speed = FMath::Max(Outside.AirspeedMetresPerSecond, 0.0);

		// The glass: towards the air outside, or towards the heater.
		const double Towards = Canopy.bHeater ? CanopyHeaterKelvin : Outside.AmbientKelvin;
		const double Seconds = Canopy.bHeater
			? CanopyHeaterSeconds : CanopyGlassSeconds / (1.0 + Speed / 50.0);
		Canopy.GlassKelvin += (Towards - Canopy.GlassKelvin) * FMath::Min(DeltaSeconds / Seconds, 1.0);

		// Water: what the glass sweeps up, less what runs off, blows off and is
		// wiped. The first term fills towards one and the second empties
		// towards zero, so the glass settles where they balance.
		const double Sweep = Outside.RainMillimetresPerHour * CanopyWetting
			* (1.0 + Speed / CanopyDropFallMetresPerSecond);
		const double Leaving = CanopyRunOffPerSecond + Speed * CanopyBlowOffPerMetre
			+ (Canopy.bWipers ? CanopyWipePerSecond : 0.0);
		Canopy.Water = FMath::Clamp(Canopy.Water
			+ (Sweep * (1.0 - Canopy.Water) - Leaving * Canopy.Water) * DeltaSeconds, 0.0, 1.0);
		Canopy.Flow = FMath::Clamp(
			(Speed - CanopyStallMetresPerSecond) / CanopyFlowSpanMetresPerSecond, -1.0, 1.0);

		// Ice: water on glass below freezing freezes; glass above it melts ice.
		if (Canopy.GlassKelvin < CanopyFreezingKelvin)
		{
			const double Frozen = Canopy.Water * CanopyFreezePerSecond * DeltaSeconds;
			Canopy.Ice = FMath::Min(Canopy.Ice + Frozen, 1.0);
			Canopy.Water -= Frozen;
		}
		else
		{
			Canopy.Ice = FMath::Max(Canopy.Ice - CanopyMeltPerSecondPer10K
				* (Canopy.GlassKelvin - CanopyFreezingKelvin) / 10.0 * DeltaSeconds, 0.0);
		}

		// Fog: the cabin's breath condenses on glass colder than its dew point
		// and dries off glass warmer than it. This is what the heater is for.
		const double Margin = Canopy.GlassKelvin - Outside.CabinDewPointKelvin;
		Canopy.Fog = FMath::Clamp(Canopy.Fog + (Margin < 0.0
			? -Margin * CanopyFoggingPerKelvin
			: -Margin * CanopyClearingPerKelvin) * DeltaSeconds, 0.0, 1.0);

		// Dust settles and the wipers take it with the water.
		Canopy.Dust = FMath::Clamp(Canopy.Dust
			+ (Outside.DustRate * CanopyDusting * (1.0 - Canopy.Dust)
				- (Canopy.bWipers ? CanopyWipePerSecond * Canopy.Dust : 0.0)) * DeltaSeconds, 0.0, 1.0);
	}
}
