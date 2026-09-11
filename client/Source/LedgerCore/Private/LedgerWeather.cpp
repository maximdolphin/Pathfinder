#include "LedgerWeather.h"

#include "LedgerMath.h"

#include <cmath>

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

	double CellAnomalyPascals(
		const FLedgerSystem& System, int32 BodyIndex,
		double LatitudeRadians, double LongitudeRadians, double SecondsFromEpoch)
	{
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return 0.0;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];

		TArray<FLedgerPressureCell> Live;
		CellsAt(System, BodyIndex, SecondsFromEpoch, Live);

		double Anomaly = 0.0;
		for (const FLedgerPressureCell& Cell : Live)
		{
			const double Distance = WeatherDistance(Body.RadiusMetres,
				LatitudeRadians, LongitudeRadians,
				Cell.LatitudeRadians, Cell.LongitudeRadians);
			const double Scaled = Distance / FMath::Max(Cell.RadiusMetres, 1.0);
			Anomaly += Cell.AnomalyPascals * FMath::Exp(-Scaled * Scaled);
		}
		return Anomaly;
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
		const double Background = Air.SurfacePressurePascals
			- 600.0 * FMath::Cos(Bands * LedgerPi);

		// Climate plus forecast. One falloff, written once: a second copy of
		// this loop in the rain model would be a second weather.
		return Background + CellAnomalyPascals(
			System, BodyIndex, LatitudeRadians, LongitudeRadians, SecondsFromEpoch);
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

	double RoughnessMetres()
	{
		// Open country with scattered obstacles. The terrain this runs on is
		// mountain and scrub rather than city or ocean, and the log profile is
		// insensitive to it -- a factor of ten in roughness moves the
		// ten-metre wind by about a fifth.
		return 0.05;
	}

	double BoundaryLayerMetres(const FLedgerBody& Body, double LatitudeRadians)
	{
		const double F = FMath::Abs(CoriolisAt(Body, LatitudeRadians));
		if (!(F > 1e-6))
		{
			// At the equator the formula is unbounded, and physically the
			// tropical boundary layer really is deep. Capped at two kilometres,
			// which is about where the trade inversion sits.
			return 2000.0;
		}
		// The Ekman depth, with a friction velocity of about a twentieth of the
		// free wind: h ~ 0.3 u* / f.
		constexpr double FrictionVelocity = 0.5;
		return FMath::Clamp(0.3 * FrictionVelocity / F, 150.0, 2000.0);
	}

	FVector2D WindAtAltitude(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double AltitudeMetres,
		double SecondsFromEpoch)
	{
		const FVector2D Free = WindAt(
			System, BodyIndex, Air, LatitudeRadians, LongitudeRadians,
			SecondsFromEpoch);
		if (!System.Bodies.IsValidIndex(BodyIndex) || Free.IsNearlyZero())
		{
			return Free;
		}

		const FLedgerBody& Body = System.Bodies[BodyIndex];
		const double Depth = BoundaryLayerMetres(Body, LatitudeRadians);
		const double Height = FMath::Max(AltitudeMetres, RoughnessMetres() * 1.01);
		if (Height >= Depth)
		{
			return Free;
		}

		// **The log profile.** u(z) / u(h) = ln(z / z0) / ln(h / z0). Two thirds
		// at ten metres of what the free wind is at a kilometre, which is what a
		// met mast reads.
		const double Roughness = RoughnessMetres();
		const double Fraction = FMath::Clamp(
			FMath::Loge(Height / Roughness) / FMath::Loge(Depth / Roughness),
			0.0, 1.0);

		// **And the backing.** Friction breaks the geostrophic balance, so the
		// surface wind crosses the isobars towards the low. The angle is largest
		// at the ground and zero at the top of the layer, and its sign is the
		// hemisphere's -- in the north the wind backs anticlockwise.
		const double Turn = FMath::DegreesToRadians(25.0) * (1.0 - Fraction);
		const double Sign = LatitudeRadians >= 0.0 ? 1.0 : -1.0;
		const double Angle = -Sign * Turn;

		const double Cos = FMath::Cos(Angle);
		const double Sin = FMath::Sin(Angle);
		return FVector2D(
			static_cast<float>((Free.X * Cos - Free.Y * Sin) * Fraction),
			static_cast<float>((Free.X * Sin + Free.Y * Cos) * Fraction));
	}
}

