#include "LedgerBody.h"

#include "LedgerLog.h"
#include "Misc/DefaultValueHelper.h"

namespace
{
	constexpr double AstronomicalUnit = 1.495978707e11;
	constexpr double SolarMass = 1.98847e30;
	constexpr double EarthMass = 5.97219e24;
	constexpr double EarthRadius = 6.371e6;

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

	double StarLuminosityRelative(const FLedgerBody& Star)
	{
		if (!(Star.MassKg > 0.0))
		{
			return 0.0;
		}
		const double Mass = Star.MassKg / SolarMass;
		const double Exponent = Mass < 0.43 ? 2.3 : (Mass < 2.0 ? 4.0 : 3.5);
		const double Scale = Mass < 0.43 ? 0.23 : 1.0;
		return Scale * FMath::Pow(Mass, Exponent);
	}

	int32 PrimaryIndex(const FLedgerSystem& System)
	{
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			if (System.Bodies[Index].ParentIndex == INDEX_NONE)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 HomeIndex(const FLedgerSystem& System)
	{
		return System.Bodies.IsValidIndex(1) ? 1 : INDEX_NONE;
	}

	int32 FirstOfKind(const FLedgerSystem& System, ELedgerBodyKind Kind)
	{
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			if (System.Bodies[Index].Kind == Kind)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 FirstChildOf(const FLedgerSystem& System, int32 ParentIndex)
	{
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			if (System.Bodies[Index].ParentIndex == ParentIndex)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 FirstChildOfKind(
		const FLedgerSystem& System, int32 ParentIndex, ELedgerBodyKind Kind)
	{
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			if (System.Bodies[Index].ParentIndex == ParentIndex
				&& System.Bodies[Index].Kind == Kind)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool Plausible(const FLedgerSystem& System, FString& OutWhy)
	{
		OutWhy.Reset();
		if (!Validate(System, OutWhy))
		{
			return false;
		}

		const int32 Star = PrimaryIndex(System);
		if (Star != 0 || System.Bodies[Star].Kind != ELedgerBodyKind::Star)
		{
			OutWhy = TEXT("the body that orbits nothing is not a star at index 0");
			return false;
		}

		const double Luminosity = StarLuminosityRelative(System.Bodies[Star]);
		const double FrostLine = 2.7 * AstronomicalUnit * FMath::Sqrt(Luminosity);

		// Planets, in order, checked against each other and against where they
		// could have formed.
		TArray<int32> Planets;
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			const FLedgerBody& Body = System.Bodies[Index];
			if (Body.Kind == ELedgerBodyKind::Planet
				|| Body.Kind == ELedgerBodyKind::GasGiant)
			{
				if (Body.ParentIndex != Star)
				{
					OutWhy = FString::Printf(
						TEXT("%s is a planet that does not orbit the star"), *Body.Name);
					return false;
				}
				Planets.Add(Index);
			}
		}

		// **Sorted, not required to be in order.** The first version of this
		// refused any system whose planets were not stored in order of
		// distance, which is a rule about the array and not about the system --
		// and it rejected sixteen of twenty generated systems for the crime of
		// listing the home world before an inner planet. Where a body sits in
		// the description is nobody's business but the description's.
		Planets.Sort([&System](const int32 A, const int32 B)
		{
			return System.Bodies[A].Orbit.SemiMajorAxisMetres
				< System.Bodies[B].Orbit.SemiMajorAxisMetres;
		});
		if (Planets.Num() < 2)
		{
			OutWhy = TEXT("a system with fewer than two planets is not much of one");
			return false;
		}

		for (int32 Which = 0; Which < Planets.Num(); ++Which)
		{
			const FLedgerBody& Body = System.Bodies[Planets[Which]];

			// **A gas giant inside the frost line could not have formed there.**
			// Water is vapour that close in, so there is not enough solid
			// material to build a core big enough to hold hydrogen.
			if (Body.Kind == ELedgerBodyKind::GasGiant
				&& Body.Orbit.SemiMajorAxisMetres < FrostLine)
			{
				OutWhy = FString::Printf(
					TEXT("%s is a gas giant at %.2f au, inside the frost line at %.2f"),
					*Body.Name, Body.Orbit.SemiMajorAxisMetres / AstronomicalUnit,
					FrostLine / AstronomicalUnit);
				return false;
			}

			const double Density = Body.MassKg / ((4.0 / 3.0) * LedgerPi
				* Body.RadiusMetres * Body.RadiusMetres * Body.RadiusMetres);
				// Saturn is 687 kg/m3 and Neptune 1638; a giant heavy enough to be
			// squeezing itself reaches a few thousand. Rock runs from a loose
			// pile at 2500 to iron-rich at 8000.
			const double Low = Body.Kind == ELedgerBodyKind::GasGiant ? 300.0 : 2500.0;
			const double High = Body.Kind == ELedgerBodyKind::GasGiant ? 3200.0 : 8000.0;
			if (Density < Low || Density > High)
			{
				OutWhy = FString::Printf(
					TEXT("%s has a density of %.0f kg/m3, outside %.0f to %.0f for its kind"),
					*Body.Name, Density, Low, High);
				return false;
			}

			if (Body.Orbit.Eccentricity > 0.35)
			{
				OutWhy = FString::Printf(TEXT("%s has an eccentricity of %.3f"),
					*Body.Name, Body.Orbit.Eccentricity);
				return false;
			}

			// **Far enough apart to survive.** Two planets closer than about ten
			// mutual Hill radii scatter each other within the age of a system,
			// so a generator that produces them is producing a system that no
			// longer exists.
			if (Which > 0)
			{
				const FLedgerBody& Inner = System.Bodies[Planets[Which - 1]];
				const double Mean = (Inner.Orbit.SemiMajorAxisMetres
					+ Body.Orbit.SemiMajorAxisMetres) * 0.5;
				const double Hill = Mean * FMath::Pow(
					(Inner.MassKg + Body.MassKg) / (3.0 * System.Bodies[Star].MassKg),
					1.0 / 3.0);
				const double Gap =
					Body.Orbit.SemiMajorAxisMetres - Inner.Orbit.SemiMajorAxisMetres;
				if (Gap < Hill * 10.0)
				{
					OutWhy = FString::Printf(
						TEXT("%s and %s are %.1f mutual Hill radii apart, and ten is "
							 "where a pair stops scattering each other"),
						*Inner.Name, *Body.Name, Gap / Hill);
					return false;
				}
			}
		}

		// Moons: outside the Roche limit, inside the Hill sphere.
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			const FLedgerBody& Moon = System.Bodies[Index];
			if (Moon.Kind != ELedgerBodyKind::Moon)
			{
				continue;
			}
			if (!System.Bodies.IsValidIndex(Moon.ParentIndex)
				|| System.Bodies[Moon.ParentIndex].Kind == ELedgerBodyKind::Star)
			{
				OutWhy = FString::Printf(TEXT("%s is a moon of nothing"), *Moon.Name);
				return false;
			}
			const FLedgerBody& Parent = System.Bodies[Moon.ParentIndex];

			const double ParentDensity = Parent.MassKg / ((4.0 / 3.0) * LedgerPi
				* Parent.RadiusMetres * Parent.RadiusMetres * Parent.RadiusMetres);
			const double MoonDensity = Moon.MassKg / ((4.0 / 3.0) * LedgerPi
				* Moon.RadiusMetres * Moon.RadiusMetres * Moon.RadiusMetres);
			const double Roche = 2.44 * Parent.RadiusMetres
				* FMath::Pow(ParentDensity / MoonDensity, 1.0 / 3.0);
			if (Moon.Orbit.SemiMajorAxisMetres < Roche)
			{
				OutWhy = FString::Printf(
					TEXT("%s orbits %s inside its Roche limit and would be a ring"),
					*Moon.Name, *Parent.Name);
				return false;
			}

			const double Hill = Parent.Orbit.SemiMajorAxisMetres
				* (1.0 - Parent.Orbit.Eccentricity)
				* FMath::Pow(Parent.MassKg / (3.0 * System.Bodies[Star].MassKg), 1.0 / 3.0);
			if (Moon.Orbit.SemiMajorAxisMetres > Hill * 0.5)
			{
				OutWhy = FString::Printf(
					TEXT("%s orbits %s beyond half its Hill sphere and the star would "
						 "take it"), *Moon.Name, *Parent.Name);
				return false;
			}
		}

		// **And somewhere to stand.** The home world has to be at a distance
		// where its equilibrium temperature allows liquid water, or the system
		// is plausible and useless.
		const int32 Home = HomeIndex(System);
		if (!System.Bodies.IsValidIndex(Home)
			|| System.Bodies[Home].Kind != ELedgerBodyKind::Planet)
		{
			OutWhy = TEXT("body 1 is not a rocky planet");
			return false;
		}
		const double HomeAu =
			System.Bodies[Home].Orbit.SemiMajorAxisMetres / AstronomicalUnit;
		const double Inner = 0.95 * FMath::Sqrt(Luminosity);
		const double Outer = 1.5 * FMath::Sqrt(Luminosity);
		if (HomeAu < Inner || HomeAu > Outer)
		{
			OutWhy = FString::Printf(
				TEXT("the home world is at %.3f au, outside the habitable zone of "
					 "%.3f to %.3f for a star of %.3f solar luminosities"),
				HomeAu, Inner, Outer, Luminosity);
			return false;
		}
		return true;
	}

	FLedgerSystem Generate(uint32 Seed)
	{
		FLedgerSystem System;
		System.Seed = Seed;
		System.EpochSeconds = 0.0;

		// ---- the star, which decides everything else ----------------------
		//
		// Mass first, because on the main sequence it settles the luminosity,
		// and the luminosity settles where the frost line and the habitable
		// zone are -- which is to say where planets can form and where one can
		// be lived on.
		FLedgerBody Star;
		Star.Name = TEXT("Primary");
		Star.Kind = ELedgerBodyKind::Star;
		Star.Seed = Seed;
		Star.MassKg = SolarMass * Between(Seed, 1, 0.62, 1.45);
		Star.RadiusMetres = 6.957e8
			* FMath::Pow(Star.MassKg / SolarMass, 0.85) * Between(Seed, 2, 0.95, 1.05);
		Star.RotationPeriodSeconds = 86400.0 * Between(Seed, 3, 18.0, 34.0);
		Star.ParentIndex = INDEX_NONE;
		System.Bodies.Add(Star);

		const double Luminosity = StarLuminosityRelative(Star);
		const double RootL = FMath::Sqrt(Luminosity);
		const double FrostLine = 2.7 * AstronomicalUnit * RootL;

		// ---- the home world, placed where it can be lived on ---------------
		FLedgerBody Home;
		Home.Name = TEXT("Home");
		Home.Kind = ELedgerBodyKind::Planet;
		Home.Seed = static_cast<uint32>(Mix(Seed) & 0xFFFFFFFFull);
		Home.MassKg = EarthMass * Between(Seed, 4, 0.7, 1.5);
		Home.RadiusMetres = EarthRadius * FMath::Pow(Home.MassKg / EarthMass, 0.27);
		Home.RotationPeriodSeconds = Between(Seed, 6, 16.0, 34.0) * 3600.0;
		Home.AxialTiltRadians = FMath::DegreesToRadians(Between(Seed, 7, 5.0, 35.0));
		Home.RotationAtEpochRadians = Between(Seed, 8, 0.0, LedgerTwoPi);
		Home.ParentIndex = 0;
		Home.Orbit.SemiMajorAxisMetres =
			AstronomicalUnit * RootL * Between(Seed, 9, 1.0, 1.42);
		Home.Orbit.Eccentricity = Between(Seed, 10, 0.0, 0.06);
		Home.Orbit.InclinationRadians = FMath::DegreesToRadians(Between(Seed, 11, -2.0, 2.0));
		Home.Orbit.AscendingNodeRadians = Between(Seed, 12, 0.0, LedgerTwoPi);
		Home.Orbit.PeriapsisArgumentRadians = Between(Seed, 13, 0.0, LedgerTwoPi);
		Home.Orbit.MeanAnomalyAtEpochRadians = Between(Seed, 14, 0.0, LedgerTwoPi);
		System.Bodies.Add(Home);

		// Its moon. Always at least one, because the whole of T072 to T075 is
		// about watching one -- and because a world without one has no tides
		// and no eclipses and is a duller place to have built.
		{
			FLedgerBody Moon;
			Moon.Name = TEXT("Companion");
			Moon.Kind = ELedgerBodyKind::Moon;
			Moon.Seed = static_cast<uint32>(Mix(Seed ^ 0x9E3779B9u) & 0xFFFFFFFFull);
			Moon.MassKg = 7.342e22 * Between(Seed, 15, 0.4, 1.8);
			Moon.RadiusMetres = 1.7374e6
				* FMath::Pow(Moon.MassKg / 7.342e22, 1.0 / 3.0) * Between(Seed, 16, 0.95, 1.15);
			// Clamped to a third of the home world's Hill sphere. A light
			// planet close to a heavy star has a small one, and seed 20363855
			// produced exactly that: a companion the star would have taken.
			const double HomeHill = Home.Orbit.SemiMajorAxisMetres
				* (1.0 - Home.Orbit.Eccentricity)
				* FMath::Pow(Home.MassKg / (3.0 * Star.MassKg), 1.0 / 3.0);
			Moon.Orbit.SemiMajorAxisMetres = FMath::Min(
				3.844e8 * Between(Seed, 17, 0.55, 1.35), HomeHill * 0.3);
			Moon.Orbit.Eccentricity = Between(Seed, 18, 0.0, 0.07);
			Moon.Orbit.InclinationRadians =
				FMath::DegreesToRadians(Between(Seed, 19, -8.0, 8.0));
			Moon.Orbit.AscendingNodeRadians = Between(Seed, 20, 0.0, LedgerTwoPi);
			Moon.Orbit.PeriapsisArgumentRadians = Between(Seed, 21, 0.0, LedgerTwoPi);
			Moon.Orbit.MeanAnomalyAtEpochRadians = Between(Seed, 22, 0.0, LedgerTwoPi);
			Moon.RotationPeriodSeconds = LedgerTwoPi * FMath::Sqrt(
				FMath::Pow(Moon.Orbit.SemiMajorAxisMetres, 3.0)
				/ (6.67430e-11 * Home.MassKg));
			Moon.RotationAtEpochRadians = Between(Seed, 23, 0.0, LedgerTwoPi);
			Moon.ParentIndex = 1;
			System.Bodies.Add(Moon);
		}

		// ---- the rest of the planets ---------------------------------------
		//
		// Placed by walking outwards in a geometric progression, which is what
		// real systems approximate: each planet clears a zone around itself, so
		// the next one has to start beyond it and the gaps grow with radius.
		// The ratio is jittered per step, so no two systems share a layout even
		// where they share a planet count.
		const int32 Extra = 2 + static_cast<int32>(Between(Seed, 24, 0.0, 4.99));
		double Axis = Home.Orbit.SemiMajorAxisMetres;
		int32 Named = 0;

		// One inner planet, sometimes, closer than home. Hot rocks close in.
		if (Between(Seed, 25, 0.0, 1.0) > 0.35)
		{
			FLedgerBody Rock;
			Rock.Name = FString::Printf(TEXT("Inner %d"), ++Named);
			Rock.Kind = ELedgerBodyKind::Planet;
			Rock.Seed = static_cast<uint32>(Mix(Seed ^ 0x1234567u) & 0xFFFFFFFFull);
			Rock.MassKg = EarthMass * Between(Seed, 26, 0.05, 0.9);
			Rock.RadiusMetres = EarthRadius * FMath::Pow(Rock.MassKg / EarthMass, 0.27);
			Rock.RotationPeriodSeconds = Between(Seed, 27, 100.0, 3000.0) * 3600.0;
			Rock.AxialTiltRadians = FMath::DegreesToRadians(Between(Seed, 28, 0.0, 12.0));
			Rock.ParentIndex = 0;
			Rock.Orbit.SemiMajorAxisMetres =
				Home.Orbit.SemiMajorAxisMetres * Between(Seed, 29, 0.28, 0.62);
			Rock.Orbit.Eccentricity = Between(Seed, 30, 0.0, 0.18);
			Rock.Orbit.InclinationRadians =
				FMath::DegreesToRadians(Between(Seed, 31, -6.0, 6.0));
			Rock.Orbit.AscendingNodeRadians = Between(Seed, 32, 0.0, LedgerTwoPi);
			Rock.Orbit.PeriapsisArgumentRadians = Between(Seed, 33, 0.0, LedgerTwoPi);
			Rock.Orbit.MeanAnomalyAtEpochRadians = Between(Seed, 34, 0.0, LedgerTwoPi);
			System.Bodies.Insert(Rock, 1);
			// Home moved to index 2 and its moon to 3, so put them back: the
			// contract is that body 1 is the world the game is about.
			System.Bodies.Swap(1, 2);
		}

		double PreviousAxis = Home.Orbit.SemiMajorAxisMetres;
		double PreviousMass = Home.MassKg;

		for (int32 Which = 0; Which < Extra; ++Which)
		{
			const int32 Stream = 40 + Which * 8;
			Axis *= Between(Seed, Stream, 1.55, 2.35);

			bool bGiant = Axis > FrostLine;

			// **Placed where it can stay, not merely where the progression put
			// it.** A geometric walk outwards produces pairs seven or eight
			// mutual Hill radii apart, and a pair that close scatters itself
			// within the age of a system -- the plausibility check caught four
			// of twenty like that. So the mass is chosen first and the orbit is
			// then pushed out until there is room for it.
			const double TentativeMass = bGiant
				? 1.898e27 * Between(Seed, Stream + 1, 0.12, 1.9)
				: EarthMass * Between(Seed, Stream + 1, 0.08, 3.0);

			for (int32 Attempt = 0; Attempt < 40; ++Attempt)
			{
				const double Mean = (PreviousAxis + Axis) * 0.5;
				const double Hill = Mean * FMath::Pow(
					(PreviousMass + TentativeMass) / (3.0 * Star.MassKg), 1.0 / 3.0);
				if (Axis - PreviousAxis >= Hill * 12.0)
				{
					break;
				}
				Axis += Hill * 4.0;
			}
			bGiant = Axis > FrostLine;

			FLedgerBody Body;
			Body.Kind = bGiant ? ELedgerBodyKind::GasGiant : ELedgerBodyKind::Planet;
			Body.Name = bGiant
				? FString::Printf(TEXT("Giant %d"), Which + 1)
				: FString::Printf(TEXT("Outer %d"), Which + 1);
			Body.Seed = static_cast<uint32>(Mix(Seed ^ (0xA5A5u * (Which + 3))) & 0xFFFFFFFFull);
			Body.ParentIndex = 0;

			Body.MassKg = TentativeMass;
			if (bGiant)
			{
				// **Fitted to the four real ones, because the obvious model has
				// it backwards.** Radius-goes-as-mass-to-the-0.08 says a small
				// giant is nearly as wide as a large one, and therefore far less
				// dense -- a 0.12-Jupiter body came out at 265 kg/m3, lighter
				// than balsa. Real small giants are DENSER: Neptune is a
				// sixteenth of Jupiter's mass, a third of its radius, and half
				// again its density.
				//
				// Two segments through Neptune, Saturn and Jupiter: 0.52 below
				// a third of a Jupiter mass and 0.14 above, where degeneracy
				// starts holding the radius flat however much is added.
				const double Jupiters = Body.MassKg / 1.898e27;
				const double Fraction = Jupiters < 0.299
					? 0.346 * FMath::Pow(Jupiters / 0.054, 0.52)
					: 0.843 * FMath::Pow(Jupiters / 0.299, 0.14);
				Body.RadiusMetres = 6.9911e7 * Fraction
					* Between(Seed, Stream + 2, 0.96, 1.04);
				Body.RotationPeriodSeconds = Between(Seed, Stream + 3, 8.0, 19.0) * 3600.0;
			}
			else
			{
				Body.RadiusMetres = EarthRadius * FMath::Pow(Body.MassKg / EarthMass, 0.27);
				Body.RotationPeriodSeconds = Between(Seed, Stream + 3, 12.0, 60.0) * 3600.0;
			}

			Body.AxialTiltRadians = FMath::DegreesToRadians(Between(Seed, Stream + 4, 0.0, 40.0));
			Body.RotationAtEpochRadians = Between(Seed, Stream + 5, 0.0, LedgerTwoPi);
			Body.Orbit.SemiMajorAxisMetres = Axis;
			Body.Orbit.Eccentricity = Between(Seed, Stream + 6, 0.0, 0.11);
			Body.Orbit.InclinationRadians =
				FMath::DegreesToRadians(Between(Seed, Stream + 7, -3.5, 3.5));
			Body.Orbit.AscendingNodeRadians = Between(Seed, Stream + 2, 0.0, LedgerTwoPi);
			Body.Orbit.PeriapsisArgumentRadians = Between(Seed, Stream + 5, 0.0, LedgerTwoPi);
			Body.Orbit.MeanAnomalyAtEpochRadians = Between(Seed, Stream + 4, 0.0, LedgerTwoPi);
			System.Bodies.Add(Body);
			PreviousAxis = Axis;
			PreviousMass = Body.MassKg;

			// Giants keep moons. Small ones, well inside the Hill sphere.
			//
			// **The giant's index is taken before the loop.** It was computed
			// inside it as `Num() - 1`, which is the giant only until the first
			// moon is added -- after that it is the previous MOON, so moon two
			// orbited moon one and the plausibility check reported eighteen
			// systems whose moons had moons.
			const int32 GiantIndex = System.Bodies.Num() - 1;
			const double GiantHill = Axis * FMath::Pow(
				Body.MassKg / (3.0 * Star.MassKg), 1.0 / 3.0);

			if (bGiant)
			{
				const int32 Moons = 1 + static_cast<int32>(Between(Seed, Stream + 6, 0.0, 2.99));
				for (int32 Each = 0; Each < Moons; ++Each)
				{
					const int32 Sub = Stream + 200 + Each * 5;
					FLedgerBody Moon;
					Moon.Name = FString::Printf(TEXT("%s moon %d"), *Body.Name, Each + 1);
					Moon.Kind = ELedgerBodyKind::Moon;
					Moon.Seed = static_cast<uint32>(Mix(Seed ^ (0x5BD1u * (Sub + 1))) & 0xFFFFFFFFull);
					Moon.MassKg = 7.342e22 * Between(Seed, Sub, 0.02, 1.9);
					Moon.RadiusMetres = 1.7374e6
						* FMath::Pow(Moon.MassKg / 7.342e22, 1.0 / 3.0);
					Moon.ParentIndex = GiantIndex;
					// Inside a third of the Hill sphere, which is where a moon
					// is stable for the age of a system rather than merely
					// bound today.
					Moon.Orbit.SemiMajorAxisMetres = FMath::Min(
						Body.RadiusMetres * Between(Seed, Sub + 1, 4.0, 26.0),
						GiantHill * 0.3);
					Moon.Orbit.Eccentricity = Between(Seed, Sub + 2, 0.0, 0.05);
					Moon.Orbit.InclinationRadians =
						FMath::DegreesToRadians(Between(Seed, Sub + 3, -4.0, 4.0));
					Moon.Orbit.MeanAnomalyAtEpochRadians = Between(Seed, Sub + 4, 0.0, LedgerTwoPi);
					Moon.RotationPeriodSeconds = LedgerTwoPi * FMath::Sqrt(
						FMath::Pow(Moon.Orbit.SemiMajorAxisMetres, 3.0)
						/ (6.67430e-11 * Body.MassKg));
					System.Bodies.Add(Moon);
				}
			}
		}

		// ---- and somewhere to dock ----------------------------------------
		{
			FLedgerBody Station;
			Station.Name = TEXT("Platform");
			Station.Kind = ELedgerBodyKind::Station;
			Station.Seed = static_cast<uint32>(Mix(Seed ^ 0xC2B2AE35u) & 0xFFFFFFFFull);
			Station.MassKg = 1.0e5;
			Station.RadiusMetres = 80.0;
			Station.ParentIndex = 1;
			Station.Orbit.SemiMajorAxisMetres = System.Bodies[1].RadiusMetres
				+ 400000.0 * Between(Seed, 35, 0.8, 1.5);
			Station.Orbit.Eccentricity = Between(Seed, 36, 0.0, 0.002);
			Station.Orbit.InclinationRadians =
				FMath::DegreesToRadians(Between(Seed, 37, 20.0, 60.0));
			Station.Orbit.AscendingNodeRadians = Between(Seed, 38, 0.0, LedgerTwoPi);
			Station.Orbit.PeriapsisArgumentRadians = Between(Seed, 39, 0.0, LedgerTwoPi);
			Station.Orbit.MeanAnomalyAtEpochRadians = Between(Seed, 40, 0.0, LedgerTwoPi);
			Station.RotationPeriodSeconds = LedgerTwoPi * FMath::Sqrt(
				FMath::Pow(Station.Orbit.SemiMajorAxisMetres, 3.0)
				/ (6.67430e-11 * System.Bodies[1].MassKg));
			System.Bodies.Add(Station);
		}

		return System;
	}
}
