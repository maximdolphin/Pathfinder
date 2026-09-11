// What the weather does to a canopy, and what a heater and wipers do back. T101.
//
// A small state the weather writes and the ship's systems clear: water that
// arrives with the rain and leaves with the airflow, ice when that water
// freezes, fog when the glass is colder than the cabin's dew point, and dust.
// Pure, like the flight model beside it, so minutes of it run in a test.

#pragma once

#include "CoreMinimal.h"

struct LEDGERFLIGHT_API FLedgerCanopy
{
	/// 0 clear to 1 streaming: water on the glass.
	double Water = 0.0;

	/// Which way the water runs: -1 straight down the glass under its own
	/// weight, +1 straight back along it in the airflow. The two forces on a
	/// drop, and which is winning.
	double Flow = -1.0;

	double Ice = 0.0;
	double Fog = 0.0;
	double Dust = 0.0;

	/// The glass's own temperature, kelvin. Zero until the first step.
	double GlassKelvin = 0.0;

	bool bHeater = false;
	bool bWipers = false;
};

/// What is outside the glass, and inside it.
struct LEDGERFLIGHT_API FLedgerCanopyWeather
{
	/// Water falling, rain or snow, millimetres an hour.
	double RainMillimetresPerHour = 0.0;

	/// The precipitation model's dust rate.
	double DustRate = 0.0;

	double AirspeedMetresPerSecond = 0.0;
	double AmbientKelvin = 288.0;

	/// The cabin air's dew point, kelvin. People breathing make it humid.
	double CabinDewPointKelvin = 288.0;
};

namespace LedgerFlight
{
	/// One step of the canopy under the weather.
	LEDGERFLIGHT_API void Expose(FLedgerCanopy& Canopy, const FLedgerCanopyWeather& Outside,
		double DeltaSeconds);
}
