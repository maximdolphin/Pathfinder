#include "LedgerPrecipitation.h"

#include "LedgerCloud.h"
#include "LedgerMath.h"
#include "LedgerWeather.h"

namespace
{
	/// Water freezes at 273.15 K and nothing else in this file is a constant of
	/// nature, so it is the only one named.
	constexpr double PrecipFreezingKelvin = 273.15;

	/// The anomaly a thoroughly wet low has, pascals.
	///
	/// A deep mid-latitude cyclone is about thirty hectopascals below the
	/// background, and thirty hectopascals of low is the sort that rains all
	/// day. The rate scale below is hung off this rather than off a number of
	/// millimetres, so a world whose storms are shallower gets less rain from
	/// the same arithmetic instead of the same rain from a different one.
	constexpr double PrecipDeepLowPascals = 3000.0;

	/// What a deep low delivers, millimetres of liquid an hour. Steady rain in
	/// a frontal system, not a thunderstorm -- convection is not modelled here
	/// and pretending otherwise with a bigger number would be worse.
	constexpr double PrecipDeepLowRate = 5.0;

	/// Below this the low is not organised enough to precipitate at all. Drizzle
	/// that never reaches the ground is a real thing and an invisible one.
	constexpr double PrecipThresholdPascals = 300.0;

	/// What it takes to lift dust, metres per second.
	///
	/// The threshold friction velocity for saltation on Mars is famously high
	/// -- the air is thin, so it takes a lot of speed to move a grain -- and
	/// this is the free-stream speed that corresponds to.
	constexpr double PrecipDustWindMetresPerSecond = 17.0;
}

const TCHAR* LexToString(ELedgerPrecipitation Kind)
{
	switch (Kind)
	{
	case ELedgerPrecipitation::Rain: return TEXT("rain");
	case ELedgerPrecipitation::Snow: return TEXT("snow");
	case ELedgerPrecipitation::Dust: return TEXT("dust");
	default:                         return TEXT("none");
	}
}

namespace LedgerPrecip
{
	double FreezingLevelMetres(const FLedgerAirProfile& Air, double SurfaceKelvin)
	{
		if (!(SurfaceKelvin > 0.0))
		{
			return LedgerCloud::HeightOfTemperature(Air, PrecipFreezingKelvin);
		}
		const double Lapse = LedgerCloud::EnvironmentalLapseRate(Air);
		return Lapse > 0.0
			? (SurfaceKelvin - PrecipFreezingKelvin) / Lapse
			: 0.0;
	}

	ELedgerPrecipitation FrozenOrNot(
		const FLedgerAirProfile& Air, double AltitudeMetres, double SurfaceKelvin)
	{
		// **The temperature there, not the freezing level.** Walking the lapse
		// rate down from the surface is a different route to the answer than
		// the one FreezingLevelMetres takes, which is the point: if the two
		// ever disagree, something is wrong, and a test can find out. A version
		// of this that compared the altitude against the freezing level would
		// agree with itself for ever and prove nothing.
		const double Lapse = LedgerCloud::EnvironmentalLapseRate(Air);
		const double Datum = SurfaceKelvin > 0.0
			? SurfaceKelvin : Air.SurfaceTemperatureKelvin;
		const double Kelvin = Datum - Lapse * AltitudeMetres;
		return Kelvin < PrecipFreezingKelvin
			? ELedgerPrecipitation::Snow
			: ELedgerPrecipitation::Rain;
	}

	double FallSpeedMetresPerSecond(ELedgerPrecipitation Kind)
	{
		switch (Kind)
		{
		// A two-millimetre drop falls at about nine metres a second, which is
		// where the drag on a sphere that size balances its weight.
		case ELedgerPrecipitation::Rain: return 9.0;
		// A snowflake is nearly all air. Same mass over ten times the area, so
		// an order of magnitude slower, and that -- not the colour -- is what
		// makes snow look like snow.
		case ELedgerPrecipitation::Snow: return 1.0;
		// Dust does not fall so much as hang. Below a millimetre a second it is
		// suspended by the turbulence that lifted it.
		case ELedgerPrecipitation::Dust: return 0.05;
		default:                         return 0.0;
		}
	}

	FLedgerPrecipitation At(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double AltitudeMetres,
		double SecondsFromEpoch, double SurfaceKelvin)
	{
		FLedgerPrecipitation Out;
		if (!System.Bodies.IsValidIndex(BodyIndex) || !Air.HasAir())
		{
			return Out;
		}

		Out.FreezingLevelMetres = FreezingLevelMetres(Air, SurfaceKelvin);
		Out.CloudBaseMetres = Air.CloudBaseMetres;
		Out.CloudTopMetres = Air.CloudTopMetres;

		// **No condensable water, no rain and no snow, at any pressure.** A
		// carbon-dioxide world with a gale on it fills the air with something,
		// and that something is the ground.
		if (!Air.bHasClouds)
		{
			const FVector2D Wind = LedgerWeather::WindAt(
				System, BodyIndex, Air, LatitudeRadians, LongitudeRadians,
				SecondsFromEpoch);
			if (Wind.Size() >= PrecipDustWindMetresPerSecond)
			{
				Out.Kind = ELedgerPrecipitation::Dust;
				// Not a depth of liquid, and quoted as one anyway so that every
				// consumer has one number to scale a density by. It is an
				// opacity in millimetre-an-hour clothing and the name says so.
				Out.RateMillimetresPerHour = FMath::Min(
					(Wind.Size() - PrecipDustWindMetresPerSecond) * 0.4, 6.0);
				Out.FallSpeedMetresPerSecond =
					FallSpeedMetresPerSecond(Out.Kind);
			}
			return Out;
		}

		// Rain happens where air is being made to rise, and air is made to rise
		// where the pressure is low. The anomaly alone rather than the pressure:
		// the subtropical ridge is a high everywhere and is not the reason it is
		// not raining in one place rather than another.
		const double Anomaly = LedgerWeather::CellAnomalyPascals(
			System, BodyIndex, LatitudeRadians, LongitudeRadians, SecondsFromEpoch);
		const double Depth = -Anomaly;
		if (Depth <= PrecipThresholdPascals)
		{
			return Out;
		}

		// Nothing falls out of the top of the cloud, and nothing falls at all
		// above it. Below the base it is falling through clear air, which is
		// where it is seen from.
		if (AltitudeMetres > Air.CloudTopMetres)
		{
			return Out;
		}

		Out.Kind = FrozenOrNot(Air, AltitudeMetres, SurfaceKelvin);
		Out.RateMillimetresPerHour = FMath::Min(
			PrecipDeepLowRate * (Depth - PrecipThresholdPascals)
				/ PrecipDeepLowPascals,
			PrecipDeepLowRate * 2.0);
		Out.FallSpeedMetresPerSecond = FallSpeedMetresPerSecond(Out.Kind);
		return Out;
	}
}
