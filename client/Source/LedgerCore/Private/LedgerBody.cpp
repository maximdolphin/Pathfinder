#include "LedgerBody.h"

#include "LedgerLog.h"
#include "Misc/DefaultValueHelper.h"

namespace
{
	/// Seventeen significant digits, which is what a double needs to survive a
	/// round trip. See the note on ToJson.
	FString Exact(double Value)
	{
		return FString::Printf(TEXT("%.17g"), Value);
	}

	/// One `"key": value` line at a given indent.
	FString Line(const TCHAR* Key, const FString& Value, int32 Indent, bool bLast = false)
	{
		return FString::ChrN(Indent, TEXT(' '))
			+ FString::Printf(TEXT("\"%s\": %s"), Key, *Value)
			+ (bLast ? TEXT("\n") : TEXT(",\n"));
	}

	FString Quoted(const FString& Text)
	{
		return FString::Printf(TEXT("\"%s\""), *Text.ReplaceCharWithEscapedChar());
	}

	/// A deterministic hash, so a seed produces the same system anywhere.
	/// Not the engine's -- FMath::Rand is a global with a shared state and a
	/// system generated after somebody else drew a number is a different
	/// system.
	uint64 Mix(uint64 Value)
	{
		Value ^= Value >> 33;
		Value *= 0xFF51AFD7ED558CCDull;
		Value ^= Value >> 33;
		Value *= 0xC4CEB9FE1A85EC53ull;
		Value ^= Value >> 33;
		return Value;
	}

	/// A double in [0,1) from a seed and a counter.
	double Uniform(uint32 Seed, uint32 Which)
	{
		const uint64 Hashed = Mix((static_cast<uint64>(Seed) << 32) | Which);
		return static_cast<double>(Hashed >> 11) / static_cast<double>(1ull << 53);
	}

	double Between(uint32 Seed, uint32 Which, double Low, double High)
	{
		return Low + Uniform(Seed, Which) * (High - Low);
	}
}

const TCHAR* LexToString(ELedgerBodyKind Kind)
{
	switch (Kind)
	{
	case ELedgerBodyKind::Star:     return TEXT("star");
	case ELedgerBodyKind::Planet:   return TEXT("planet");
	case ELedgerBodyKind::Moon:     return TEXT("moon");
	case ELedgerBodyKind::GasGiant: return TEXT("gas-giant");
	case ELedgerBodyKind::Asteroid: return TEXT("asteroid");
	case ELedgerBodyKind::Station:  return TEXT("station");
	}
	return TEXT("planet");
}

bool LexFromString(ELedgerBodyKind& Out, const TCHAR* Text)
{
	const FString Name(Text);
	if (Name == TEXT("star"))      { Out = ELedgerBodyKind::Star;     return true; }
	if (Name == TEXT("planet"))    { Out = ELedgerBodyKind::Planet;   return true; }
	if (Name == TEXT("moon"))      { Out = ELedgerBodyKind::Moon;     return true; }
	if (Name == TEXT("gas-giant")) { Out = ELedgerBodyKind::GasGiant; return true; }
	if (Name == TEXT("asteroid"))  { Out = ELedgerBodyKind::Asteroid; return true; }
	if (Name == TEXT("station"))   { Out = ELedgerBodyKind::Station;  return true; }
	return false;
}

bool FLedgerOrbit::operator==(const FLedgerOrbit& Other) const
{
	// Bit equality, deliberately. The acceptance is that a description rebuilds
	// *identically*, and a tolerance here would let this pass while quietly
	// moving a planet a hundred metres every time it was saved.
	return SemiMajorAxisMetres == Other.SemiMajorAxisMetres
		&& Eccentricity == Other.Eccentricity
		&& InclinationRadians == Other.InclinationRadians
		&& AscendingNodeRadians == Other.AscendingNodeRadians
		&& PeriapsisArgumentRadians == Other.PeriapsisArgumentRadians
		&& MeanAnomalyAtEpochRadians == Other.MeanAnomalyAtEpochRadians;
}

bool FLedgerBody::operator==(const FLedgerBody& Other) const
{
	return Name == Other.Name
		&& Kind == Other.Kind
		&& Seed == Other.Seed
		&& MassKg == Other.MassKg
		&& RadiusMetres == Other.RadiusMetres
		&& RotationPeriodSeconds == Other.RotationPeriodSeconds
		&& AxialTiltRadians == Other.AxialTiltRadians
		&& RotationAtEpochRadians == Other.RotationAtEpochRadians
		&& ParentIndex == Other.ParentIndex
		&& Orbit == Other.Orbit;
}

