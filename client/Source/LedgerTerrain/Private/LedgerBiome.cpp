#include "LedgerBiome.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/// Required number field. Absent or non-numeric is an error, because the
	/// alternative is a biome that quietly sits at the origin of climate space
	/// and wins everywhere.
	bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field,
		double& Out, FString& OutError)
	{
		if (!Object->TryGetNumberField(Field, Out))
		{
			OutError = FString::Printf(TEXT("'%s' is missing or not a number"), Field);
			return false;
		}
		return true;
	}

	/// Optional number field, left alone when absent.
	void OptionalNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& Out)
	{
		double Value = 0.0;
		if (Object->TryGetNumberField(Field, Value))
		{
			Out = Value;
		}
	}
}

namespace LedgerBiomes
{
	FString DefaultDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("Biomes"));
	}

	bool Parse(const FString& Json, FLedgerBiome& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("not a JSON object");
			return false;
		}

		FLedgerBiome Biome;
		if (!Root->TryGetStringField(TEXT("name"), Biome.Name) || Biome.Name.IsEmpty())
		{
			OutError = TEXT("'name' is missing or empty");
			return false;
		}

		// The four that place it in climate space are required. Everything else
		// has a defensible default; these do not.
		if (!Number(Root, TEXT("temperatureC"), Biome.TemperatureC, OutError)
			|| !Number(Root, TEXT("temperatureToleranceC"), Biome.TemperatureToleranceC, OutError)
			|| !Number(Root, TEXT("moisture"), Biome.Moisture, OutError)
			|| !Number(Root, TEXT("moistureTolerance"), Biome.MoistureTolerance, OutError))
		{
			return false;
		}

		if (Biome.TemperatureToleranceC <= 0.0 || Biome.MoistureTolerance <= 0.0)
		{
			OutError = TEXT("tolerances must be positive");
			return false;
		}

		OptionalNumber(Root, TEXT("maxSlopeDegrees"), Biome.MaxSlopeDegrees);
		OptionalNumber(Root, TEXT("scatterDensity"), Biome.ScatterDensity);
		Root->TryGetStringField(TEXT("surfaceSet"), Biome.SurfaceSet);

		const TArray<TSharedPtr<FJsonValue>>* Tint = nullptr;
		if (Root->TryGetArrayField(TEXT("tint"), Tint) && Tint->Num() >= 3)
		{
			Biome.Tint = FLinearColor(
				static_cast<float>((*Tint)[0]->AsNumber()),
				static_cast<float>((*Tint)[1]->AsNumber()),
				static_cast<float>((*Tint)[2]->AsNumber()));
		}

		Out = Biome;
		return true;
	}

	TArray<FLedgerBiome> Load(const FString& Directory, TArray<FString>& OutErrors)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.json")), true, false);
		Files.Sort();

		TArray<FLedgerBiome> Biomes;
		for (const FString& File : Files)
		{
			FString Contents;
			if (!FFileHelper::LoadFileToString(Contents, *(Directory / File)))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: unreadable"), *File));
				continue;
			}

			FLedgerBiome Biome;
			FString Error;
			if (!Parse(Contents, Biome, Error))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: %s"), *File, *Error));
				continue;
			}
			Biomes.Add(Biome);
		}
		return Biomes;
	}

	void Weigh(const TArray<FLedgerBiome>& Biomes, const FLedgerClimate& Climate,
		double SlopeDegrees, TArray<double>& OutWeights)
	{
		OutWeights.Reset(Biomes.Num());
		if (Biomes.Num() == 0)
		{
			return;
		}

		double Total = 0.0;
		double ClimateTotal = 0.0;
		double NearestDistance = TNumericLimits<double>::Max();
		int32 Nearest = 0;

		for (int32 Index = 0; Index < Biomes.Num(); ++Index)
		{
			const FLedgerBiome& Biome = Biomes[Index];
			const double Warmth = (Climate.TemperatureC - Biome.TemperatureC)
				/ Biome.TemperatureToleranceC;
			const double Wetness = (Climate.Moisture - Biome.Moisture) / Biome.MoistureTolerance;
			const double DistanceSquared = Warmth * Warmth + Wetness * Wetness;

			if (DistanceSquared < NearestDistance)
			{
				NearestDistance = DistanceSquared;
				Nearest = Index;
			}

			// Smoothed over the last ten degrees rather than cut, so a hillside
			// crossing a biome's slope limit fades out of it instead of ending.
			const double Slope = FMath::GetMappedRangeValueClamped(
				FVector2d(Biome.MaxSlopeDegrees - 10.0, Biome.MaxSlopeDegrees),
				FVector2d(1.0, 0.0), SlopeDegrees);

			const double Weight = FMath::Exp(-DistanceSquared) * Slope;
			OutWeights.Add(Weight);
			Total += Weight;
			ClimateTotal += FMath::Exp(-DistanceSquared);
		}

		// **Slope can refuse a biome; it cannot promote an implausible one.**
		//
		// Normalising after the slope cut hands the ground to whatever survived
		// it, however poor its climate match -- and the ice cap, the one biome
		// that allows ninety degrees, survived every cliff on the planet with a
		// weight of 0.005 at +10 C and was scaled up to one: snow on a 62-degree
		// face that the climate says carries none. A survivor now has to hold a
		// quarter of what the climate alone would give; below that the ground
		// falls to the biome its climate is nearest, and the material's slope
		// blend is what draws the face as rock. A polar cliff still goes to the
		// ice cap, because there its climate weight is most of the total.
		if (Total < 0.25 * ClimateTotal)
		{
			for (double& Weight : OutWeights)
			{
				Weight = 0.0;
			}
			OutWeights[Nearest] = 1.0;
			return;
		}

		// ---- truncate to three, continuously ------------------------------
		//
		// **The mesh can carry three weights, so the field has to be a
		// three-biome field.** Left as eight, a patch keeps its own three
		// heaviest and silently drops the rest -- and two neighbouring patches
		// that drop different ones disagree along their shared edge, which is a
		// straight line across the ground. Those seams were visible in the first
		// T053 captures, and a control run with one palette everywhere made them
		// vanish, which is how this stopped being a suspicion.
		//
		// Subtracting the fourth-largest weight and clamping leaves at most three
		// non-zero, and does it *continuously*: the fourth-largest moves smoothly,
		// so a biome entering or leaving the top three fades rather than appears.
		if (OutWeights.Num() > 3)
		{
			double First = 0.0;
			double Second = 0.0;
			double Third = 0.0;
			double Fourth = 0.0;
			for (const double Weight : OutWeights)
			{
				if (Weight > First) { Fourth = Third; Third = Second; Second = First; First = Weight; }
				else if (Weight > Second) { Fourth = Third; Third = Second; Second = Weight; }
				else if (Weight > Third) { Fourth = Third; Third = Weight; }
				else if (Weight > Fourth) { Fourth = Weight; }
			}

			Total = 0.0;
			for (double& Weight : OutWeights)
			{
				Weight = FMath::Max(0.0, Weight - Fourth);
				Total += Weight;
			}
		}

		if (Total <= UE_DOUBLE_SMALL_NUMBER)
		{
			// Nothing claims this point. Give it to the nearest in climate space
			// rather than returning zeroes: the caller wants ground, and ground
			// that is slightly wrong beats ground that is absent.
			for (double& Weight : OutWeights)
			{
				Weight = 0.0;
			}
			OutWeights[Nearest] = 1.0;
			return;
		}

		for (double& Weight : OutWeights)
		{
			Weight /= Total;
		}
	}

	int32 Dominant(const TArray<FLedgerBiome>& Biomes, const FLedgerClimate& Climate,
		double SlopeDegrees)
	{
		TArray<double> Weights;
		Weigh(Biomes, Climate, SlopeDegrees, Weights);

		int32 Best = INDEX_NONE;
		double BestWeight = -1.0;
		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			if (Weights[Index] > BestWeight)
			{
				BestWeight = Weights[Index];
				Best = Index;
			}
		}
		return Best;
	}
}

