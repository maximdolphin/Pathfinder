#include "LedgerAurora.h"

#include "LedgerMath.h"

namespace
{
	/// Earth, for the dipole's scale.
	constexpr double AuroraEarthMassKg = 5.972e24;
	constexpr double AuroraEarthRadiusMetres = 6.371e6;
	constexpr double AuroraEarthDaySeconds = 86164.0;

	/// Below this fraction of Earth's field there is no magnetosphere worth the
	/// name to funnel anything anywhere.
	constexpr double AuroraMinimumDipole = 0.01;

	/// The star's events: this many independent slots, each on its own cycle.
	constexpr int32 AuroraEventSlots = 3;
	constexpr double AuroraCycleLowSeconds = 8.0 * 86400.0;
	constexpr double AuroraCycleHighSeconds = 20.0 * 86400.0;

	/// Activity that counts as an event, on the Kp scale. Four is unsettled;
	/// five is a minor geomagnetic storm, where aurora starts to be reported.
	constexpr double AuroraEventKp = 4.5;

	double AuroraRandom(uint32 Seed, int32 Index, int32 Stream)
	{
		uint64 X = static_cast<uint64>(Seed) * 0xD6E8FEB86659FD93ull
			^ static_cast<uint64>(static_cast<uint32>(Index)) * 0xA0761D6478BD642Full
			^ static_cast<uint64>(static_cast<uint32>(Stream)) * 0xE7037ED1A0B428DBull;
		X ^= X >> 32; X *= 0xD6E8FEB86659FD93ull;
		X ^= X >> 32; X *= 0xD6E8FEB86659FD93ull;
		X ^= X >> 32;
		return static_cast<double>(X >> 11) / 9007199254740992.0;
	}

	double AuroraBetween(uint32 Seed, int32 Index, int32 Stream, double Low, double High)
	{
		return Low + (High - Low) * AuroraRandom(Seed, Index, Stream);
	}
}

namespace LedgerAurora
{
	double DipoleRelative(const FLedgerBody& Body)
	{
		if (Body.Kind == ELedgerBodyKind::Star || Body.Kind == ELedgerBodyKind::Asteroid
			|| Body.Kind == ELedgerBodyKind::Station || !(Body.RotationPeriodSeconds > 0.0))
		{
			return 0.0;
		}
		return (Body.MassKg / AuroraEarthMassKg)
			* FMath::Square(Body.RadiusMetres / AuroraEarthRadiusMetres)
			* (AuroraEarthDaySeconds / FMath::Abs(Body.RotationPeriodSeconds));
	}

	FVector3d MagneticPole(const FLedgerSystem& System, int32 BodyIndex)
	{
		const uint32 Seed = System.Seed ^ 0x6D61676Eu;
		const double Tilt = FMath::DegreesToRadians(AuroraBetween(Seed, BodyIndex, 0, 4.0, 14.0));
		const double Azimuth = AuroraBetween(Seed, BodyIndex, 1, 0.0, LedgerTwoPi);
		return FVector3d(FMath::Sin(Tilt) * FMath::Cos(Azimuth), FMath::Sin(Tilt) * FMath::Sin(Azimuth),
			FMath::Cos(Tilt));
	}

	double ActivityKp(const FLedgerSystem& System, double SecondsFromEpoch, bool* bOutEvent)
	{
		const uint32 Seed = System.Seed ^ 0x73756E73u;

		// The quiet background: one to two, wandering over a solar rotation.
		double Kp = 1.5 + 0.5 * FMath::Sin(LedgerTwoPi * SecondsFromEpoch / (27.0 * 86400.0));
		double EventKp = 0.0;

		// **Events on a schedule, like the weather's cells.** Each slot has its
		// own cycle; in each cycle an event arrives at a drawn moment, climbs to
		// its peak over three hours and decays over a day or so. Which cycle and
		// how far into it is arithmetic on the time.
		for (int32 Slot = 0; Slot < AuroraEventSlots; ++Slot)
		{
			const double Cycle = AuroraBetween(Seed, Slot, 0, AuroraCycleLowSeconds, AuroraCycleHighSeconds);
			const double Offset = AuroraBetween(Seed, Slot, 1, 0.0, Cycle);
			const double Since = SecondsFromEpoch + Offset;
			const int32 Generation = static_cast<int32>(FMath::FloorToDouble(Since / Cycle));
			const double Into = Since - Generation * Cycle;
			const int32 Stream = 8 + (Generation & 0xFFFF) * 4;
			const double Arrival = AuroraBetween(Seed, Slot, Stream, 0.0, Cycle * 0.5);
			const double Peak = AuroraBetween(Seed, Slot, Stream + 1, 5.0, 9.0);
			const double Decay = AuroraBetween(Seed, Slot, Stream + 2, 8.0, 20.0) * 3600.0;
			const double Age = Into - Arrival;
			if (Age < 0.0)
			{
				continue;
			}
			constexpr double Rise = 3.0 * 3600.0;
			const double Level = Age < Rise ? Peak * Age / Rise : Peak * FMath::Exp(-(Age - Rise) / Decay);
			EventKp = FMath::Max(EventKp, Level);
		}
		Kp = FMath::Min(FMath::Max(Kp, EventKp), 9.0);
		if (bOutEvent != nullptr)
		{
			*bOutEvent = EventKp >= AuroraEventKp;
		}
		return Kp;
	}

	FLedgerAurora At(const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double SecondsFromEpoch)
	{
		FLedgerAurora Out;
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return Out;
		}
		Out.Kp = ActivityKp(System, SecondsFromEpoch, &Out.bEvent);

		const double Dipole = DipoleRelative(System.Bodies[BodyIndex]);
		const FVector3d Pole = MagneticPole(System, BodyIndex);
		const FVector3d Here(FMath::Cos(LatitudeRadians) * FMath::Cos(LongitudeRadians),
			FMath::Cos(LatitudeRadians) * FMath::Sin(LongitudeRadians), FMath::Sin(LatitudeRadians));
		Out.MagneticLatitude = FMath::Asin(FMath::Clamp(FVector3d::DotProduct(Here, Pole), -1.0, 1.0));

		// **The oval moves towards the equator as the activity rises**: seventy
		// degrees magnetic in quiet times, sixty at Kp 5, about fifty-two at 9,
		// which is Earth's; a stronger field holds it nearer the pole.
		const double FromPole = (20.0 + 2.0 * Out.Kp)
			* FMath::Pow(FMath::Max(Dipole, AuroraMinimumDipole), -1.0 / 6.0);
		Out.OvalMagneticLatitude = FMath::DegreesToRadians(FMath::Clamp(90.0 - FromPole, 40.0, 85.0));
		Out.OvalHalfWidth = FMath::DegreesToRadians(3.0 + 0.6 * Out.Kp);

		// Nothing to glow without air, nothing to funnel it without a field,
		// and -- the acceptance's line -- nothing seen outside an event.
		// ponytail: the faint quiet-time oval real skies have is left out.
		if (!Air.HasAir() || Dipole < AuroraMinimumDipole || !Out.bEvent)
		{
			return Out;
		}
		Out.Strength = FMath::Clamp((Out.Kp - 4.0) / 3.0, 0.0, 1.0);
		const double Off = (FMath::Abs(Out.MagneticLatitude) - Out.OvalMagneticLatitude) / Out.OvalHalfWidth;
		Out.Overhead = Out.Strength * FMath::Exp(-Off * Off);
		return Out;
	}
}