bool FLedgerSystem::operator==(const FLedgerSystem& Other) const
{
	return Seed == Other.Seed
		&& EpochSeconds == Other.EpochSeconds
		&& Bodies == Other.Bodies;
}

namespace LedgerBodies
{
	FString ToJson(const FLedgerSystem& System)
	{
		FString Out;
		Out += TEXT("{\n");
		Out += Line(TEXT("seed"), FString::Printf(TEXT("%u"), System.Seed), 2);
		Out += Line(TEXT("epochSeconds"), Exact(System.EpochSeconds), 2);
		Out += TEXT("  \"bodies\": [\n");

		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			const FLedgerBody& Body = System.Bodies[Index];
			Out += TEXT("    {\n");
			Out += Line(TEXT("name"), Quoted(Body.Name), 6);
			Out += Line(TEXT("kind"), Quoted(LexToString(Body.Kind)), 6);
			Out += Line(TEXT("seed"), FString::Printf(TEXT("%u"), Body.Seed), 6);
			Out += Line(TEXT("massKg"), Exact(Body.MassKg), 6);
			Out += Line(TEXT("radiusMetres"), Exact(Body.RadiusMetres), 6);
			Out += Line(TEXT("rotationPeriodSeconds"), Exact(Body.RotationPeriodSeconds), 6);
			Out += Line(TEXT("axialTiltRadians"), Exact(Body.AxialTiltRadians), 6);
			Out += Line(TEXT("rotationAtEpochRadians"), Exact(Body.RotationAtEpochRadians), 6);
			Out += Line(TEXT("parentIndex"), FString::Printf(TEXT("%d"), Body.ParentIndex), 6);
			Out += TEXT("      \"orbit\": {\n");
			Out += Line(TEXT("semiMajorAxisMetres"), Exact(Body.Orbit.SemiMajorAxisMetres), 8);
			Out += Line(TEXT("eccentricity"), Exact(Body.Orbit.Eccentricity), 8);
			Out += Line(TEXT("inclinationRadians"), Exact(Body.Orbit.InclinationRadians), 8);
			Out += Line(TEXT("ascendingNodeRadians"), Exact(Body.Orbit.AscendingNodeRadians), 8);
			Out += Line(TEXT("periapsisArgumentRadians"), Exact(Body.Orbit.PeriapsisArgumentRadians), 8);
			Out += Line(TEXT("meanAnomalyAtEpochRadians"),
				Exact(Body.Orbit.MeanAnomalyAtEpochRadians), 8, /*bLast*/ true);
			Out += TEXT("      }\n");
			Out += (Index + 1 < System.Bodies.Num()) ? TEXT("    },\n") : TEXT("    }\n");
		}

		Out += TEXT("  ]\n");
		Out += TEXT("}\n");
		return Out;
	}
}

namespace
{
	/// A tiny reader, because the shape being read is known and fixed.
	///
	/// Not the engine's JSON reader, and the reason is the whole point of this
	/// task: FJsonSerializer parses numbers into doubles through a path that
	/// does not promise to preserve the last bit, and "rebuilds identically" is
	/// the acceptance. This reads the digits back with FDefaultValueHelper,
	/// which round-trips %.17g exactly.
	struct FReader
	{
		const FString& Text;
		int32 At = 0;
		FString Error;

		explicit FReader(const FString& InText) : Text(InText) {}

		void SkipSpace()
		{
			while (At < Text.Len() && FChar::IsWhitespace(Text[At]))
			{
				++At;
			}
		}

		bool Expect(TCHAR Character)
		{
			SkipSpace();
			if (At >= Text.Len() || Text[At] != Character)
			{
				Error = FString::Printf(TEXT("expected '%c' at %d"), Character, At);
				return false;
			}
			++At;
			return true;
		}

		/// The next `"key"`, without consuming the colon.
		bool ReadKey(FString& Out)
		{
			SkipSpace();
			if (!Expect(TEXT('"')))
			{
				return false;
			}
			const int32 Start = At;
			while (At < Text.Len() && Text[At] != TEXT('"'))
			{
				++At;
			}
			Out = Text.Mid(Start, At - Start);
			return Expect(TEXT('"')) && Expect(TEXT(':'));
		}