namespace
{
	/// A low this deep is a storm, and at StormFullPascals it is the worst the
	/// schedule makes. Lows peak between 1500 and 4000 Pa (CellAt), so this is
	/// the deepest few near the height of their lives.
	constexpr double StormOnsetPascals = 2500.0;
	constexpr double StormFullPascals = 4000.0;

	/// **A front, not a gradient.** The first flight into a storm met a
	/// severity that crept up from nothing across hundreds of kilometres, so
	/// nothing changed on entry because there was no entry. A storm has an
	/// edge -- the squall line along its front -- and this is how sharp it is:
	/// half the severity within two kilometres of the onset, and the rest of
	/// the way to the core after it. A distance, not pascals: a hundred pascals
	/// past the onset was thirty-five kilometres at the edge of a deep low, and
	/// the second flight in met a front that took three minutes to cross.
	constexpr double StormFrontMetres = 2000.0;

	/// A dry world's equivalent: wind over the dust threshold, and how far over
	/// it the dust storm is as bad as it gets.
	constexpr double StormDustOnsetMetresPerSecond = 17.0;
	constexpr double StormDustSpanMetresPerSecond = 15.0;

	/// A severe thunderstorm flashes several times a minute within ten
	/// kilometres; this is the worst of them.
	constexpr double StormFlashesPerMinute = 8.0;

	/// RMS gust at full severity, metres per second. Severe convective
	/// turbulence is gusts of twenty metres a second and more.
	constexpr double StormGustMetresPerSecond = 18.0;
}

namespace LedgerWeather
{
	FLedgerStorm StormAt(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double SecondsFromEpoch)
	{
		FLedgerStorm Out;
		if (!System.Bodies.IsValidIndex(BodyIndex) || !Air.HasAir())
		{
			return Out;
		}
		if (!Air.bHasClouds)
		{
			const double Speed = WindAt(System, BodyIndex, Air,
				LatitudeRadians, LongitudeRadians, SecondsFromEpoch).Size();
			Out.bDust = true;
			Out.Severity = FMath::Clamp((Speed - StormDustOnsetMetresPerSecond)
				/ StormDustSpanMetresPerSecond, 0.0, 1.0);
		}
		else
		{
			const double Depth = -CellAnomalyPascals(System, BodyIndex,
				LatitudeRadians, LongitudeRadians, SecondsFromEpoch);
			const double Over = Depth - StormOnsetPascals;
			if (Over > 0.0)
			{
				// How far inside the edge this is: the depth past the onset over the
				// local gradient, found a kilometre either way.
				const double Step = 1000.0 / System.Bodies[BodyIndex].RadiusMetres;
				const double CosLat = FMath::Max(FMath::Cos(LatitudeRadians), 1.0e-3);
				const double North = CellAnomalyPascals(System, BodyIndex, LatitudeRadians + Step, LongitudeRadians, SecondsFromEpoch)
					- CellAnomalyPascals(System, BodyIndex, LatitudeRadians - Step, LongitudeRadians, SecondsFromEpoch);
				const double East = CellAnomalyPascals(System, BodyIndex, LatitudeRadians, LongitudeRadians + Step / CosLat, SecondsFromEpoch)
					- CellAnomalyPascals(System, BodyIndex, LatitudeRadians, LongitudeRadians - Step / CosLat, SecondsFromEpoch);
				const double Inside = Over / FMath::Max(FMath::Sqrt(North * North + East * East) / 2000.0, 1.0e-9);
				Out.Severity = 0.5 * FMath::Min(Inside / StormFrontMetres, 1.0)
					+ 0.5 * FMath::Min(Over / (StormFullPascals - StormOnsetPascals), 1.0);
			}
			// As the square: charge separation takes both the updraught and
			// the depth of cloud, and both grow with how deep the low is.
			Out.FlashesPerMinute = StormFlashesPerMinute * Out.Severity * Out.Severity;
		}
		Out.GustMetresPerSecond = StormGustMetresPerSecond * Out.Severity;
		return Out;
	}

