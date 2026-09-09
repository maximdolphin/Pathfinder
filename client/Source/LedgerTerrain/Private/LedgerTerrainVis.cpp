#include "LedgerTerrainVis.h"

#include "LedgerBiome.h"
#include "LedgerClimate.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerTerrainMath.h"
#include "Misc/CommandLine.h"

namespace LedgerTerrainVis
{
	ELedgerTerrainVis Mode()
	{
		static const ELedgerTerrainVis Parsed = []() -> ELedgerTerrainVis
		{
			FString Name;
			if (!FParse::Value(FCommandLine::Get(), TEXT("terrainvis="), Name))
			{
				return ELedgerTerrainVis::Off;
			}

			Name = Name.ToLower();
			if (Name == TEXT("lod"))       { return ELedgerTerrainVis::Lod; }
			if (Name == TEXT("patch"))     { return ELedgerTerrainVis::Patch; }
			if (Name == TEXT("collision")) { return ELedgerTerrainVis::Collision; }
			if (Name == TEXT("biome"))     { return ELedgerTerrainVis::Biome; }
			if (Name == TEXT("climate"))   { return ELedgerTerrainVis::Climate; }
			if (Name == TEXT("cache"))     { return ELedgerTerrainVis::Cache; }

			// Loudly, because the failure mode of a quiet one is somebody
			// staring at an ordinary render trying to read a diagnostic off it.
			UE_LOG(LogLedger, Error,
				TEXT("-terrainvis=%s is not a mode. One of: "
				     "lod, patch, collision, biome, climate, cache"), *Name);
			return ELedgerTerrainVis::Off;
		}();
		return Parsed;
	}
}

namespace
{
	/// Blue through green and yellow to red. A ramp somebody can read off a
	/// screenshot without a legend beside it.
	FColor Ramp(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		const double R = FMath::Clamp(T < 0.5 ? T * 2.0 : 1.0, 0.0, 1.0);
		const double G = FMath::Clamp(T < 0.5 ? T * 2.0 : (1.0 - T) * 2.0, 0.0, 1.0);
		const double B = FMath::Clamp(T < 0.5 ? 1.0 - T * 2.0 : 0.0, 0.0, 1.0);
		return FColor(
			static_cast<uint8>(R * 255.0),
			static_cast<uint8>(G * 255.0),
			static_cast<uint8>(B * 255.0), 255);
	}

	uint64 Scramble(uint64 Value)
	{
		Value ^= Value >> 33;
		Value *= 0xFF51AFD7ED558CCDull;
		Value ^= Value >> 33;
		return Value;
	}
}

namespace LedgerTerrainVis
{
	void Paint(FLedgerPatchJob& Job, bool bFromDisk)
	{
		const int32 Count = Job.Colors.Num();
		if (Count == 0)
		{
			return;
		}

		// Depth from extent: the root covers a whole face, and every level
		// halves it. Recovered rather than carried, because a job that knows
		// its own depth is one more field to keep in step.
		const int32 Depth = Job.Extent > 0.0
			? FMath::Clamp(FMath::RoundToInt(-FMath::Log2(Job.Extent)), 0, 24)
			: 0;

		FColor Flat = FColor::Black;
		switch (Mode())
		{
		case ELedgerTerrainVis::Lod:
			// Against 18, which is MaxDepth. A patch that is red is as fine as
			// this planet gets.
			Flat = Ramp(Depth / 18.0);
			break;

		case ELedgerTerrainVis::Patch:
		{
			// A hash per patch, so every boundary is a hard edge and a seam
			// between two levels is a seam you can point at.
			const uint64 Hash = Scramble(Job.Key ^ (static_cast<uint64>(Depth) << 56));
			Flat = FColor(
				static_cast<uint8>(80 + (Hash & 0x7F)),
				static_cast<uint8>(80 + ((Hash >> 8) & 0x7F)),
				static_cast<uint8>(80 + ((Hash >> 16) & 0x7F)), 255);
			break;
		}

		case ELedgerTerrainVis::Collision:
			// Green where a body could stand, red where it would fall through.
			Flat = Job.bWithCollision ? FColor(40, 200, 60, 255) : FColor(200, 50, 40, 255);
			break;

		case ELedgerTerrainVis::Cache:
			// Blue off disk, orange built this run. A flight that stays orange
			// is a flight whose cache key is changing under it.
			Flat = bFromDisk ? FColor(50, 110, 230, 255) : FColor(230, 130, 40, 255);
			break;

		default:
			break;
		}

		if (Mode() != ELedgerTerrainVis::Biome && Mode() != ELedgerTerrainVis::Climate)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				// Alpha carries snow and nothing reads it in these modes, but
				// leaving it at whatever the climate wrote makes a screenshot
				// depend on the weather.
				Job.Colors[Index] = Flat;
			}
			return;
		}

		// The two per-vertex modes have to sample, because what they show
		// varies within one patch.
		const int32 Side = FMath::Max(3, Job.Side | 1);
		TArray<double> Weights;
		const TArray<FLedgerBiome>* Biomes =
			Job.Biomes.IsValid() ? Job.Biomes.Get() : nullptr;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 X = Index % Side;
			const int32 Y = Index / Side;
			const double LocalU = static_cast<double>(X) / (Side - 1);
			const double LocalV = static_cast<double>(Y) / (Side - 1);
			const FVector3d Direction = LedgerTerrain::CubeToSphere(
				LedgerTerrain::FaceToCube(Job.Face,
					Job.U + LocalU * Job.Extent, Job.V + LocalV * Job.Extent));

			const FLedgerClimate Climate =
				LedgerClimate::At(Direction, Job.Params, Job.SeasonPhase);

			if (Mode() == ELedgerTerrainVis::Climate)
			{
				// Temperature red over -40..40 C, moisture blue. Green stays
				// empty on purpose: two fields, two channels, and nothing to
				// mistake for a third.
				const double Warmth =
					FMath::Clamp((Climate.SeaLevelTemperatureC + 40.0) / 80.0, 0.0, 1.0);
				Job.Colors[Index] = FColor(
					static_cast<uint8>(Warmth * 255.0), 0,
					static_cast<uint8>(FMath::Clamp(Climate.Moisture, 0.0, 1.0) * 255.0), 255);
				continue;
			}

			// Biome: the three strongest weights as red, green and blue, so a
			// boundary is a gradient between two channels and a three-way
			// meeting is grey.
			if (Biomes == nullptr || Biomes->Num() == 0)
			{
				Job.Colors[Index] = FColor(60, 60, 60, 255);
				continue;
			}
			LedgerBiomes::Weigh(*Biomes, Climate, 0.0, Weights);
			double First = 0.0;
			double Second = 0.0;
			double Third = 0.0;
			for (const double Weight : Weights)
			{
				if (Weight > First) { Third = Second; Second = First; First = Weight; }
				else if (Weight > Second) { Third = Second; Second = Weight; }
				else if (Weight > Third) { Third = Weight; }
			}
			Job.Colors[Index] = FColor(
				static_cast<uint8>(FMath::Clamp(First, 0.0, 1.0) * 255.0),
				static_cast<uint8>(FMath::Clamp(Second, 0.0, 1.0) * 255.0),
				static_cast<uint8>(FMath::Clamp(Third, 0.0, 1.0) * 255.0), 255);
		}
	}
}
