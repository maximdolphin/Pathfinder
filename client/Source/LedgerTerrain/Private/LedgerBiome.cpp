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
