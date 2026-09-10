#include "LedgerWeather.h"

#include "LedgerMath.h"

namespace
{
	/// How many pressure cells the schedule holds at once. Earth's northern
	/// hemisphere carries half a dozen synoptic systems at any moment and the
	/// southern much the same, so a dozen and a half over a whole planet is the
	/// right order rather than a budget.
	constexpr int32 WeatherCellCount = 18;

	/// How long one lives, seconds. A mid-latitude cyclone spins up over a day,
	/// runs for three or four, and fills. The spread is what stops them all
	/// dying at once.
	constexpr double WeatherLifeLowSeconds = 3.0 * 86400.0;
	constexpr double WeatherLifeHighSeconds = 8.0 * 86400.0;

	/// Cells are born on a stagger rather than together: one every few hours,
	/// which keeps the sky populated without ever having eighteen new storms.
	constexpr double WeatherBirthSpacingSeconds = 9.0 * 3600.0;

	double WeatherRandom(uint32 Seed, int32 Index, int32 Stream)
	{
		uint64 X = static_cast<uint64>(Seed) * 0x9E3779B97F4A7C15ull
			^ static_cast<uint64>(static_cast<uint32>(Index)) * 0xBF58476D1CE4E5B9ull
			^ static_cast<uint64>(static_cast<uint32>(Stream)) * 0x94D049BB133111EBull;
		X ^= X >> 30; X *= 0xBF58476D1CE4E5B9ull;
		X ^= X >> 27; X *= 0x94D049BB133111EBull;
		X ^= X >> 31;
		return static_cast<double>(X >> 11) / 9007199254740992.0;
	}

	double WeatherBetween(uint32 Seed, int32 Index, int32 Stream, double Low, double High)
	{
		return Low + (High - Low) * WeatherRandom(Seed, Index, Stream);
	}

	/// Which generation of a cell slot is alive at a time, and how far into it.
	///
	/// A slot is reused: cell 3 is a different storm on Tuesday from the one it
	/// was on Sunday. The generation number goes into the seed, so the new storm
	/// is a new storm rather than the old one moved.
	void WeatherGeneration(
		uint32 Seed, int32 Index, double Seconds, int32& OutGeneration, double& OutAge,
		double& OutLifetime, double& OutBorn)
	{
		const double Offset = Index * WeatherBirthSpacingSeconds;
		// Every slot runs on its own cycle, whose length is its lifetime -- so a
		// slot is always occupied and the population is steady.
		OutLifetime = WeatherBetween(Seed, Index, 1,
			WeatherLifeLowSeconds, WeatherLifeHighSeconds);
		const double Since = Seconds + Offset;
		OutGeneration = static_cast<int32>(FMath::FloorToDouble(Since / OutLifetime));
		OutBorn = OutGeneration * OutLifetime - Offset;
		OutAge = FMath::Clamp((Seconds - OutBorn) / OutLifetime, 0.0, 1.0);
	}
}

namespace LedgerWeather
{
	double HadleyEdgeRadians(const FLedgerBody& Body)
	{
		if (!(Body.RotationPeriodSeconds > 0.0))
		{
			return LedgerPi * 0.5;
		}
		// Held and Hou: the cell's edge goes as the inverse square root of the
		// rotation rate. Anchored on Earth, which turns once in 86164 s and
		// whose Hadley cell reaches 30 degrees.
		const double Ratio = Body.RotationPeriodSeconds / 86164.0;
		const double Degrees = 30.0 * FMath::Sqrt(Ratio);
		return FMath::DegreesToRadians(FMath::Clamp(Degrees, 7.5, 90.0));
	}

	int32 CirculationCells(const FLedgerBody& Body)
	{
		const double Edge = HadleyEdgeRadians(Body);
		if (!(Edge > 0.0))
		{
			return 1;
		}
		return FMath::Clamp(
			FMath::RoundToInt((LedgerPi * 0.5) / Edge), 1, 8);
	}

