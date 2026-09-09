// What the material factory does in a packaged build, now that most of it is
// an asset.
//
// Terrain, water and underwater are baked by `BakeMaterials` and loaded by
// `LedgerMaterialLoad.cpp`, in editor and packaged builds alike -- so they are
// no longer here. What is left is the one material that is not baked.
//
// `CreateFlatMaterial` takes a colour and a roughness, and the settlement asks
// for a different pair per building. Baking it would mean a material instance
// per colour, which is the instancing task in this milestone and not this one.
// So in a packaged build it is still absent, and that is now a small and
// specific hole rather than the whole look of the game.

#include "LedgerSurface.h"

#if !WITH_EDITOR

#include "LedgerLog.h"

namespace LedgerSurface
{
	UMaterialInterface* CreateFlatMaterial(UObject*, const FLinearColor&, float)
	{
		static bool bSaid = false;
		if (!bSaid)
		{
			bSaid = true;
			UE_LOG(LogLedger, Warning,
				TEXT("flat materials are not baked yet, so buildings and the ship hull "
				     "have no material in a packaged build. M2W: instanced rendering."));
		}
		return nullptr;
	}
}

#endif
