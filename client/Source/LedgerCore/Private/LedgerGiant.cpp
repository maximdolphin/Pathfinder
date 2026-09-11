#include "LedgerGiant.h"

#include "LedgerMath.h"
#include "LedgerNoise.h"

namespace
{
	/// How many alternating jets from equator to pole. Jupiter has about seven
	/// each side; the seed moves it a little either way so two giants do not
	/// wear the same coat.
	double JetCount(uint32 Seed)
	{
		const uint32 Hash = (Seed * 2654435761u) ^ 0x9E3779B9u;
		return 5.0 + 4.0 * ((Hash >> 8) & 0xFFFF) / 65535.0;
	}

	/// A storm's centre, drifting with its belt.
	void StormCentre(uint32 Seed, int32 Index, double Seconds,
		double& OutLatitude, double& OutLongitude, double& OutSize)
	{
		uint64 X = static_cast<uint64>(Seed) * 0x9E3779B97F4A7C15ull
			^ static_cast<uint64>(static_cast<uint32>(Index)) * 0xBF58476D1CE4E5B9ull;
		X ^= X >> 30; X *= 0xBF58476D1CE4E5B9ull;
		X ^= X >> 27; X *= 0x94D049BB133111EBull;
		X ^= X >> 31;

		const double A = static_cast<double>((X >> 11) & 0x1FFFFF) / 2097152.0;
		const double B = static_cast<double>((X >> 32) & 0x1FFFFF) / 2097152.0;
		const double C = static_cast<double>((X >> 43) & 0x1FFFF) / 131072.0;

		// **Beside a shear line, not on it.** A storm sits where the wind on one
		// side runs past the wind on the other, which is what spins it and what
		// keeps it. Placed by picking a jet boundary rather than a latitude, so
		// a giant with more jets has more places to put storms.
		//
		// But NOT exactly on the boundary. The first version put them there and
		// the drift test caught what that means: a jet boundary is where the
		// wind reverses, so it is where the wind is zero, and nine storms sat
		// in perfectly still air for ever. Real ones are embedded in one of the
		// two jets and are carried by it -- which is also why they are lenses
		// rather than circles. Offset by a third of a jet's width, into
		// whichever side the draw picks.
		const double Jets = JetCount(Seed);
		const int32 Boundary = 1 + static_cast<int32>(A * (Jets - 1.0));
		const double Sign = B < 0.5 ? 1.0 : -1.0;
		const double Spacing = (LedgerPi * 0.5) / Jets;
		const double Side = C < 0.5 ? -1.0 : 1.0;
		OutLatitude = Sign * ((Boundary / Jets) * (LedgerPi * 0.5) + Side * Spacing * 0.33);

		// Carried around by the wind at its latitude, so it drifts and the
		// drift is the wind rather than an animation speed.
		const double Wind = LedgerGiant::ZonalWindAt(OutLatitude, Seed);
		OutLongitude = FMath::Fmod(
			B * LedgerTwoPi + Wind * Seconds * 1.0e-5 + 100.0 * LedgerTwoPi, LedgerTwoPi);

		OutSize = 0.04 + 0.10 * C;
	}
}

namespace LedgerGiant
{
	double ZonalWindAt(double LatitudeRadians, uint32 Seed)
	{
		const double Jets = JetCount(Seed);

		// Alternating, and weakest at the poles. The cosine envelope is what
		// stops the jets running right over the pole, where there is no room
		// left to go round.
		const double Envelope = FMath::Cos(LatitudeRadians);
		return FMath::Sin(LatitudeRadians * Jets * 2.0) * Envelope * Envelope;
	}

	double BandAt(double LatitudeRadians, uint32 Seed)
	{
		// The band structure is the wind's own sign: a belt is where the air
		// sinks between two jets. Taking it from the wind rather than drawing a
		// second stripe pattern is what keeps the two from disagreeing.
		const double Jets = JetCount(Seed);
		return FMath::Cos(LatitudeRadians * Jets * 2.0);
	}

