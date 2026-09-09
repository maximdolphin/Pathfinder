// How the game gets a material: the baked asset, or a loud fallback.
//
// ADR-0006. The graph builders in this module are the source of truth for what
// a material *is* -- they are code, so they diff and review and merge.
// `BakeMaterials` runs them once and saves the result. This is the other half:
// everything in the game asks for a material here, and gets the saved asset.
//
// **Why the game does not just build one.** Because a packaged build cannot.
// Shaders compile at runtime only in an editor build, which is why the cooked
// path used to return null for every material and a build with the editor
// stripped had no look at all.
//
// **Why the editor falls back to building.** A fresh clone has no baked assets
// -- they are derived, and derived things are not in version control. Falling
// back keeps that clone working, and the log says loudly that it happened so
// nobody concludes the bake ran when it did not.

#include "LedgerSurface.h"

#include "LedgerLog.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if WITH_EDITOR
#include "LedgerMaterialGraph.h"
#endif

namespace LedgerSurface
{
	namespace
	{
		UMaterialInterface* LoadBaked(const TCHAR* Name)
		{
			// `-livematerials` skips the asset and builds from the graph. For
			// working on a material: the asset is what the game loads, so
			// without this every edit would need a re-bake before it could be
			// seen. Not the default, because then the thing being looked at
			// would not be the thing that ships.
			if (FParse::Param(FCommandLine::Get(), TEXT("livematerials")))
			{
				return nullptr;
			}

			const FString Path = FString::Printf(TEXT("%s%s.%s"), MaterialPackageRoot, Name, Name);
			return LoadObject<UMaterialInterface>(nullptr, *Path);
		}

		UMaterialInterface* Fallback(const TCHAR* Name)
		{
			UE_LOG(LogLedger, Error,
				TEXT("no baked material at %s%s and none can be built without the editor. "
				     "Run the bake: UnrealEditor.exe <project> -game -bakematerials"),
				MaterialPackageRoot, Name);
			return nullptr;
		}
	}

	UMaterialInterface* CreateTerrainMaterial(UObject* Outer, uint32 Seed)
	{
		if (UMaterialInterface* Baked = LoadBaked(TEXT("M_Terrain")))
		{
			return Baked;
		}
#if WITH_EDITOR
		UE_LOG(LogLedger, Warning,
			TEXT("terrain material built in memory: no baked asset. It will not exist "
			     "in a packaged build until the bake is run."));
		return BuildTerrainMaterial(Outer, Seed);
#else
		return Fallback(TEXT("M_Terrain"));
#endif
	}

	UMaterialInterface* CreateWaterMaterial(UObject* Outer)
	{
		if (UMaterialInterface* Baked = LoadBaked(TEXT("M_Water")))
		{
			return Baked;
		}
#if WITH_EDITOR
		UE_LOG(LogLedger, Warning, TEXT("water material built in memory: no baked asset."));
		return BuildWaterMaterial(Outer);
#else
		return Fallback(TEXT("M_Water"));
#endif
	}

	UMaterialInterface* CreateUnderwaterMaterial(UObject* Outer)
	{
		if (UMaterialInterface* Baked = LoadBaked(TEXT("M_Underwater")))
		{
			return Baked;
		}
#if WITH_EDITOR
		UE_LOG(LogLedger, Warning, TEXT("underwater material built in memory: no baked asset."));
		return BuildUnderwaterMaterial(Outer);
#else
		return Fallback(TEXT("M_Underwater"));
#endif
	}
}