	double CoriolisAt(const FLedgerBody& Body, double LatitudeRadians)
	{
		if (!(Body.RotationPeriodSeconds > 0.0))
		{
			return 0.0;
		}
		const double Omega = LedgerTwoPi / Body.RotationPeriodSeconds;
		return 2.0 * Omega * FMath::Sin(LatitudeRadians);
	}

	double ZonalWindAt(const FLedgerBody& Body, double LatitudeRadians)
	{
		const int32 Cells = CirculationCells(Body);
		const double Edge = (LedgerPi * 0.5) / Cells;
		const double Latitude = FMath::Abs(LatitudeRadians);

		// Which band, and how far through it. The first band from the equator is
		// the tropical one and blows easterly; they alternate outward.
		const double Position = Latitude / Edge;
		const int32 Band = FMath::Min(static_cast<int32>(Position), Cells - 1);
		const double Through = Position - Band;

		// A half-sine across each band, so the wind is zero at every boundary
		// and strongest in the middle. Boundaries are where air rises or sinks,
		// and rising air has no east-west preference.
		const double Shape = FMath::Sin(Through * LedgerPi);
		const double Direction = (Band % 2) == 0 ? -1.0 : 1.0;

		// **Strength comes from the rotation.** A faster body turns its
		// overturning into a stronger zonal jet; the scaling is with Omega
		// times the radius, cut to Earth's fifteen metres a second in the
		// westerlies.
		const double Omega = Body.RotationPeriodSeconds > 0.0
			? LedgerTwoPi / Body.RotationPeriodSeconds : 0.0;
		const double Reference = (LedgerTwoPi / 86164.0) * 6.371e6;
		const double Speed = 15.0 * (Omega * Body.RadiusMetres) / Reference;
		return Direction * Shape * FMath::Clamp(Speed, 0.0, 200.0);
	}

	int32 CellCount()
	{
		return WeatherCellCount;
	}

