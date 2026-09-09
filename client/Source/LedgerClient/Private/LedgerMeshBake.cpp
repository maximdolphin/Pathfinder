// Turning generated geometry into a real static mesh asset.
//
// ADR-0006, and the task that makes the rest of it worth anything. A procedural
// mesh component has no Nanite, no automatic LOD chain, no mesh distance field
// and no instancing. The same geometry saved as a static mesh has all four, and
// none of them is work this project has to do -- they come with the asset type.
//
// **What this is not.** It is not a mesh format, an importer, or a conversion
// layer. FLedgerMeshBuilder already produces the arrays; this hands them to
// Unreal's own static mesh build and asks for the features that only exist on
// the far side of it.

#include "LedgerMeshBake.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "LedgerLog.h"
#include "LedgerMeshBuilder.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace LedgerMesh
{
	namespace
	{
		/// The builder's parallel arrays as a mesh description.
		///
		/// One polygon group, because everything this project generates is one
		/// material per mesh so far. Normals and tangents come across rather
		/// than being recomputed: the builder made them deliberately flat, and
		/// a hull with recomputed shared normals reads as soap.
		void Describe(const FLedgerMeshBuilder& Builder, FMeshDescription& Out)
		{
			FStaticMeshAttributes Attributes(Out);
			Attributes.Register();

			TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
			TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
			TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
			TVertexInstanceAttributesRef<FVector4f> Colours = Attributes.GetVertexInstanceColors();

			const int32 VertexCount = Builder.Vertices.Num();
			Out.ReserveNewVertices(VertexCount);
			Out.ReserveNewVertexInstances(VertexCount);
			Out.ReserveNewTriangles(Builder.Triangles.Num() / 3);
			Out.ReserveNewPolygonGroups(1);

			TArray<FVertexID> VertexIds;
			VertexIds.Reserve(VertexCount);
			for (int32 Index = 0; Index < VertexCount; ++Index)
			{
				const FVertexID Id = Out.CreateVertex();
				Positions[Id] = FVector3f(Builder.Vertices[Index]);
				VertexIds.Add(Id);
			}

			const FPolygonGroupID Group = Out.CreatePolygonGroup();
			Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Default");

			for (int32 Index = 0; Index + 2 < Builder.Triangles.Num(); Index += 3)
			{
				FVertexInstanceID Corners[3];
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const int32 Vertex = Builder.Triangles[Index + Corner];
					const FVertexInstanceID Instance = Out.CreateVertexInstance(VertexIds[Vertex]);
					Normals[Instance] = Builder.Normals.IsValidIndex(Vertex)
						? FVector3f(Builder.Normals[Vertex]) : FVector3f::UpVector;
					UVs.Set(Instance, 0, Builder.UVs.IsValidIndex(Vertex)
						? FVector2f(Builder.UVs[Vertex]) : FVector2f::ZeroVector);
					Colours[Instance] = Builder.Colors.IsValidIndex(Vertex)
						? FVector4f(FLinearColor(Builder.Colors[Vertex]))
						: FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
					Corners[Corner] = Instance;
				}
				Out.CreateTriangle(Group, Corners);
			}
		}
	}

	UStaticMesh* Bake(const FLedgerMeshBuilder& Builder, const FString& PackageName,
		const TCHAR* AssetName, FString& Line)
	{
		if (Builder.Triangles.Num() < 3)
		{
			Line = FString::Printf(TEXT("  %-16s FAILED: nothing to bake"), AssetName);
			return nullptr;
		}

		UPackage* Package = CreatePackage(*PackageName);
		UStaticMesh* Mesh = NewObject<UStaticMesh>(
			Package, FName(AssetName), RF_Public | RF_Standalone);

		FMeshDescription Description;
		Describe(Builder, Description);

		UStaticMesh::FBuildMeshDescriptionsParams Params;
		// Built like an asset, not like a runtime mesh: this is the whole point.
		// The fast path skips the very work -- LOD reduction, distance fields,
		// Nanite -- that the asset exists to get.
		Params.bBuildSimpleCollision = true;
		Params.bFastBuild = false;
		Params.bAllowCpuAccess = false;

		if (!Mesh->BuildFromMeshDescriptions({ &Description }, Params))
		{
			Line = FString::Printf(TEXT("  %-16s FAILED: the mesh build refused it"), AssetName);
			return nullptr;
		}

		// Nanite. The reason this task exists: a procedural mesh cannot have it,
		// and a hull that does gets its own LOD selection per cluster instead of
		// the whole-mesh switch a manual chain gives.
		Mesh->NaniteSettings.bEnabled = true;

		// A distance field, which is what Lumen's software tracing needs and
		// what nothing in this project has ever had -- it is why Lumen had to be
		// forced onto hardware ray tracing.
		Mesh->bGenerateMeshDistanceField = true;

		if (UBodySetup* Body = Mesh->GetBodySetup())
		{
			Body->CollisionTraceFlag = CTF_UseSimpleAndComplex;
		}

		Mesh->Build(/*bInSilent*/ true);
		Mesh->PostEditChange();

		FMetaData& MetaData = Package->GetMetaData();
		MetaData.SetValue(Mesh, TEXT("Ledger.Generator"), TEXT("LedgerMesh::Bake"));
		MetaData.SetValue(Mesh, TEXT("Ledger.Source"), AssetName);

		FAssetRegistryModule::AssetCreated(Mesh);
		Package->MarkPackageDirty();

		const FString FileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().Delete(*FileName, false, true, true);

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FSavePackageResultStruct Result =
			UPackage::Save(Package, Mesh, *FileName, Args);

		if (!Result.IsSuccessful())
		{
			Line = FString::Printf(TEXT("  %-16s FAILED: save returned %d"),
				AssetName, static_cast<int32>(Result.Result));
			return nullptr;
		}

		const int32 Lods = Mesh->GetNumSourceModels();
		Line = FString::Printf(
			TEXT("  %-16s %6d tris  nanite %s  lods %d  collision %s  dist field %s"),
			AssetName, Builder.Triangles.Num() / 3,
			Mesh->NaniteSettings.bEnabled ? TEXT("on ") : TEXT("off"),
			Lods,
			Mesh->GetBodySetup() != nullptr ? TEXT("yes") : TEXT("NO "),
			Mesh->bGenerateMeshDistanceField ? TEXT("yes") : TEXT("NO "));

		UE_LOG(LogLedger, Log, TEXT("mesh bake: %s -> %s"), AssetName, *PackageName);
		return Mesh;
	}
}

#endif
