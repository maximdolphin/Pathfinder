#include "LedgerMap.h"

#include "LedgerEphemeris.h"
#include "LedgerFrames.h"
#include "LedgerMath.h"

namespace
{
	/// How many times the intercept is re-solved. The iteration converges
	/// geometrically -- each pass moves the aim point by how far the target
	/// travelled during the last pass's error -- and twenty is far past the
	/// point where a double stops changing for anything in a planetary system.
	constexpr int32 MapInterceptPasses = 20;

	/// Solve for the trip and the aim point together, since neither is knowable
	/// without the other.
	bool MapSolve(
		const FLedgerSystem& System, const FLedgerSite& From, const FLedgerSite& To,
		double SecondsFromEpoch, double Acceleration,
		double& OutSeconds, FVector3d& OutIntercept)
	{
		if (!(Acceleration > 0.0)
			|| !System.Bodies.IsValidIndex(From.BodyIndex)
			|| !System.Bodies.IsValidIndex(To.BodyIndex))
		{
			return false;
		}

		const FVector3d Start = LedgerMap::PositionAt(System, From, SecondsFromEpoch);
		const FVector3d Relative =
			LedgerMap::VelocityAt(System, From, SecondsFromEpoch)
			- LedgerMap::VelocityAt(System, To, SecondsFromEpoch);

		double Seconds = 0.0;
		FVector3d Intercept = LedgerMap::PositionAt(System, To, SecondsFromEpoch);

		for (int32 Pass = 0; Pass < MapInterceptPasses; ++Pass)
		{
			const FVector3d Along = Intercept - Start;
			const double Distance = Along.Length();
			if (!(Distance > 0.0))
			{
				break;
			}

			// Positive is closing: already moving the right way, so the flip
			// comes later than halfway and the trip is shorter than from rest.
			const double Closing =
				FVector3d::DotProduct(Relative, Along / Distance);
			Seconds = (2.0 * FMath::Sqrt(Closing * Closing * 0.5
				+ Acceleration * Distance) - Closing) / Acceleration;
			Intercept = LedgerMap::PositionAt(System, To, SecondsFromEpoch + Seconds);
		}

		OutSeconds = Seconds;
		OutIntercept = Intercept;
		return true;
	}
}

namespace LedgerMap
{
	void Sites(const FLedgerSystem& System, TArray<FLedgerSite>& Out)
	{
		Out.Reset();
		Out.Reserve(System.Bodies.Num());
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			FLedgerSite Site;
			Site.Name = System.Bodies[Index].Name;
			Site.BodyIndex = Index;
			// The prime meridian on the equator: an arbitrary choice, and the
			// only defensible one for a body nobody has landed on yet.
			Site.AnchorDirection = FVector3d::UnitX();
			Out.Add(Site);
		}
	}

	FVector3d PositionAt(
		const FLedgerSystem& System, const FLedgerSite& Site, double SecondsFromEpoch)
	{
		if (!System.Bodies.IsValidIndex(Site.BodyIndex))
		{
			return FVector3d::ZeroVector;
		}

		FLedgerBodyPoint Point;
		Point.BodyIndex = Site.BodyIndex;
		Point.Metres = Site.AnchorDirection.GetSafeNormal()
			* (System.Bodies[Site.BodyIndex].RadiusMetres + Site.AltitudeMetres);
		return LedgerFrames::ToSystem(System, Point, SecondsFromEpoch).Metres;
	}

	FVector3d VelocityAt(
		const FLedgerSystem& System, const FLedgerSite& Site, double SecondsFromEpoch)
	{
		// A second either side. Short enough that a body's spin is linear over
		// it, long enough that the difference of two positions at 1e11 m does
		// not lose its significant digits.
		constexpr double Half = 1.0;
		return (PositionAt(System, Site, SecondsFromEpoch + Half)
			- PositionAt(System, Site, SecondsFromEpoch - Half)) / (2.0 * Half);
	}

	double DistanceMetres(
		const FLedgerSystem& System, const FLedgerSite& From, const FLedgerSite& To,
		double SecondsFromEpoch)
	{
		return (PositionAt(System, To, SecondsFromEpoch)
			- PositionAt(System, From, SecondsFromEpoch)).Length();
	}

	double TravelTimeSeconds(
		const FLedgerSystem& System, const FLedgerSite& From, const FLedgerSite& To,
		double SecondsFromEpoch, double AccelerationMetresPerSecond2)
	{
		double Seconds = 0.0;
		FVector3d Intercept = FVector3d::ZeroVector;
		if (!MapSolve(System, From, To, SecondsFromEpoch,
			AccelerationMetresPerSecond2, Seconds, Intercept))
		{
			return -1.0;
		}
		return Seconds;
	}

	FVector3d InterceptPosition(
		const FLedgerSystem& System, const FLedgerSite& From, const FLedgerSite& To,
		double SecondsFromEpoch, double AccelerationMetresPerSecond2)
	{
		double Seconds = 0.0;
		FVector3d Intercept = FVector3d::ZeroVector;
		if (!MapSolve(System, From, To, SecondsFromEpoch,
			AccelerationMetresPerSecond2, Seconds, Intercept))
		{
			return FVector3d::ZeroVector;
		}
		return Intercept;
	}
}
