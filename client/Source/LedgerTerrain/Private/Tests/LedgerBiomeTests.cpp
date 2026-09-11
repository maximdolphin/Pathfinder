// The biome framework, without a planet.
//
// T052's acceptance is "adding a biome is one data file and requires no
// recompile", which is a claim about behaviour and therefore testable rather
// than assertable: the last test here writes a biome file to a directory and
// watches it win the ground it claims.

#include "LedgerBiome.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FLedgerClimate Weather(double TemperatureC, double Moisture)
	{
		FLedgerClimate Climate;
		Climate.TemperatureC = TemperatureC;
		Climate.SeaLevelTemperatureC = TemperatureC;
		Climate.Moisture = Moisture;
		return Climate;
	}

	/// A biome in the middle of climate space, for tests that only care that
	/// something is there.
	FString MiddlingBiome(const FString& Name)
	{
		return FString::Printf(
			TEXT("{\"name\":\"%s\",\"temperatureC\":15,\"temperatureToleranceC\":8,")
			TEXT("\"moisture\":0.5,\"moistureTolerance\":0.2}"), *Name);
	}

	FString ScratchDirectory(const TCHAR* Leaf)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Tests") / Leaf);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerBiomeShippedSetLoads,
	"Ledger.Biome.ShippedSetLoadsWithoutError",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerBiomeShippedSetLoads::RunTest(const FString&)
{
	TArray<FString> Errors;
	const TArray<FLedgerBiome> Biomes = LedgerBiomes::Load(LedgerBiomes::DefaultDirectory(), Errors);

	for (const FString& Error : Errors)
	{
		AddError(FString::Printf(TEXT("shipped biome rejected: %s"), *Error));
	}

	// A framework that loads zero biomes passes every other test in this file.
	TestTrue(TEXT("the shipped set is not empty"), Biomes.Num() >= 8);

	for (const FLedgerBiome& Biome : Biomes)
	{
		TestFalse(FString::Printf(TEXT("%s names a surface set"), *Biome.Name),
			Biome.SurfaceSet.IsEmpty());
	}
	return Errors.Num() == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerBiomeWeightsArePartitions,
	"Ledger.Biome.WeightsSumToOneEverywhere",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerBiomeWeightsArePartitions::RunTest(const FString&)
{
	TArray<FString> Errors;
	const TArray<FLedgerBiome> Biomes = LedgerBiomes::Load(LedgerBiomes::DefaultDirectory(), Errors);
	if (Biomes.Num() == 0)
	{
		AddError(TEXT("no biomes to weigh"));
		return false;
	}

	// The whole climate space and every slope, including the corners no biome
	// sits near and the cliffs every biome refuses. The nearest-biome fallback
	// exists precisely so those return a partition rather than zeroes, and a
	// caller blending by weight would render nothing at all if it did not.
	TArray<double> Weights;
	for (int32 Temperature = -40; Temperature <= 45; Temperature += 5)
	{
		for (int32 Wetness = 0; Wetness <= 100; Wetness += 10)
		{
			for (int32 Slope = 0; Slope <= 90; Slope += 15)
			{
				LedgerBiomes::Weigh(Biomes,
					Weather(Temperature, Wetness / 100.0), Slope, Weights);

				double Sum = 0.0;
				for (const double Weight : Weights)
				{
					if (!FMath::IsFinite(Weight) || Weight < 0.0)
					{
						AddError(FString::Printf(
							TEXT("weight %f at %d C, %d%% moisture, %d degrees"),
							Weight, Temperature, Wetness, Slope));
						return false;
					}
					Sum += Weight;
				}

				if (!FMath::IsNearlyEqual(Sum, 1.0, 1e-9))
				{
					AddError(FString::Printf(
						TEXT("weights sum to %.12f at %d C, %d%% moisture, %d degrees"),
						Sum, Temperature, Wetness, Slope));
					return false;
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerBiomeCornersAreTheRightBiome,
	"Ledger.Biome.ClimateCornersPickTheObviousBiome",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerBiomeCornersAreTheRightBiome::RunTest(const FString&)
{
	TArray<FString> Errors;
	const TArray<FLedgerBiome> Biomes = LedgerBiomes::Load(LedgerBiomes::DefaultDirectory(), Errors);
	if (Biomes.Num() == 0)
	{
		AddError(TEXT("no biomes to choose between"));
		return false;
	}

	// Not a taste test. These are the corners of Whittaker's diagram, and
	// getting one wrong means the weighting is inverted in temperature or in
	// moisture -- the failure that looks plausible from altitude and absurd on
	// the ground.
	auto Winner = [&Biomes, this](double TemperatureC, double Moisture, const TCHAR* Expected)
	{
		const int32 Index = LedgerBiomes::Dominant(Biomes, Weather(TemperatureC, Moisture), 0.0);
		const FString Got = Biomes.IsValidIndex(Index) ? Biomes[Index].Name : TEXT("none");
		TestEqual(FString::Printf(TEXT("%.0f C at %.2f moisture"), TemperatureC, Moisture),
			Got, FString(Expected));
	};

	Winner(-30.0, 0.4, TEXT("Ice cap"));
	Winner(28.0, 0.90, TEXT("Tropical rainforest"));
	Winner(28.0, 0.05, TEXT("Desert"));
	Winner(12.0, 0.70, TEXT("Temperate forest"));
	Winner(12.0, 0.35, TEXT("Temperate grassland"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerBiomeSteepGroundYields,
	"Ledger.Biome.SteepGroundLeavesTheBiomesThatRefuseIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerBiomeSteepGroundYields::RunTest(const FString&)
{
	TArray<FString> Errors;
	const TArray<FLedgerBiome> Biomes = LedgerBiomes::Load(LedgerBiomes::DefaultDirectory(), Errors);

	int32 Rainforest = INDEX_NONE;
	int32 IceCap = INDEX_NONE;
	int32 Tundra = INDEX_NONE;
	for (int32 Index = 0; Index < Biomes.Num(); ++Index)
	{
		Rainforest = Biomes[Index].Name == TEXT("Tropical rainforest") ? Index : Rainforest;
		IceCap = Biomes[Index].Name == TEXT("Ice cap") ? Index : IceCap;
		Tundra = Biomes[Index].Name == TEXT("Tundra") ? Index : Tundra;
	}
	if (Rainforest == INDEX_NONE || IceCap == INDEX_NONE || Tundra == INDEX_NONE)
	{
		AddError(TEXT("the shipped set lacks tropical rainforest, ice cap or tundra"));
		return false;
	}

	// Flat jungle is jungle, and a jungle cliff is still jungle ground: every
	// biome that is plausible here refuses eighty degrees, so it falls to the
	// nearest in climate and the material's slope blend draws it as rock.
	// What it must not be is ice. The ice cap allows ninety degrees, and
	// normalising its trace of a weight up to one put snow on every cliff on
	// the planet -- which this test used to require.
	TArray<double> Flat;
	TArray<double> Cliff;
	const FLedgerClimate Jungle = Weather(27.0, 0.88);
	LedgerBiomes::Weigh(Biomes, Jungle, 0.0, Flat);
	LedgerBiomes::Weigh(Biomes, Jungle, 80.0, Cliff);

	TestTrue(TEXT("flat ground is rainforest"), Flat[Rainforest] > 0.5);
	TestTrue(TEXT("a jungle cliff is not ice"), Cliff[IceCap] < 0.01);
	TestTrue(TEXT("a jungle cliff falls back to the jungle"), Cliff[Rainforest] > 0.99);

	// Where a biome that allows the slope is plausible, steep ground does
	// leave the ones that refuse it: a polar cliff is ice, not tundra.
	TArray<double> Polar;
	LedgerBiomes::Weigh(Biomes, Weather(-20.0, 0.45), 80.0, Polar);
	TestTrue(TEXT("a polar cliff is ice"), Polar[IceCap] > 0.5);
	TestTrue(TEXT("tundra refuses it"), Polar[Tundra] < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerBiomeAddingOneIsOneFile,
	"Ledger.Biome.AddingABiomeIsOneFileAndNoRecompile",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerBiomeAddingOneIsOneFile::RunTest(const FString&)
{
	// The acceptance, run rather than asserted: a directory with two biomes,
	// then the same directory with a third file written into it by this test,
	// and the third one owns the ground it claims. Nothing between the two
	// loads but a file appearing.
	const FString Directory = ScratchDirectory(TEXT("Biomes"));
	IFileManager::Get().DeleteDirectory(*Directory, false, true);

	auto Write = [&Directory](const FString& File, const FString& Contents)
	{
		return FFileHelper::SaveStringToFile(Contents, *(Directory / File));
	};

	TestTrue(TEXT("wrote the first"), Write(TEXT("a.json"), MiddlingBiome(TEXT("Somewhere"))));
	TestTrue(TEXT("wrote the second"), Write(TEXT("b.json"),
		TEXT("{\"name\":\"Elsewhere\",\"temperatureC\":-10,\"temperatureToleranceC\":8,")
		TEXT("\"moisture\":0.2,\"moistureTolerance\":0.2}")));

	TArray<FString> Errors;
	TArray<FLedgerBiome> Biomes = LedgerBiomes::Load(Directory, Errors);
	TestEqual(TEXT("two biomes"), Biomes.Num(), 2);
	TestEqual(TEXT("hot and wet has nowhere better to go"),
		Biomes[LedgerBiomes::Dominant(Biomes, Weather(34.0, 0.95), 0.0)].Name,
		FString(TEXT("Somewhere")));

	TestTrue(TEXT("wrote the third"), Write(TEXT("c.json"),
		TEXT("{\"name\":\"Salt flat\",\"temperatureC\":34,\"temperatureToleranceC\":5,")
		TEXT("\"moisture\":0.95,\"moistureTolerance\":0.08,")
		TEXT("\"surfaceSet\":\"rocky_sand_vd4pbdt\"}")));

	Errors.Reset();
	Biomes = LedgerBiomes::Load(Directory, Errors);
	TestEqual(TEXT("three biomes"), Biomes.Num(), 3);
	TestEqual(TEXT("and the new one owns its corner"),
		Biomes[LedgerBiomes::Dominant(Biomes, Weather(34.0, 0.95), 0.0)].Name,
		FString(TEXT("Salt flat")));

	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerBiomeBadFilesAreNamed,
	"Ledger.Biome.MalformedFilesAreReportedAndSkipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerBiomeBadFilesAreNamed::RunTest(const FString&)
{
	FLedgerBiome Biome;
	FString Error;

	// Each of these would otherwise become a biome at the origin of climate
	// space, which is the middle, which is where it would win.
	TestFalse(TEXT("not JSON"), LedgerBiomes::Parse(TEXT("nope"), Biome, Error));
	TestFalse(TEXT("no name"), LedgerBiomes::Parse(
		TEXT("{\"temperatureC\":1,\"temperatureToleranceC\":1,")
		TEXT("\"moisture\":0.5,\"moistureTolerance\":0.1}"), Biome, Error));
	TestFalse(TEXT("no temperature"), LedgerBiomes::Parse(
		TEXT("{\"name\":\"X\",\"temperatureToleranceC\":1,")
		TEXT("\"moisture\":0.5,\"moistureTolerance\":0.1}"), Biome, Error));
	TestFalse(TEXT("a zero tolerance would divide by zero"), LedgerBiomes::Parse(
		TEXT("{\"name\":\"X\",\"temperatureC\":1,\"temperatureToleranceC\":0,")
		TEXT("\"moisture\":0.5,\"moistureTolerance\":0.1}"), Biome, Error));

	// And the message names the field, because a rejected file whose author
	// cannot see what is wrong with it is a rejected file that gets deleted.
	LedgerBiomes::Parse(TEXT("{\"name\":\"X\",\"temperatureToleranceC\":1,")
		TEXT("\"moisture\":0.5,\"moistureTolerance\":0.1}"), Biome, Error);
	TestTrue(TEXT("the error names the field"), Error.Contains(TEXT("temperatureC")));

	// A bad file does not take the good ones down with it.
	const FString Directory = ScratchDirectory(TEXT("BadBiomes"));
	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	FFileHelper::SaveStringToFile(MiddlingBiome(TEXT("Good")), *(Directory / TEXT("good.json")));
	FFileHelper::SaveStringToFile(FString(TEXT("{")), *(Directory / TEXT("broken.json")));

	TArray<FString> Errors;
	const TArray<FLedgerBiome> Biomes = LedgerBiomes::Load(Directory, Errors);
	TestEqual(TEXT("the good one loaded"), Biomes.Num(), 1);
	TestEqual(TEXT("the bad one was reported"), Errors.Num(), 1);
	TestTrue(TEXT("by filename"), Errors.Num() == 1 && Errors[0].Contains(TEXT("broken.json")));

	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