namespace LedgerBiomes
{
	FLedgerBiomePalette ChoosePalette(const TArray<double>& Totals)
	{
		FLedgerBiomePalette Palette;

		// Three passes of "the largest one not already taken". A sort would
		// allocate and this runs once per patch on a worker thread.
		for (int32 Slot = 0; Slot < 3; ++Slot)
		{
			int32 Best = INDEX_NONE;
			double BestWeight = 0.0;
			for (int32 Index = 0; Index < Totals.Num(); ++Index)
			{
				if (Index == Palette.Slots[0] || Index == Palette.Slots[1])
				{
					continue;
				}
				if (Totals[Index] > BestWeight)
				{
					BestWeight = Totals[Index];
					Best = Index;
				}
			}
			Palette.Slots[Slot] = Best;
		}
		return Palette;
	}

	FVector3f SlotWeights(const TArray<double>& Weights, const FLedgerBiomePalette& Palette)
	{
		double Slot[3] = { 0.0, 0.0, 0.0 };
		double Total = 0.0;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (Weights.IsValidIndex(Palette.Slots[Index]))
			{
				Slot[Index] = Weights[Palette.Slots[Index]];
				Total += Slot[Index];
			}
		}

		if (Total <= UE_DOUBLE_SMALL_NUMBER)
		{
			// Every one of this patch's three biomes has abandoned this vertex.
			// The first slot is the patch's own dominant biome, so giving it the
			// vertex is the least wrong answer available and it keeps the sum at
			// one, which the shader relies on.
			return FVector3f(1.0f, 0.0f, 0.0f);
		}

		return FVector3f(
			static_cast<float>(Slot[0] / Total),
			static_cast<float>(Slot[1] / Total),
			static_cast<float>(Slot[2] / Total));
	}
}