		bool ReadString(FString& Out)
		{
			SkipSpace();
			if (!Expect(TEXT('"')))
			{
				return false;
			}
			const int32 Start = At;
			while (At < Text.Len() && Text[At] != TEXT('"'))
			{
				++At;
			}
			Out = Text.Mid(Start, At - Start);
			return Expect(TEXT('"'));
		}

		bool ReadNumber(double& Out)
		{
			SkipSpace();
			const int32 Start = At;
			while (At < Text.Len()
				&& (FChar::IsDigit(Text[At]) || Text[At] == TEXT('-') || Text[At] == TEXT('+')
					|| Text[At] == TEXT('.') || Text[At] == TEXT('e') || Text[At] == TEXT('E')))
			{
				++At;
			}
			const FString Digits = Text.Mid(Start, At - Start);
			if (Digits.IsEmpty() || !FDefaultValueHelper::ParseDouble(Digits, Out))
			{
				Error = FString::Printf(TEXT("bad number at %d: '%s'"), Start, *Digits);
				return false;
			}
			return true;
		}
	};
}

namespace LedgerBodies
{
	bool FromJson(const FString& Json, FLedgerSystem& Out, FString& OutError)
	{
		Out = FLedgerSystem();
		FReader Reader(Json);

		auto Fail = [&Reader, &OutError]()
		{
			OutError = Reader.Error.IsEmpty() ? TEXT("malformed system description") : Reader.Error;
			return false;
		};

		if (!Reader.Expect(TEXT('{'))) { return Fail(); }

		FString Key;
		double Number = 0.0;

		if (!Reader.ReadKey(Key) || Key != TEXT("seed") || !Reader.ReadNumber(Number)) { return Fail(); }
		Out.Seed = static_cast<uint32>(Number);
		if (!Reader.Expect(TEXT(','))) { return Fail(); }

		if (!Reader.ReadKey(Key) || Key != TEXT("epochSeconds") || !Reader.ReadNumber(Out.EpochSeconds)) { return Fail(); }
		if (!Reader.Expect(TEXT(','))) { return Fail(); }

		if (!Reader.ReadKey(Key) || Key != TEXT("bodies")) { return Fail(); }
		if (!Reader.Expect(TEXT('['))) { return Fail(); }

		Reader.SkipSpace();
		if (Reader.At < Json.Len() && Json[Reader.At] == TEXT(']'))
		{
			++Reader.At;
			return Reader.Expect(TEXT('}')) ? true : Fail();
		}

		for (;;)
		{
			if (!Reader.Expect(TEXT('{'))) { return Fail(); }
			FLedgerBody Body;

			if (!Reader.ReadKey(Key) || Key != TEXT("name") || !Reader.ReadString(Body.Name)) { return Fail(); }
			if (!Reader.Expect(TEXT(','))) { return Fail(); }

			FString KindText;
			if (!Reader.ReadKey(Key) || Key != TEXT("kind") || !Reader.ReadString(KindText)) { return Fail(); }
			if (!LexFromString(Body.Kind, *KindText))
			{
				OutError = FString::Printf(TEXT("'%s' is not a body kind"), *KindText);
				return false;
			}
			if (!Reader.Expect(TEXT(','))) { return Fail(); }

			if (!Reader.ReadKey(Key) || Key != TEXT("seed") || !Reader.ReadNumber(Number)) { return Fail(); }
			Body.Seed = static_cast<uint32>(Number);
			if (!Reader.Expect(TEXT(','))) { return Fail(); }

			const TCHAR* Names[] = { TEXT("massKg"), TEXT("radiusMetres"),
				TEXT("rotationPeriodSeconds"), TEXT("axialTiltRadians"),
				TEXT("rotationAtEpochRadians") };
			double* Fields[] = { &Body.MassKg, &Body.RadiusMetres,
				&Body.RotationPeriodSeconds, &Body.AxialTiltRadians,
				&Body.RotationAtEpochRadians };
			for (int32 Field = 0; Field < UE_ARRAY_COUNT(Names); ++Field)
			{
				if (!Reader.ReadKey(Key) || Key != Names[Field]
					|| !Reader.ReadNumber(*Fields[Field])) { return Fail(); }
				if (!Reader.Expect(TEXT(','))) { return Fail(); }
			}

			if (!Reader.ReadKey(Key) || Key != TEXT("parentIndex") || !Reader.ReadNumber(Number)) { return Fail(); }
			Body.ParentIndex = static_cast<int32>(Number);
			if (!Reader.Expect(TEXT(','))) { return Fail(); }

			if (!Reader.ReadKey(Key) || Key != TEXT("orbit")) { return Fail(); }
			if (!Reader.Expect(TEXT('{'))) { return Fail(); }

			const TCHAR* OrbitNames[] = { TEXT("semiMajorAxisMetres"), TEXT("eccentricity"),
				TEXT("inclinationRadians"), TEXT("ascendingNodeRadians"),
				TEXT("periapsisArgumentRadians"), TEXT("meanAnomalyAtEpochRadians") };
			double* OrbitFields[] = { &Body.Orbit.SemiMajorAxisMetres, &Body.Orbit.Eccentricity,
				&Body.Orbit.InclinationRadians, &Body.Orbit.AscendingNodeRadians,
				&Body.Orbit.PeriapsisArgumentRadians, &Body.Orbit.MeanAnomalyAtEpochRadians };
			for (int32 Field = 0; Field < UE_ARRAY_COUNT(OrbitNames); ++Field)
			{
				if (!Reader.ReadKey(Key) || Key != OrbitNames[Field]
					|| !Reader.ReadNumber(*OrbitFields[Field])) { return Fail(); }
				if (Field + 1 < UE_ARRAY_COUNT(OrbitNames) && !Reader.Expect(TEXT(','))) { return Fail(); }
			}
			if (!Reader.Expect(TEXT('}'))) { return Fail(); }
			if (!Reader.Expect(TEXT('}'))) { return Fail(); }

			Out.Bodies.Add(MoveTemp(Body));

			Reader.SkipSpace();
			if (Reader.At < Json.Len() && Json[Reader.At] == TEXT(','))
			{
				++Reader.At;
				continue;
			}
			break;
		}

		if (!Reader.Expect(TEXT(']'))) { return Fail(); }
		if (!Reader.Expect(TEXT('}'))) { return Fail(); }
		return true;
	}

