// Running the material builders once and saving what they produce.
//
// ADR-0006. Every material in this project was `RF_Transient`, built as a node
// graph on each launch inside `#if WITH_EDITOR`, which is why the packaged path
// returned null for all of them and a build with the editor stripped had no
// look at all. It was recorded as a known hole; it was a known hole that meant
// there was no game outside the editor.
//
// **The graphs stay in C++.** That is the part worth keeping: a material that
// is code is a material that diffs, reviews and merges, and one that is a
// binary asset is none of those. So the builders are unchanged and this runs
// them, once, into real packages. The code is what a diff shows; the asset is
// what the game loads.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "LedgerMaterialGraph.h"
#include "LedgerLog.h"
#include "Materials/Material.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace LedgerSurface
{
	namespace
	{
		/// Where baked materials live. Derived from the name rather than typed
		/// at each site, so a material cannot end up somewhere nobody looks.
		FString PackagePathFor(const TCHAR* Name)
		{
			return FString::Printf(TEXT("%s%s"), MaterialPackageRoot, Name);
		}

		bool SaveOne(const TCHAR* Name, TFunctionRef<UMaterialInterface*(UObject*)> Build,
			FString& Line)
		{
			const FString PackageName = PackagePathFor(Name);
			UPackage* Package = CreatePackage(*PackageName);
			if (Package == nullptr)
			{
				Line = FString::Printf(TEXT("  %-14s FAILED: no package"), Name);
				return false;
			}

			UMaterialInterface* Material = Build(Package);
			if (Material == nullptr)
			{
				Line = FString::Printf(TEXT("  %-14s FAILED: the builder returned nothing"), Name);
				return false;
			}

			// The asset records what made it. Unreal's own metadata rather than
			// a sidecar file, so it travels with the asset and shows in the
			// editor's details panel -- and so the question "where did this come
			// from" has an answer at the place somebody asks it.
			//
			// No timestamp. A generated asset that embeds the time it was
			// generated is a generated asset that can never be byte-compared
			// against a re-run, which is the check that says the generator is
			// deterministic.
			FMetaData& MetaData = Package->GetMetaData();
			MetaData.SetValue(Material, TEXT("Ledger.Generator"),
				TEXT("LedgerMaterial::BakeMaterials"));
			MetaData.SetValue(Material, TEXT("Ledger.Source"), Name);
			MetaData.SetValue(Material, TEXT("Ledger.Command"),
				TEXT("UnrealEditor.exe <project> -game -bakematerials"));

			FAssetRegistryModule::AssetCreated(Material);
			Package->MarkPackageDirty();

			const FString FileName = FPackageName::LongPackageNameToFilename(
				PackageName, FPackageName::GetAssetPackageExtension());

			// Any previous file goes first. Saving over one that is already on
			// disk fails, which made the bake work exactly once: the first run
			// wrote three assets, and every run after it reported "could not
			// write the package" for all three. A generator that only works on
			// a clean tree is a generator nobody can re-run, and re-running is
			// the whole point.
			IFileManager::Get().Delete(*FileName, /*RequireExists*/ false,
				/*EvenReadOnly*/ true, /*Quiet*/ true);

			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			const FSavePackageResultStruct Result =
				UPackage::Save(Package, Material, *FileName, Args);

			Line = Result.IsSuccessful()
				? FString::Printf(TEXT("  %-14s %s"), Name, *PackageName)
				: FString::Printf(TEXT("  %-14s FAILED: save returned %d"),
					Name, static_cast<int32>(Result.Result));
			return Result.IsSuccessful();
		}
	}

	bool BakeMaterials(FString& Report)
	{
		int32 Saved = 0;
		int32 Attempted = 0;
		TArray<FString> Lines;

		const auto Record = [&Saved, &Attempted, &Lines](const TCHAR* Name,
			TFunctionRef<UMaterialInterface*(UObject*)> Build)
		{
			++Attempted;
			FString Line;
			if (SaveOne(Name, Build, Line))
			{
				++Saved;
			}
			Lines.Add(Line);
		};

		// The seed only affects the generated detail textures, which the terrain
		// material no longer uses -- it samples the imported scans. Passed for
		// signature compatibility and pinned so a re-bake is reproducible.
		Record(TEXT("M_Terrain"), [](UObject* Outer) { return BuildTerrainMaterial(Outer, 0u); });
		Record(TEXT("M_Water"), [](UObject* Outer) { return BuildWaterMaterial(Outer); });
		Record(TEXT("M_Underwater"), [](UObject* Outer) { return BuildUnderwaterMaterial(Outer); });

		// M_Flat is baked now that its colour and roughness are parameters,
		// so one asset serves every building, tree and hull through a dynamic
		// instance each.
		Record(TEXT("M_Flat"), [](UObject* Outer) { return BuildFlatMaterial(Outer); });
		Record(TEXT("M_Clouds"), [](UObject* Outer) { return BuildCloudMaterial(Outer); });
		Record(TEXT("M_Star"), [](UObject* Outer) { return BuildStarMaterial(Outer); });

		Report = TEXT("Baked materials.\n\n");
		for (const FString& Line : Lines)
		{
			Report += Line + TEXT("\n");
		}
		Report += FString::Printf(TEXT("\n  %d of %d saved\n\n"), Saved, Attempted);
		Report += FString::Printf(TEXT("VERDICT: %s\n"),
			Saved == Attempted ? TEXT("PASS") : TEXT("FAIL"));

		UE_LOG(LogLedger, Log, TEXT("bake: %d of %d materials saved"), Saved, Attempted);
		return Saved == Attempted;
	}
}

#endif
