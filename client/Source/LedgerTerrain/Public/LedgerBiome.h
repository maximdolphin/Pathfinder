// Biomes as data. Architecture Rule 7, T052.
//
// A biome is a soft region of climate space with a surface set, a tint, a
// scatter density and a slope limit. The mechanism that reads it is here; the
// biomes themselves are JSON files in Config/Biomes, one per biome, and adding
// one is adding a file.
//
// **They are regions, not boxes.** A classifier that returns one biome per
// point draws a line on the ground wherever two of them meet, and T053's
// acceptance is explicitly that three biomes can meet on a slope with no
// visible blend band. So `Weigh` returns a weight per biome and the caller
// blends. The falloff is Gaussian in climate distance, which has no support
// boundary to see.
//
// **Config, not Content.** Loose non-asset files under Content/ are deleted by
// the cook -- that is why the pipeline cache never shipped
// (docs/comparisons/packaged/pso/README.md). The config directory stages
// intact.

#pragma once

#include "CoreMinimal.h"
#include "LedgerClimate.h"

/// One biome, as read from one file.
struct FLedgerBiome
{
	/// What it is called. Used in reports and error messages, not as a key.
	FString Name;

	/// Where it sits in climate space, and how far its influence reaches.
	///
	/// Tolerance is one standard deviation of the Gaussian, so a biome still
	/// contributes something two tolerances away. That overlap is the point:
	/// it is what makes a boundary a gradient instead of an edge.
	double TemperatureC = 15.0;
	double TemperatureToleranceC = 10.0;
	double Moisture = 0.5;
	double MoistureTolerance = 0.25;

	/// Ground steeper than this is not this biome, whatever the climate says:
	/// rainforest does not grow on a cliff face. Degrees from horizontal, and
	/// the cutoff is smoothed over the last ten degrees for the same reason the
	/// climate falloff is smooth.
	double MaxSlopeDegrees = 90.0;

	/// The surface set directory name under Content/Surfaces.
	FString SurfaceSet;

	/// Multiplied into the surface albedo. Climate drives the ground's colour
	/// response rather than a second painted map.
	FLinearColor Tint = FLinearColor::White;

	/// Relative scatter density, 0 for bare ground. Consumed by the scatter
	/// work; carried here because it is a property of the biome.
	double ScatterDensity = 0.0;
};

namespace LedgerBiomes
{
	/// Where the shipped biomes live, absolute.
	LEDGERTERRAIN_API FString DefaultDirectory();

	/// Parse one biome file's contents. False and a filled OutError on anything
	/// malformed -- a biome that silently defaults to temperate grassland
	/// because a field was misspelled is worse than one that refuses to load.
	LEDGERTERRAIN_API bool Parse(const FString& Json, FLedgerBiome& Out, FString& OutError);

	/// Every *.json in a directory, sorted by filename so the order is the same
	/// on every machine. Files that fail to parse are skipped and reported in
	/// OutErrors rather than aborting the set: one bad file should not leave the
	/// world with no biomes at all.
	LEDGERTERRAIN_API TArray<FLedgerBiome> Load(
		const FString& Directory, TArray<FString>& OutErrors);

	/// Weights over Biomes, in the same order, summing to one.
	///
	/// SlopeDegrees is the ground's angle from horizontal. If every biome is
	/// ruled out -- a climate no shipped biome covers, or a cliff -- the nearest
	/// one in climate space takes the whole weight, so a caller never has to
	/// handle an all-zero result.
	LEDGERTERRAIN_API void Weigh(
		const TArray<FLedgerBiome>& Biomes,
		const FLedgerClimate& Climate,
		double SlopeDegrees,
		TArray<double>& OutWeights);

	/// Index of the heaviest biome, or INDEX_NONE for an empty set.
	LEDGERTERRAIN_API int32 Dominant(
		const TArray<FLedgerBiome>& Biomes,
		const FLedgerClimate& Climate,
		double SlopeDegrees);
}
