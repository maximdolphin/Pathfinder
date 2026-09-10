#include "LedgerStarField.h"

#include "LedgerMath.h"

namespace
{
	/// A uniform double from a seed and a stream, by hashing. No shared state,
	/// so a caller can ask for star 40,000's temperature without generating the
	/// 39,999 before it.
	///
	/// **This was FNV-1a and the sky came out as a line.** Folding three
	/// consecutive small integers through FNV leaves the streams correlated:
	/// stream 1 chose the cosine of the latitude and stream 2 the longitude,
	/// and because the two moved together, all 819 naked-eye stars landed on a
	/// single one-dimensional arc across the sky.
	///
	/// **No test caught it.** The tests measured angles BETWEEN stars, and
	/// those were perfectly well behaved on a degenerate catalogue -- a
	/// constellation drawn on a wire still holds its shape across a solar
	/// system and still comes apart between two. It took drawing the chart.
	/// There is now a test for coverage as well, which is the assertion that
	/// was missing rather than a new kind of test.
	///
	/// SplitMix64's finalizer instead: three multiplies and three xorshifts,
	/// specifically designed so that neighbouring inputs decorrelate.
	double Uniform(uint32 Seed, int32 Index, int32 Stream)
	{
		uint64 X = static_cast<uint64>(Seed) * 0x9E3779B97F4A7C15ull
			^ static_cast<uint64>(static_cast<uint32>(Index)) * 0xBF58476D1CE4E5B9ull
			^ static_cast<uint64>(static_cast<uint32>(Stream)) * 0x94D049BB133111EBull;
		X ^= X >> 30;
		X *= 0xBF58476D1CE4E5B9ull;
		X ^= X >> 27;
		X *= 0x94D049BB133111EBull;
		X ^= X >> 31;
		// The top 53 bits, which is what a double can hold exactly.
		return static_cast<double>(X >> 11) / 9007199254740992.0;
	}
}

namespace LedgerStarField
{
	void Generate(
		uint32 Seed, int32 Count, double RadiusParsecs,
		TArray<FLedgerCatalogueStar>& Out)
	{
		Out.Reset();
		if (Count <= 0 || !(RadiusParsecs > 0.0))
		{
			return;
		}
		Out.Reserve(Count);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			FLedgerCatalogueStar Star;

			// Uniform in VOLUME, which is the cube root: sampling the radius
			// evenly would pile most of the catalogue near the observer and
			// give a sky of a few dozen enormous stars.
			const double Radius =
				RadiusParsecs * FMath::Pow(Uniform(Seed, Index, 0), 1.0 / 3.0);

			// Uniform on the sphere, which needs the cosine and not the angle.
			const double Z = 1.0 - 2.0 * Uniform(Seed, Index, 1);
			const double Ring = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
			const double Angle = LedgerTwoPi * Uniform(Seed, Index, 2);
			Star.PositionParsecs = FVector3d(
				Ring * FMath::Cos(Angle), Ring * FMath::Sin(Angle), Z) * Radius;

			// **Mostly faint.** A real luminosity function is steeply weighted
			// towards small dim stars -- there are far more red dwarfs than
			// giants, and a catalogue drawn flat would be a sky of beacons.
			// Cubing a uniform pushes the mass to the dim end and leaves a thin
			// bright tail, which is the shape that matters here.
			const double Draw = Uniform(Seed, Index, 3);
			Star.AbsoluteMagnitude = FMath::Lerp(-6.0, 16.0, FMath::Pow(Draw, 0.33));

			// Temperature correlates with brightness on the main sequence, so
			// the bright ones come out blue and the dim ones red without a
			// second independent draw deciding it.
			const double Hot = FMath::Clamp((10.0 - Star.AbsoluteMagnitude) / 16.0, 0.0, 1.0);
			Star.TemperatureKelvin = FMath::Lerp(2600.0, 22000.0, Hot * Hot)
				* FMath::Lerp(0.9, 1.1, Uniform(Seed, Index, 4));

			Out.Add(Star);
		}
	}

	double ApparentMagnitude(
		const FLedgerCatalogueStar& Star, const FVector3d& ObserverParsecs)
	{
		const double Distance = (Star.PositionParsecs - ObserverParsecs).Length();
		if (!(Distance > 0.0))
		{
			return -TNumericLimits<double>::Max();
		}
		// m = M + 5 log10(d/10pc). Five magnitudes per factor of a hundred in
		// brightness, which is a factor of ten in distance.
		return Star.AbsoluteMagnitude + 5.0 * FMath::LogX(10.0, Distance / 10.0);
	}

	void Visible(
		const TArray<FLedgerCatalogueStar>& Catalogue,
		const FVector3d& ObserverParsecs, double MagnitudeLimit,
		TArray<FLedgerSkyStar>& Out)
	{
		Out.Reset();
		for (int32 Index = 0; Index < Catalogue.Num(); ++Index)
		{
			const FLedgerCatalogueStar& Star = Catalogue[Index];
			const FVector3d Offset = Star.PositionParsecs - ObserverParsecs;
			const double Distance = Offset.Length();
			if (!(Distance > 0.0))
			{
				continue;
			}

			const double Magnitude = ApparentMagnitude(Star, ObserverParsecs);
			if (Magnitude > MagnitudeLimit)
			{
				continue;
			}

			FLedgerSkyStar Seen;
			Seen.Index = Index;
			Seen.Direction = Offset / Distance;
			Seen.ApparentMagnitude = Magnitude;
			Seen.TemperatureKelvin = Star.TemperatureKelvin;
			Seen.DistanceParsecs = Distance;
			Out.Add(Seen);
		}

		// Brightest first, which on this scale means smallest.
		Out.Sort([](const FLedgerSkyStar& A, const FLedgerSkyStar& B)
		{
			return A.ApparentMagnitude < B.ApparentMagnitude;
		});
	}
}
