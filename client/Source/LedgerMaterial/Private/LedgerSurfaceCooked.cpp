// What the material factory does in a packaged build: nothing.
//
// Shaders only compile at runtime in an editor build, so every one of these
// returns null and the caller falls back. Saving them as assets is M12 work
// (the cook and package pipeline); until then a packaged build has no
// terrain material, and that is a known hole rather than a surprise.

#include "LedgerSurface.h"

#if !WITH_EDITOR

namespace LedgerSurface
{
	UMaterialInterface* CreateTerrainMaterial(UObject*, uint32)
	{
		// A cooked build cannot compile a shader at runtime; this needs a saved
		// asset before there is a packaged game.
		return nullptr;
	}

	UMaterialInterface* CreateFlatMaterial(UObject*, const FLinearColor&, float)
	{
		return nullptr;
	}

	UMaterialInterface* CreateWaterMaterial(UObject*)
	{
		return nullptr;
	}

	UMaterialInterface* CreateUnderwaterMaterial(UObject*)
	{
		return nullptr;
	}
}

#endif