	FLedgerPressureCell CellAt(
		const FLedgerSystem& System, int32 BodyIndex, int32 CellIndex,
		double SecondsFromEpoch)
	{
		FLedgerPressureCell Out;
		if (!System.Bodies.IsValidIndex(BodyIndex)
			|| CellIndex < 0 || CellIndex >= WeatherCellCount)
		{
			return Out;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];
		Out.Index = CellIndex;

		int32 Generation = 0;
		double Lifetime = 0.0;
		double Born = 0.0;
		WeatherGeneration(System.Seed, CellIndex, SecondsFromEpoch,
			Generation, Out.Age, Lifetime, Born);

		// The generation goes into the seed, so reusing a slot makes a new storm
		// rather than teleporting the old one.
		const int32 Stream = CellIndex * 64 + (Generation & 0x3F) * 7 + BodyIndex;

		// **Born where storms are born.** Mid-latitude cyclones form on the
		// boundary between circulation cells, where warm air meets cold -- the
		// polar front. Which boundary depends on how many cells the body has, so
		// a fast rotator gets several storm tracks and a slow one gets none.
		const int32 Cells = CirculationCells(Body);
		const double Edge = (LedgerPi * 0.5) / Cells;
		const int32 Boundary = Cells > 1
			? 1 + static_cast<int32>(WeatherRandom(System.Seed, CellIndex, Stream + 2)
				* (Cells - 1))
			: 1;
		const double Hemisphere =
			WeatherRandom(System.Seed, CellIndex, Stream + 3) < 0.5 ? -1.0 : 1.0;
		const double BirthLatitude = Hemisphere * FMath::Min(
			Boundary * Edge + WeatherBetween(System.Seed, CellIndex, Stream + 4,
				-Edge * 0.25, Edge * 0.25),
			LedgerPi * 0.48);
		const double BirthLongitude = WeatherBetween(
			System.Seed, CellIndex, Stream + 5, -LedgerPi, LedgerPi);

		// **Where it has got to is arithmetic, not a record.** The cell is
		// carried east by the prevailing wind at its latitude and drifts
		// poleward as it ages, which is what mid-latitude cyclones do -- they
		// ride the westerlies and curl up towards the pole as they occlude.
		const double Elapsed = SecondsFromEpoch - Born;
		const double Carried = ZonalWindAt(Body, BirthLatitude) * Elapsed;
		const double Circumference = LedgerTwoPi * Body.RadiusMetres
			* FMath::Max(FMath::Cos(BirthLatitude), 0.05);

		Out.LongitudeRadians = FMath::UnwindRadians(
			BirthLongitude + LedgerTwoPi * (Carried / FMath::Max(Circumference, 1.0)));

		// Poleward at a few metres a second, for the whole life.
		const double Poleward = Hemisphere * 3.0 * Elapsed / Body.RadiusMetres;
		Out.LatitudeRadians = FMath::Clamp(
			BirthLatitude + Poleward, -LedgerPi * 0.49, LedgerPi * 0.49);

		// A storm spins up and fills. A half-sine over its life, so the field is
		// continuous when a slot turns over -- a pressure field with a step in
		// it has infinite wind, which is the sort of thing that shows up as a
		// gale nobody ordered.
		Out.Strength = FMath::Sin(Out.Age * LedgerPi);
		Out.bLow = WeatherRandom(System.Seed, CellIndex, Stream + 6) < 0.6;

		// Lows are deeper than highs are tall: 980 mb against 1030 is 35 down
		// and 20 up from a 1013 background, and that asymmetry is why storms
		// are events and fine weather is a lull.
		const double Peak = Out.bLow
			? -WeatherBetween(System.Seed, CellIndex, Stream + 7, 1500.0, 4000.0)
			: WeatherBetween(System.Seed, CellIndex, Stream + 8, 800.0, 2000.0);
		Out.AnomalyPascals = Peak * Out.Strength;
		Out.RadiusMetres = WeatherBetween(
			System.Seed, CellIndex, Stream + 9, 0.10, 0.22) * Body.RadiusMetres;
		return Out;
	}

	void CellsAt(
		const FLedgerSystem& System, int32 BodyIndex, double SecondsFromEpoch,
		TArray<FLedgerPressureCell>& Out)
	{
		Out.Reset();
		for (int32 Index = 0; Index < WeatherCellCount; ++Index)
		{
			const FLedgerPressureCell Cell =
				CellAt(System, BodyIndex, Index, SecondsFromEpoch);
			if (Cell.Index != INDEX_NONE && Cell.Strength > 0.01)
			{
				Out.Add(Cell);
			}
		}
	}

	/// Great-circle distance between two places on a body, metres.
	static double WeatherDistance(
		double RadiusMetres, double LatA, double LonA, double LatB, double LonB)
	{
		const double SinHalfLat = FMath::Sin((LatB - LatA) * 0.5);
		const double SinHalfLon = FMath::Sin((LonB - LonA) * 0.5);
		const double A = SinHalfLat * SinHalfLat
			+ FMath::Cos(LatA) * FMath::Cos(LatB) * SinHalfLon * SinHalfLon;
		return 2.0 * RadiusMetres
			* FMath::Atan2(FMath::Sqrt(A), FMath::Sqrt(FMath::Max(1.0 - A, 0.0)));
	}