	FVector3d GustAt(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians, double AltitudeMetres,
		double SecondsFromEpoch)
	{
		const FLedgerStorm Storm = StormAt(System, BodyIndex, Air,
			LatitudeRadians, LongitudeRadians, SecondsFromEpoch);
		if (!(Storm.GustMetresPerSecond > 0.0))
		{
			return FVector3d::ZeroVector;
		}
		const double Radius = System.Bodies[BodyIndex].RadiusMetres;
		const double East = LongitudeRadians * Radius * FMath::Cos(LatitudeRadians);
		const double North = LatitudeRadians * Radius;

		// ponytail: four seeded plane waves, not a turbulence spectrum. Enough
		// to be felt and the same every time; a von Karman spectrum when T098
		// wants the shape of real turbulence. The seam at the antimeridian is
		// a jump in phase nobody will fly a storm across.
		FVector3d Gust = FVector3d::ZeroVector;
		for (int32 Wave = 0; Wave < 4; ++Wave)
		{
			const int32 Index = 1000 + BodyIndex * 16 + Wave;
			const double Heading = WeatherBetween(System.Seed, Index, 0, 0.0, LedgerTwoPi);
			const double Wavelength = WeatherBetween(System.Seed, Index, 1, 150.0, 900.0);
			const double Period = WeatherBetween(System.Seed, Index, 2, 4.0, 20.0);
			const double Along = East * FMath::Cos(Heading) + North * FMath::Sin(Heading)
				+ AltitudeMetres * WeatherBetween(System.Seed, Index, 3, -0.5, 0.5);
			const double Phase = LedgerTwoPi * (Along / Wavelength + SecondsFromEpoch / Period)
				+ WeatherBetween(System.Seed, Index, 4, 0.0, LedgerTwoPi);
			// Each wave pushes its own way, with a vertical part: the
			// updraughts and downdraughts are most of what a storm does to an
			// aircraft.
			const FVector3d Push = FVector3d(
				FMath::Cos(Heading + 1.3 * Wave), FMath::Sin(Heading + 1.3 * Wave),
				WeatherBetween(System.Seed, Index, 5, -0.8, 0.8)).GetSafeNormal();
			Gust += Push * FMath::Sin(Phase);
		}
		// Four unit waves at unrelated phases have a mean square of two.
		return Gust * (Storm.GustMetresPerSecond / FMath::Sqrt(2.0));
	}
}

namespace LedgerWeather
{
	FLedgerForecast ForecastAt(
		const FLedgerSystem& System, int32 BodyIndex, const FLedgerAirProfile& Air,
		double LatitudeRadians, double LongitudeRadians,
		double IssuedSeconds, double ValidSeconds)
	{
		FLedgerForecast Out;
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return Out;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];
		const int32 Cells = CirculationCells(Body);
		const double Edge = (LedgerPi * 0.5) / Cells;

