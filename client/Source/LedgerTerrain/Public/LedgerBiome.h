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

/// The biomes one patch is painted with.
///
/// **Three, because a patch's vertex colour has three channels.** The material
/// carries three parameterised surface slots and the mesh carries the weights,
/// so a patch is a choice of three from the whole set. The census says 33.9% of
/// land is in a transition and essentially none of it is a four-way one, which
/// is what makes three enough rather than a compromise.
///
/// Slots are biome indices into the loaded set, or INDEX_NONE for an unused
/// slot. The order is the order of the vertex colour channels.
struct FLedgerBiomePalette
{
	int32 Slots[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };

	/// A key that is equal exactly when the palettes are, so materials can be
	/// shared between patches instead of built per patch.
	uint32 Key() const
	{
		uint32 Value = 0;
		for (const int32 Slot : Slots)
		{
			Value = Value * 251u + static_cast<uint32>(Slot + 1);
		}
		return Value;
	}

	bool IsEmpty() const { return Slots[0] == INDEX_NONE; }
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

	/// The three biomes with the most weight in an accumulated total, heaviest
	/// first. Totals shorter than the biome set, or all zero, give an empty
	/// palette rather than a wrong one.
	LEDGERTERRAIN_API FLedgerBiomePalette ChoosePalette(const TArray<double>& Totals);

	/// The weights of a palette's three slots, renormalised over just those
	/// three, for writing into a vertex colour.
	///
	/// A point whose true biome is not in its patch's palette gets the nearest
	/// thing the patch has. That is a real approximation and it is bounded by
	/// how much of one patch a fourth biome can cover before it displaces one
	/// of the three -- which is the case the palette is chosen to avoid.
	LEDGERTERRAIN_API FVector3f SlotWeights(
		const TArray<double>& Weights, const FLedgerBiomePalette& Palette);

	/// T053: the ground channel each biome is drawn from, 0 to 6, or INDEX_NONE
	/// for the biome whose ground is snow, which goes to the snow overlay. The
	/// rule LedgerSurface::GroundChannels applies to the same files, in the
	/// same order, for the terrain material.
	constexpr int32 GroundChannelCount = 7;
	LEDGERTERRAIN_API TArray<int32> GroundChannels(const TArray<FLedgerBiome>& Biomes);

	/// Index of the heaviest biome, or INDEX_NONE for an empty set.
	LEDGERTERRAIN_API int32 Dominant(
		const TArray<FLedgerBiome>& Biomes,
		const FLedgerClimate& Climate,
		double SlopeDegrees);
}