	double PressureAt(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double SecondsFromEpoch)
	{
		if (!System.Bodies.IsValidIndex(BodyIndex) || !Air.HasAir())
		{
			return 0.0;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];

		// The zonal mean: high where each circulation cell descends, low where
		// it rises. That is the subtropical ridge and the subpolar trough, and
		// it is the reason deserts sit at thirty degrees.
		const int32 Cells = CirculationCells(Body);
		const double Edge = (LedgerPi * 0.5) / Cells;
		const double Bands = FMath::Abs(LatitudeRadians) / Edge;
		double Pressure = Air.SurfacePressurePascals
			- 600.0 * FMath::Cos(Bands * LedgerPi);

		TArray<FLedgerPressureCell> Live;
		CellsAt(System, BodyIndex, SecondsFromEpoch, Live);
		for (const FLedgerPressureCell& Cell : Live)
		{
			const double Distance = WeatherDistance(Body.RadiusMetres,
				LatitudeRadians, LongitudeRadians,
				Cell.LatitudeRadians, Cell.LongitudeRadians);
			const double Scaled = Distance / FMath::Max(Cell.RadiusMetres, 1.0);
			Pressure += Cell.AnomalyPascals * FMath::Exp(-Scaled * Scaled);
		}
		return Pressure;
	}

	FVector2D WindAt(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double SecondsFromEpoch)
	{
		if (!System.Bodies.IsValidIndex(BodyIndex) || !Air.HasAir())
		{
			return FVector2D::ZeroVector;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];

		// The gradient, by central difference over a tenth of a degree, which is
		// small against a storm and large against a double's last bits.
		const double Step = FMath::DegreesToRadians(0.1);
		const double North = PressureAt(System, BodyIndex, Air,
			LatitudeRadians + Step, LongitudeRadians, SecondsFromEpoch);
		const double South = PressureAt(System, BodyIndex, Air,
			LatitudeRadians - Step, LongitudeRadians, SecondsFromEpoch);
		const double East = PressureAt(System, BodyIndex, Air,
			LatitudeRadians, LongitudeRadians + Step, SecondsFromEpoch);
		const double West = PressureAt(System, BodyIndex, Air,
			LatitudeRadians, LongitudeRadians - Step, SecondsFromEpoch);

		const double MetresNorth = Body.RadiusMetres * Step;
		const double MetresEast = Body.RadiusMetres * Step
			* FMath::Max(FMath::Cos(LatitudeRadians), 1e-3);
		const double GradientNorth = (North - South) / (2.0 * MetresNorth);
		const double GradientEast = (East - West) / (2.0 * MetresEast);

		const double Density = Air.MolecularMassKg * Air.NumberDensityPerCubicMetre;
		if (!(Density > 0.0))
		{
			return FVector2D::ZeroVector;
		}

		// Geostrophic: v = (1 / (rho f)) k x grad p. The wind runs along the
		// isobars with the low on the left in the northern hemisphere.
		const double F = CoriolisAt(Body, LatitudeRadians);
		FVector2D Wind = FVector2D::ZeroVector;
		if (FMath::Abs(F) > 1e-6)
		{
			Wind = FVector2D(
				static_cast<float>(-GradientNorth / (Density * F)),
				static_cast<float>(GradientEast / (Density * F)));
		}

		// **Geostrophy has a hole at the equator**, where the Coriolis parameter
		// is zero and the formula divides by it. Inside the tropics the wind is
		// blended into a straight down-gradient flow, which is what actually
		// happens there -- tropical air really does blow from high to low.
		const double Tropic = FMath::DegreesToRadians(12.0);
		const double Blend = FMath::Clamp(
			FMath::Abs(LatitudeRadians) / Tropic, 0.0, 1.0);
		const FVector2D DownGradient = FVector2D(
			static_cast<float>(-GradientEast * 2.0e4 / Density),
			static_cast<float>(-GradientNorth * 2.0e4 / Density));

		Wind = Wind * static_cast<float>(Blend)
			+ DownGradient * static_cast<float>(1.0 - Blend);

		// Plus the prevailing band the whole thing sits in.
		Wind.X += static_cast<float>(ZonalWindAt(Body, LatitudeRadians));

		// Nothing on a planet blows at the speed of sound. This is a cap on a
		// model, not a physical limit, and it fires only where the gradient
		// method breaks down.
		const float Speed = Wind.Size();
		if (Speed > 150.0f)
		{
			Wind *= 150.0f / Speed;
		}
		return Wind;
	}
}