	double StormAt(
		double LatitudeRadians, double LongitudeRadians, uint32 Seed, double Seconds)
	{
		constexpr int32 StormCount = 9;
		double Strongest = 0.0;
		for (int32 Index = 0; Index < StormCount; ++Index)
		{
			double Latitude = 0.0;
			double Longitude = 0.0;
			double Size = 0.0;
			StormCentre(Seed, Index, Seconds, Latitude, Longitude, Size);

			// Ovals, not circles: stretched along the band, because the wind
			// shears anything round into a lens within a few rotations.
			double DeltaLongitude = FMath::Fmod(
				LongitudeRadians - Longitude + 3.0 * LedgerPi, LedgerTwoPi) - LedgerPi;
			const double DeltaLatitude = LatitudeRadians - Latitude;
			const double Across = DeltaLongitude * FMath::Cos(LatitudeRadians) / (Size * 2.6);
			const double Along = DeltaLatitude / Size;

			const double Distance = FMath::Sqrt(Across * Across + Along * Along);
			Strongest = FMath::Max(Strongest,
				1.0 - FMath::SmoothStep(0.35, 1.0, Distance));
		}
		return Strongest;
	}

	double SurfaceAt(
		double LatitudeRadians, double LongitudeRadians, uint32 Seed, double Seconds,
		double SpacingRadians)
	{
		const double Band = BandAt(LatitudeRadians, Seed);

		// **Turbulence sampled along the flow, not across it.** The longitude
		// is offset by the wind at this latitude times the time, so the detail
		// inside a band travels with that band. Sampling a plain 3D noise here
		// would give a giant whose clouds sit still while its jets run, which
		// is the single most obvious way to make one look painted.
		const double Wind = ZonalWindAt(LatitudeRadians, Seed);
		const double Flow = LongitudeRadians + Wind * Seconds * 1.0e-5;

		// Onto a cylinder, so longitude wraps without a seam.
		const FVector3d Sample(
			FMath::Cos(Flow) * 1.7,
			FMath::Sin(Flow) * 1.7,
			LatitudeRadians * 3.4);
		const double Turbulence = LedgerNoise::Fractal(Sample, Seed ^ 0x51ED2701u, 5);

		// **Finer eddies where the caller can see them.** The five octaves above
		// end at about two degrees, and below that the field was flat: a descent
		// over a band boundary, 3,000 km up, saw one soft gradient. Each further
		// octave halves the wavelength and keeps 0.7 of the amplitude -- a
		// flatter spectrum than the bands', because the small eddies on a giant
		// are not proportionally weaker -- and they stop once four samples no
		// longer span a wavelength, so nothing is added that would alias.
		double Detail = 0.0;
		if (SpacingRadians > 0.0)
		{
			double Scale = FMath::Pow(2.02, 5.0);
			double Amplitude = 1.0;
			// Latitude is the finer axis of the sample (3.4 a radian against 1.7).
			for (int32 Octave = 0; Octave < 12 && 1.0 / (3.4 * Scale) > 4.0 * SpacingRadians; ++Octave)
			{
				Detail += LedgerNoise::Gradient(Sample * Scale,
					(Seed ^ 0x2D1E7A11u) + static_cast<uint32>(Octave) * 7919u) * Amplitude;
				Amplitude *= 0.7;
				Scale *= 2.02;
			}
		}

		// The turbulence bends the band boundaries rather than being laid on
		// top of them, which is why it is added to the latitude's argument in
		// effect: a band edge that wobbles reads as fluid, one that is straight
		// reads as a decal.
		const double Bent = BandAt(
			LatitudeRadians + Turbulence * 0.06, Seed) * 0.5 + 0.5;

		const double Storm = StormAt(LatitudeRadians, LongitudeRadians, Seed, Seconds);
		return FMath::Clamp(
			FMath::Lerp(Bent, 1.0, Storm * 0.85) + Turbulence * 0.05 + Detail * 0.2 + Band * 0.0,
			0.0, 1.0);
	}
}
