// Loading an authored surface set, by the name the manifest gives it.
//
// The textures are imported into /Game/Surfaces by Unreal's own ImportAssets
// commandlet, from a plan generated out of surfaces/manifest.json. This reads
// the same manifest, so the two cannot drift: the tiling distance the shader
// scales by, and the mean colour it divides out, are the numbers that were
// measured off the actual images at import time rather than constants somebody
// typed into a shader and then forgot the provenance of.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

class UTexture2D;

namespace LedgerSurface
{
	struct FSurfaceSet
	{
		FString Name;

		UTexture2D* Albedo = nullptr;
		UTexture2D* Normal = nullptr;
		UTexture2D* Packed = nullptr;

		/// How many metres of real ground one tile covers. Read from the scan's
		/// own metadata, not assumed: these sets are not all the same. Forest
		/// Floor Compost is half a metre, the rock cliffs are one, most of the
		/// ground scans are two, and a set tiled at the wrong distance reads as
		/// a repeating pattern rather than as ground.
		double TilingMetres = 2.0;

		/// The albedo's own average colour. The terrain tints by a per-vertex
		/// biome colour, so the scan's own colour has to come out first or the
		/// two multiply: green grass under a green tint is a swamp.
		FLinearColor MeanAlbedo = FLinearColor::White;

		bool IsValid() const
		{
			return Albedo != nullptr && Normal != nullptr && Packed != nullptr;
		}
	};

	/// Loads one set by manifest name. Returns an invalid set, and logs which
	/// part was missing, rather than half of one.
	FSurfaceSet LoadSurfaceSet(const FString& Name);
}

#endif
