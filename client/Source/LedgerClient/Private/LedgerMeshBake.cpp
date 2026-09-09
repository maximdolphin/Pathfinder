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
#include "MaterialDomain.h"
#include "Materials/Material.h"
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
		void Describe(const FLedgerMeshBuilder& Builder, FMeshDescription& Out,
			int32 SecondGroupStartTriangle)
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

			// One group, or two if the caller says where the second starts.
			//
			// Two exists because a tree is a brown trunk and a green canopy in
			// one mesh, and the colour was carried per vertex -- which survives
			// a procedural mesh component and does not survive the trip through
			// a baked static mesh onto an instanced component. Three attempts
			// to make it survive were three wrong guesses. A material slot each
			// is how this is normally done and does not depend on any of it.
			const FPolygonGroupID Groups[2] = { Out.CreatePolygonGroup(), FPolygonGroupID() };
			Attributes.GetPolygonGroupMaterialSlotNames()[Groups[0]] = TEXT("Slot0");

			const bool bSplit = SecondGroupStartTriangle > 0;
			FPolygonGroupID Second = Groups[0];
			if (bSplit)
			{
				Second = Out.CreatePolygonGroup();
				Attributes.GetPolygonGroupMaterialSlotNames()[Second] = TEXT("Slot1");
			}

			for (int32 Index = 0; Index + 2 < Builder.Triangles.Num(); Index += 3)
			{
				const int32 Triangle = Index / 3;
				const FPolygonGroupID Group =
					(bSplit && Triangle >= SecondGroupStartTriangle) ? Second : Groups[0];
				FVertexInstanceID Corners[3];
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const int32 Vertex = Builder.Triangles[Index + Corner];
					const FVertexInstanceID Instance = Out.CreateVertexInstance(VertexIds[Vertex]);
					Normals[Instance] = Builder.Normals.IsValidIndex(Vertex)
						? FVector3f(Builder.Normals[Vertex]) : FVector3f::UpVector;
					UVs.Set(Instance, 0, Builder.UVs.IsValidIndex(Vertex)
						? FVector2f(Builder.UVs[Vertex]) : FVector2f::ZeroVector);
					// Straight 0-1, not through FLinearColor.
					//
					// FLinearColor(FColor) applies the sRGB decode, and these
					// colours were authored for the procedural mesh path, which
					// does not. Decoding them turned a canopy green of (44, 68,
					// 36) into roughly 0.02 of linear and rendered three hundred
					// and forty-two trees as black cut-outs -- correctly placed,
					// correctly instanced, lit, and the wrong colour by a factor
					// of twelve.
					const FColor Colour = Builder.Colors.IsValidIndex(Vertex)
						? Builder.Colors[Vertex] : FColor::White;
					Colours[Instance] = FVector4f(
						Colour.R / 255.0f, Colour.G / 255.0f,
						Colour.B / 255.0f, Colour.A / 255.0f);
					Corners[Corner] = Instance;
				}
				Out.CreateTriangle(Group, Corners);
			}
		}
	}

	UStaticMesh* Bake(const FLedgerMeshBuilder& Builder, const FString& PackageName,
		const TCHAR* AssetName, FString& Line, bool bNanite,
		int32 SecondGroupStartTriangle)
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
		Describe(Builder, Description, SecondGroupStartTriangle);

		// One material slot, matching the polygon group's slot name.
		//
		// BuildFromMeshDescriptions does not create these, and a static mesh
		// with no slots cannot be given a material at all: SetMaterial(0, ...)
		// on the component succeeds, reports the material back, and renders
		// nothing. Three hundred and forty-two trees came out as black cut-outs
		// and both earlier explanations for it -- a missing assignment and an
		// sRGB decode -- were wrong. The component was asked what it had and
		// answered "0 material slots".
		Mesh->GetStaticMaterials().Add(FStaticMaterial(
			UMaterial::GetDefaultMaterial(MD_Surface), TEXT("Slot0"), TEXT("Slot0")));
		if (SecondGroupStartTriangle > 0)
		{
			Mesh->GetStaticMaterials().Add(FStaticMaterial(
				UMaterial::GetDefaultMaterial(MD_Surface), TEXT("Slot1"), TEXT("Slot1")));
		}

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

		// Nanite where it earns its place. A procedural mesh cannot have it at
		// all, which is the reason this task exists -- but it is for meshes
		// dense enough that per-cluster selection beats drawing the whole
		// thing, and it does not carry mesh vertex colours through to the
		// material. On a forty-six triangle tree that trade is all cost: the
		// trees came back uniformly pale because the only colour they have is
		// per-vertex.
		Mesh->NaniteSettings.bEnabled = bNanite;

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
			TEXT("  %-16s %6d tris  nanite %s  lods %d  slots %d  collision %s  dist field %s"),
			AssetName, Builder.Triangles.Num() / 3,
			Mesh->NaniteSettings.bEnabled ? TEXT("on ") : TEXT("off"),
			Lods,
			Mesh->GetStaticMaterials().Num(),
			Mesh->GetBodySetup() != nullptr ? TEXT("yes") : TEXT("NO "),
			Mesh->bGenerateMeshDistanceField ? TEXT("yes") : TEXT("NO "));

		UE_LOG(LogLedger, Log, TEXT("mesh bake: %s -> %s"), AssetName, *PackageName);
		return Mesh;
	}
}

#endif