		double Mean = 0.0;
		double Variance = 0.0;
		for (int32 Index = 0; Index < WeatherCellCount; ++Index)
		{
			int32 Issued = 0;
			int32 Valid = 0;
			double Age = 0.0;
			double Lifetime = 0.0;
			double Born = 0.0;
			WeatherGeneration(System.Seed, Index, IssuedSeconds, Issued, Age, Lifetime, Born);
			WeatherGeneration(System.Seed, Index, ValidSeconds, Valid, Age, Lifetime, Born);

			if (Valid == Issued)
			{
				// Alive when the forecast was issued: where it will be is known.
				const FLedgerPressureCell Cell = CellAt(System, BodyIndex, Index, ValidSeconds);
				const double Scaled = WeatherDistance(Body.RadiusMetres, LatitudeRadians,
					LongitudeRadians, Cell.LatitudeRadians, Cell.LongitudeRadians)
					/ FMath::Max(Cell.RadiusMetres, 1.0);
				Mean += Cell.AnomalyPascals * FMath::Exp(-Scaled * Scaled);
				++Out.KnownCells;
				continue;
			}

			// Not yet born. Its strength at the valid time is the slot's; where
			// it forms, how big and how deep are drawn the way CellAt draws
			// them, from a stream no cell uses -- so this is an expectation over
			// what it could be, not a peek at what it will be.
			++Out.UnbornCells;
			const double Strength = FMath::Sin(Age * LedgerPi);
			const double Elapsed = ValidSeconds - Born;
			constexpr int32 Draws = 256;
			const uint32 Seed = System.Seed ^ 0xA5A5A5A5u;
			double Sum = 0.0;
			double Squares = 0.0;
			for (int32 Draw = 0; Draw < Draws; ++Draw)
			{
				const int32 Stream = Draw * 16;
				const int32 Boundary = Cells > 1
					? 1 + static_cast<int32>(WeatherRandom(Seed, Index, Stream) * (Cells - 1)) : 1;
				const double Hemisphere = WeatherRandom(Seed, Index, Stream + 1) < 0.5 ? -1.0 : 1.0;
				const double BirthLatitude = Hemisphere * FMath::Min(Boundary * Edge
					+ WeatherBetween(Seed, Index, Stream + 2, -Edge * 0.25, Edge * 0.25), LedgerPi * 0.48);
				const double Circumference = LedgerTwoPi * Body.RadiusMetres
					* FMath::Max(FMath::Cos(BirthLatitude), 0.05);
				const double Longitude = WeatherBetween(Seed, Index, Stream + 3, -LedgerPi, LedgerPi)
					+ LedgerTwoPi * ZonalWindAt(Body, BirthLatitude) * Elapsed / Circumference;
				const double Latitude = BirthLatitude + Hemisphere * 3.0 * Elapsed / Body.RadiusMetres;
				const bool bLow = WeatherRandom(Seed, Index, Stream + 4) < 0.6;
				const double Peak = bLow
					? -WeatherBetween(Seed, Index, Stream + 5, 1500.0, 4000.0)
					: WeatherBetween(Seed, Index, Stream + 6, 800.0, 2000.0);
				const double Radius = WeatherBetween(Seed, Index, Stream + 7, 0.10, 0.22) * Body.RadiusMetres;
				const double Scaled = WeatherDistance(Body.RadiusMetres, LatitudeRadians,
					LongitudeRadians, Latitude, Longitude) / Radius;
				const double Anomaly = Peak * Strength * FMath::Exp(-Scaled * Scaled);
				Sum += Anomaly;
				Squares += Anomaly * Anomaly;
			}
			const double DrawMean = Sum / Draws;
			Mean += DrawMean;
			Variance += FMath::Max(Squares / Draws - DrawMean * DrawMean, 0.0);
		}

		Out.AnomalyPascals = Mean;
		Out.UncertaintyPascals = FMath::Sqrt(Variance);
		// The background is climate and is known exactly: the pressure less the
		// cells, which cancels whatever the cells actually do.
		Out.PressurePascals = PressureAt(System, BodyIndex, Air, LatitudeRadians, LongitudeRadians, ValidSeconds)
			- CellAnomalyPascals(System, BodyIndex, LatitudeRadians, LongitudeRadians, ValidSeconds)
			+ Mean;

		// ponytail: the precipitation model rains under a low deeper than
		// 300 Pa, repeated here; and the spread is taken as normal, which the
		// tail of a newborn storm is not quite.
		constexpr double RainOnsetPascals = 300.0;
		if (Air.bHasClouds)
		{
			Out.RainChance = Out.UncertaintyPascals > 0.0
				? 0.5 * std::erfc((Mean + RainOnsetPascals) / (Out.UncertaintyPascals * FMath::Sqrt(2.0)))
				: (Mean < -RainOnsetPascals ? 1.0 : 0.0);
		}
		return Out;
	}
}

namespace LedgerWeather
{
	void DeepestLows(const FLedgerSystem& System, int32 BodyIndex,
		double SecondsFromEpoch, int32 Count, TArray<FLedgerPressureCell>& Out)
	{
		CellsAt(System, BodyIndex, SecondsFromEpoch, Out);
		Out.RemoveAll([](const FLedgerPressureCell& Cell) { return !Cell.bLow; });
		Out.Sort([](const FLedgerPressureCell& A, const FLedgerPressureCell& B)
		{
			return A.AnomalyPascals < B.AnomalyPascals;
		});
		if (Out.Num() > Count)
		{
			Out.SetNum(Count);
		}
	}
}
