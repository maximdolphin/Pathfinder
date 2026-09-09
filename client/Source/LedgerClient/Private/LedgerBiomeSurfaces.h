// The one place that knows both what a biome is and how to load its ground.

#pragma once

#include "CoreMinimal.h"

struct FLedgerBiome;
struct FLedgerBiomePalette;
class UMaterialInstanceDynamic;
class UMaterialInterface;

namespace LedgerBiomeSurfaces
{
	/// A dynamic instance of the terrain material with a palette's three
	/// surface sets bound into its ground slots. Null if the material is null
	/// or the instance cannot be created; a missing *set* is logged and leaves
	/// that slot at the material's default rather than failing the patch.
	UMaterialInstanceDynamic* CreatePaletteInstance(
		UObject* Outer,
		UMaterialInterface* Terrain,
		const FLedgerBiomePalette& Palette,
		const TArray<FLedgerBiome>& Biomes);
}
