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
#include "Materials/MaterialInstanceDynamic.h"
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

	UMaterialInterface* CreateStarMaterial(UObject* Outer)
	{
		if (UMaterialInterface* Baked = LoadBaked(TEXT("M_Star")))
		{
			return Baked;
		}
#if WITH_EDITOR
		UE_LOG(LogLedger, Warning,
			TEXT("star material built in memory: no baked asset. It will not "
			     "exist in a packaged build until the bake is run."));
		return BuildStarMaterial(Outer);
#else
		return Fallback(TEXT("M_Star"));
#endif
	}

	UMaterialInterface* CreateCloudMaterial(UObject* Outer)
	{
		if (UMaterialInterface* Baked = LoadBaked(TEXT("M_Clouds")))
		{
			return Baked;
		}
#if WITH_EDITOR
		UE_LOG(LogLedger, Warning,
			TEXT("cloud material built in memory: no baked asset. It will not "
			     "exist in a packaged build until the bake is run."));
		return BuildCloudMaterial(Outer);
#else
		// **No fallback, on purpose.** A surface without its material renders
		// the wrong colour; a volume without its material renders as an opaque
		// grey slab across the whole sky, which is worse than no cloud at all.
		return nullptr;
#endif
	}

	UMaterialInterface* CreateFlatMaterial(UObject* Outer, const FLinearColor& Colour,
		float Roughness)
	{
		// A dynamic instance of one baked material, not a material per colour.
		//
		// This is what makes buildings, trees and the ship hull exist in a
		// packaged build. They had no material at all: CreateFlatMaterial built
		// a fresh UMaterial with the colour as a constant, which cannot be
		// baked, so the cooked path returned null and the first packaged build
		// came up with the town and the forest rendered as black cut-outs on
		// correctly textured ground.
		UMaterialInterface* Parent = LoadBaked(TEXT("M_Flat"));

#if WITH_EDITOR
		if (Parent == nullptr)
		{
			UE_LOG(LogLedger, Warning,
				TEXT("flat material built in memory: no baked asset."));
			Parent = BuildFlatMaterial(Outer);
		}
#endif

		if (Parent == nullptr)
		{
			return Fallback(TEXT("M_Flat"));
		}

		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, Outer);
		if (Instance == nullptr)
		{
			return Parent;
		}
		Instance->SetVectorParameterValue(TEXT("Tint"), Colour);
		Instance->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		return Instance;
	}

	UMaterialInterface* CreateFoliageMaterial(UObject* Outer, const FLinearColor& Colour,
		float Roughness)
	{
		// The same arrangement as the flat material: one baked parent, a
		// dynamic instance per colour.
		UMaterialInterface* Parent = LoadBaked(TEXT("M_Foliage"));

#if WITH_EDITOR
		if (Parent == nullptr)
		{
			UE_LOG(LogLedger, Warning,
				TEXT("foliage material built in memory: no baked asset. It will "
				     "not exist in a packaged build until the bake is run."));
			Parent = BuildFoliageMaterial(Outer);
		}
#endif

		if (Parent == nullptr)
		{
			return Fallback(TEXT("M_Foliage"));
		}

		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, Outer);
		if (Instance == nullptr)
		{
			return Parent;
		}
		Instance->SetVectorParameterValue(TEXT("Tint"), Colour);
		Instance->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		return Instance;
	}
}