	bool Validate(const FLedgerSystem& System, FString& OutError)
	{
		if (System.Bodies.Num() == 0)
		{
			OutError = TEXT("a system with no bodies is not a system");
			return false;
		}

		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			const FLedgerBody& Body = System.Bodies[Index];
			const FString Where = FString::Printf(TEXT("body %d (%s)"), Index, *Body.Name);

			if (!(Body.MassKg > 0.0) || !(Body.RadiusMetres > 0.0))
			{
				OutError = Where + TEXT(" has no mass or no radius");
				return false;
			}
			if (Body.Orbit.Eccentricity < 0.0 || Body.Orbit.Eccentricity >= 1.0)
			{
				OutError = Where + TEXT(" has an eccentricity outside [0,1): a body on an "
					"escape trajectory is not part of a system description");
				return false;
			}
			if (Body.ParentIndex == Index)
			{
				OutError = Where + TEXT(" orbits itself");
				return false;
			}
			if (Body.ParentIndex != INDEX_NONE && !System.Bodies.IsValidIndex(Body.ParentIndex))
			{
				OutError = Where + TEXT(" orbits a body that is not in this system");
				return false;
			}

			// Walk to the root. A cycle would otherwise hang whatever computes
			// positions, at which point it is a lock-up rather than a message.
			int32 Steps = 0;
			int32 Walk = Body.ParentIndex;
			while (Walk != INDEX_NONE)
			{
				if (++Steps > System.Bodies.Num())
				{
					OutError = Where + TEXT(" is in a cycle of parents");
					return false;
				}
				Walk = System.Bodies[Walk].ParentIndex;
			}
		}

