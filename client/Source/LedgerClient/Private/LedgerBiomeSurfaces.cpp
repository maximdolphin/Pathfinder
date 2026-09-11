// Turning a patch's biomes into a material. T053.
//
// The composition root's job, and only its job. The terrain knows which three
// biomes a patch is made of and nothing about textures; the material module
// knows how to load a surface set and nothing about biomes. This is the one
// place allowed to know both, which is why it is here and not in either.

#include "LedgerBiomeSurfaces.h"

#include "UObject/StrongObjectPtr.h"

#include "LedgerBiome.h"
#include "LedgerLog.h"
#include "LedgerSurfaceSets.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/// Surface sets, loaded once each.
	///
	/// A palette is created the first time a patch needs it and there are a few
	/// dozen of them on a planet, but the sets behind them repeat: savanna and
	/// grassland share a scan, and every palette containing either loads it
	/// again without this.
	/// Everything the cache holds, kept alive.
	///
	/// **A static cache of raw UObject pointers is a static cache of dangling
	/// pointers waiting for a collection.** Nothing else referenced these
	/// textures except the materials made from them, so when a world was torn
	/// down and its materials went, the collector took the textures too and
	/// left this map pointing at freed memory. The next world's palettes were
	/// then built on them, and the render thread dereferenced null a few
	/// seconds later -- which is as far from the cause as a symptom usually
	/// gets. A strong pointer is what says "this map is a referencer".
	TArray<TStrongObjectPtr<UTexture2D>>& CacheRoots()
	{
		static TArray<TStrongObjectPtr<UTexture2D>> Roots;
		return Roots;
	}

	void RootSet(const LedgerSurface::FSurfaceSet& Set)
	{
		for (UTexture2D* Texture : { Set.Albedo, Set.Normal, Set.Packed })
		{
			if (Texture != nullptr)
			{
				CacheRoots().Add(TStrongObjectPtr<UTexture2D>(Texture));
			}
		}
	}

	LedgerSurface::FSurfaceSet& CachedSet(const FString& Name)
	{
		static TMap<FString, LedgerSurface::FSurfaceSet> Cache;
		if (LedgerSurface::FSurfaceSet* Existing = Cache.Find(Name))
		{
			// **And a check that says so out loud if it ever happens again.**
			// A cached texture that has stopped being valid is a bug somewhere
			// upstream, not a thing to paper over silently -- and it is
			// invisible from the crash it eventually causes.
			const bool bStale =
				(Existing->Albedo != nullptr && !IsValid(Existing->Albedo))
				|| (Existing->Normal != nullptr && !IsValid(Existing->Normal))
				|| (Existing->Packed != nullptr && !IsValid(Existing->Packed));
			if (!bStale)
			{
				return *Existing;
			}
			UE_LOG(LogLedger, Error,
				TEXT("surface set %s went stale in the cache: a texture was "
					 "collected while the cache still held it. Reloading."),
				*Name);
			Cache.Remove(Name);
		}
		LedgerSurface::FSurfaceSet& Added =
			Cache.Add(Name, LedgerSurface::LoadSurfaceSet(Name));
		RootSet(Added);
		return Added;
	}

	FString SlotParameter(int32 Slot, const TCHAR* Suffix)
	{
		return FString::Printf(TEXT("Ground%d%s"), Slot, Suffix);
	}
}

UMaterialInstanceDynamic* LedgerBiomeSurfaces::CreatePaletteInstance(
	UObject* Outer,
	UMaterialInterface* Terrain,
	const FLedgerBiomePalette& Palette,
	const TArray<FLedgerBiome>& Biomes)
{
	if (Terrain == nullptr)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Terrain, Outer);
	if (Instance == nullptr)
	{
		return nullptr;
	}

	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		const int32 Index = Palette.Slots[Slot];
		if (!Biomes.IsValidIndex(Index))
		{
			// An unused slot. Left at the material's defaults rather than
			// cleared: the mesh gives it zero weight, so what it holds never
			// reaches a pixel, and a null texture parameter would.
			continue;
		}

		const FLedgerBiome& Biome = Biomes[Index];
		const LedgerSurface::FSurfaceSet& Set = CachedSet(Biome.SurfaceSet);
		if (!Set.IsValid())
		{
			// Loud, once per set, and then the default ground. A biome whose
			// scan is missing should look wrong somewhere obvious rather than
			// take the whole patch out.
			UE_LOG(LogLedger, Error,
				TEXT("biome %s names surface set %s, which did not load; slot %d "
				     "keeps the material default"),
				*Biome.Name, *Biome.SurfaceSet, Slot);
			continue;
		}

		Instance->SetTextureParameterValue(
			FName(*SlotParameter(Slot, TEXT("Albedo"))), Set.Albedo);
		Instance->SetTextureParameterValue(
			FName(*SlotParameter(Slot, TEXT("Normal"))), Set.Normal);
		Instance->SetTextureParameterValue(
			FName(*SlotParameter(Slot, TEXT("Packed"))), Set.Packed);
		Instance->SetScalarParameterValue(
			FName(*SlotParameter(Slot, TEXT("Tiling"))),
			1.0f / static_cast<float>(Set.TilingMetres * 100.0));
		Instance->SetVectorParameterValue(
			FName(*SlotParameter(Slot, TEXT("Mean"))), Set.MeanAlbedo);
		Instance->SetVectorParameterValue(
			FName(*SlotParameter(Slot, TEXT("Tint"))), Biome.Tint);
	}

	// Logged once per palette, which is a few dozen times over a whole planet.
	// It is how "the ground is three grounds" stops being a claim: the palettes
	// a run actually created are in its log, by name.
	FString Names;
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		const int32 Index = Palette.Slots[Slot];
		Names += Slot > 0 ? TEXT(" + ") : TEXT("");
		Names += Biomes.IsValidIndex(Index) ? Biomes[Index].Name : TEXT("(none)");
	}
	UE_LOG(LogLedger, Log, TEXT("palette %u: %s"), Palette.Key(), *Names);

	return Instance;
}