		int32 Primaries = 0;
		for (const FLedgerBody& Body : System.Bodies)
		{
			Primaries += (Body.ParentIndex == INDEX_NONE) ? 1 : 0;
		}
		if (Primaries != 1)
		{
			OutError = FString::Printf(
				TEXT("a system has exactly one body that orbits nothing; this has %d"), Primaries);
			return false;
		}
		return true;
	}

	FLedgerSystem Generate(uint32 Seed)
	{
		FLedgerSystem System;
		System.Seed = Seed;
		System.EpochSeconds = 0.0;

		constexpr double AstronomicalUnit = 1.495978707e11;
		constexpr double SolarMass = 1.98847e30;
		constexpr double EarthMass = 5.97219e24;
		constexpr double EarthRadius = 6.371e6;

		FLedgerBody Star;
		Star.Name = TEXT("Primary");
		Star.Kind = ELedgerBodyKind::Star;
		Star.Seed = Seed;
		Star.MassKg = SolarMass * Between(Seed, 1, 0.8, 1.2);
		Star.RadiusMetres = 6.957e8 * Between(Seed, 2, 0.85, 1.15);
		Star.RotationPeriodSeconds = 86400.0 * Between(Seed, 3, 24.0, 30.0);
		Star.ParentIndex = INDEX_NONE;
		System.Bodies.Add(Star);

		// One habitable planet, which is the one the game is about, plus a moon.
		// More bodies are what T078 through T080 add; this is the description
		// the round trip has to survive, not a solar system.
		FLedgerBody Planet;
		Planet.Name = TEXT("Home");
		Planet.Kind = ELedgerBodyKind::Planet;
		Planet.Seed = static_cast<uint32>(Mix(Seed) & 0xFFFFFFFFull);
		Planet.MassKg = EarthMass * Between(Seed, 4, 0.85, 1.15);
		Planet.RadiusMetres = EarthRadius * Between(Seed, 5, 0.95, 1.05);
		Planet.RotationPeriodSeconds = Between(Seed, 6, 20.0, 30.0) * 3600.0;
		Planet.AxialTiltRadians = FMath::DegreesToRadians(Between(Seed, 7, 15.0, 28.0));
		Planet.RotationAtEpochRadians = Between(Seed, 8, 0.0, 2.0 * PI);
		Planet.ParentIndex = 0;
		Planet.Orbit.SemiMajorAxisMetres = AstronomicalUnit * Between(Seed, 9, 0.9, 1.1);
		Planet.Orbit.Eccentricity = Between(Seed, 10, 0.0, 0.06);
		Planet.Orbit.InclinationRadians = FMath::DegreesToRadians(Between(Seed, 11, -2.0, 2.0));
		Planet.Orbit.AscendingNodeRadians = Between(Seed, 12, 0.0, 2.0 * PI);
		Planet.Orbit.PeriapsisArgumentRadians = Between(Seed, 13, 0.0, 2.0 * PI);
		Planet.Orbit.MeanAnomalyAtEpochRadians = Between(Seed, 14, 0.0, 2.0 * PI);
		System.Bodies.Add(Planet);

		FLedgerBody Moon;
		Moon.Name = TEXT("Companion");
		Moon.Kind = ELedgerBodyKind::Moon;
		Moon.Seed = static_cast<uint32>(Mix(Seed ^ 0x9E3779B9u) & 0xFFFFFFFFull);
		Moon.MassKg = 7.342e22 * Between(Seed, 15, 0.5, 1.5);
		Moon.RadiusMetres = 1.7374e6 * Between(Seed, 16, 0.7, 1.3);
		// Tidally locked, which is what a close moon becomes: one rotation per
		// orbit, so the same face is always towards the planet.
		Moon.Orbit.SemiMajorAxisMetres = 3.844e8 * Between(Seed, 17, 0.7, 1.4);
		Moon.Orbit.Eccentricity = Between(Seed, 18, 0.0, 0.08);
		Moon.Orbit.InclinationRadians = FMath::DegreesToRadians(Between(Seed, 19, -6.0, 6.0));
		Moon.Orbit.AscendingNodeRadians = Between(Seed, 20, 0.0, 2.0 * PI);
		Moon.Orbit.PeriapsisArgumentRadians = Between(Seed, 21, 0.0, 2.0 * PI);
		Moon.Orbit.MeanAnomalyAtEpochRadians = Between(Seed, 22, 0.0, 2.0 * PI);
		{
			const double Mu = 6.67430e-11 * Planet.MassKg;
			const double Axis = Moon.Orbit.SemiMajorAxisMetres;
			Moon.RotationPeriodSeconds = 2.0 * PI * FMath::Sqrt((Axis * Axis * Axis) / Mu);
		}
		Moon.RotationAtEpochRadians = Between(Seed, 23, 0.0, 2.0 * PI);
		Moon.ParentIndex = 1;
		System.Bodies.Add(Moon);

		return System;
	}
}
