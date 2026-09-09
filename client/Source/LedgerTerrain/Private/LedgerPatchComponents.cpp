#include "LedgerPatchComponents.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "LedgerPlanet.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshAttributes.h"

namespace
{
	/// Land is section 0 and water section 1 in both backends, so the material
	/// assignment reads the same either way.
	constexpr int32 LandSection = 0;
	constexpr int32 WaterSection = 1;

	void AppendToDescription(
		FMeshDescription& Description,
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UVs,
		const TArray<FVector2D>& MorphUVs,
		const TArray<FColor>& Colors,
		const TArray<FProcMeshTangent>& Tangents,
		FPolygonGroupID Group)
	{
		FStaticMeshAttributes Attributes(Description);
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> InstanceTangents = Attributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector4f> InstanceColors = Attributes.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> InstanceUVs = Attributes.GetVertexInstanceUVs();

		TArray<FVertexID> Added;
		Added.Reserve(Vertices.Num());
		for (const FVector& Position : Vertices)
		{
			const FVertexID Vertex = Description.CreateVertex();
			Positions[Vertex] = FVector3f(Position);
			Added.Add(Vertex);
		}

		// Wound the other way round. MeshDescription's front face is the
		// opposite of the procedural component's, and a patch built under one
		// convention and drawn under the other is invisible from above and
		// solid from below, which reads as "the static backend does not work"
		// rather than as a winding bug.
		const int32 Order[3] = { 2, 1, 0 };

		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			FVertexInstanceID Corners[3];
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const int32 Source = Triangles[Index + Order[Corner]];
				if (!Added.IsValidIndex(Source))
				{
					return;
				}

				const FVertexInstanceID Instance = Description.CreateVertexInstance(Added[Source]);
				Corners[Corner] = Instance;

				InstanceNormals[Instance] = Normals.IsValidIndex(Source)
					? FVector3f(Normals[Source]) : FVector3f::ZAxisVector;

				if (Tangents.IsValidIndex(Source))
				{
					InstanceTangents[Instance] = FVector3f(Tangents[Source].TangentX);
					BinormalSigns[Instance] = Tangents[Source].bFlipTangentY ? -1.0f : 1.0f;
				}
				else
				{
					InstanceTangents[Instance] = FVector3f::XAxisVector;
					BinormalSigns[Instance] = 1.0f;
				}

				// Straight over 255, not FLinearColor(FColor). The vertex
				// colour carries a mask rather than a colour, and the sRGB
				// decode would bend it. This exact mistake cost an evening on
				// the baked trees.
				if (Colors.IsValidIndex(Source))
				{
					const FColor& Colour = Colors[Source];
					InstanceColors[Instance] = FVector4f(
						Colour.R / 255.0f, Colour.G / 255.0f,
						Colour.B / 255.0f, Colour.A / 255.0f);
				}
				else
				{
					InstanceColors[Instance] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
				}

				InstanceUVs.Set(Instance, 0, UVs.IsValidIndex(Source)
					? FVector2f(UVs[Source]) : FVector2f::ZeroVector);
				InstanceUVs.Set(Instance, 1, MorphUVs.IsValidIndex(Source)
					? FVector2f(MorphUVs[Source]) : FVector2f::ZeroVector);
			}

			Description.CreateTriangle(Group, Corners);
		}
	}
}

namespace LedgerTerrain
{
	ELedgerPatchComponent PatchComponentKind()
	{
		FString Requested;
		if (FParse::Value(FCommandLine::Get(), TEXT("terraincomponent="), Requested)
			&& Requested.Equals(TEXT("static"), ESearchCase::IgnoreCase))
		{
			return ELedgerPatchComponent::Static;
		}
		return ELedgerPatchComponent::Procedural;
	}

	const TCHAR* PatchComponentName(ELedgerPatchComponent Kind)
	{
		return Kind == ELedgerPatchComponent::Static ? TEXT("static") : TEXT("procedural");
	}

	UMeshComponent* MakePatchComponent(AActor& Owner, ELedgerPatchComponent Kind)
	{
		UMeshComponent* Mesh = nullptr;

		if (Kind == ELedgerPatchComponent::Static)
		{
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(&Owner);
			Component->SetMobility(EComponentMobility::Movable);
			Mesh = Component;
		}
		else
		{
			UProceduralMeshComponent* Component = NewObject<UProceduralMeshComponent>(&Owner);
			Component->bUseAsyncCooking = true;
			Mesh = Component;
		}

		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetVisibility(false);
		Mesh->SetupAttachment(Owner.GetRootComponent());
		Mesh->RegisterComponent();
		return Mesh;
	}

	void UploadPatch(
		UMeshComponent& Mesh, FLedgerPatchJob& Job,
		UMaterialInterface* Land, UMaterialInterface* Water)
	{
		if (UProceduralMeshComponent* Procedural = Cast<UProceduralMeshComponent>(&Mesh))
		{
			Procedural->ClearMeshSection(LandSection);

			// UV1 carries the geomorph target. The overload that takes it wants
			// all four channels, so two go in empty.
			const TArray<FVector2D> Unused;
			Procedural->CreateMeshSection(
				LandSection, Job.Vertices, Job.Triangles, Job.Normals, Job.UVs,
				Job.MorphUVs, Unused, Unused,
				Job.Colors, Job.Tangents, Job.bWithCollision);
			if (Land != nullptr)
			{
				Procedural->SetMaterial(LandSection, Land);
			}

			Procedural->ClearMeshSection(WaterSection);
			if (Job.bHasWater)
			{
				Procedural->CreateMeshSection(
					WaterSection, Job.WaterVertices, Job.WaterTriangles, Job.WaterNormals,
					Job.WaterUVs, Job.WaterColors, Job.WaterTangents, /*bCreateCollision*/ false);
				if (Water != nullptr)
				{
					Procedural->SetMaterial(WaterSection, Water);
				}
			}
			return;
		}

		UStaticMeshComponent* Component = CastChecked<UStaticMeshComponent>(&Mesh);

		// A fresh transient mesh per upload. Reusing one is not on offer:
		// render data is built once and there is no runtime path to rebuild it
		// in place, which is a large part of what this comparison measures.
		UStaticMesh* Built = NewObject<UStaticMesh>(
			GetTransientPackage(), NAME_None, RF_Transient);
		Built->GetStaticMaterials().Add(FStaticMaterial(Land));
		if (Job.bHasWater)
		{
			Built->GetStaticMaterials().Add(FStaticMaterial(Water));
		}

		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		Attributes.GetVertexInstanceUVs().SetNumChannels(2);

		const FPolygonGroupID LandGroup = Description.CreatePolygonGroup();
		AppendToDescription(Description, Job.Vertices, Job.Triangles, Job.Normals,
			Job.UVs, Job.MorphUVs, Job.Colors, Job.Tangents, LandGroup);

		if (Job.bHasWater)
		{
			const FPolygonGroupID WaterGroup = Description.CreatePolygonGroup();
			AppendToDescription(Description, Job.WaterVertices, Job.WaterTriangles,
				Job.WaterNormals, Job.WaterUVs, Job.WaterUVs, Job.WaterColors,
				Job.WaterTangents, WaterGroup);
		}

		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bMarkPackageDirty = false;
		Params.bCommitMeshDescription = false;
		Params.bBuildSimpleCollision = false;
		// The only build available outside the editor. It skips the full
		// pipeline, which is also why there are no LODs and no Nanite here:
		// both are produced by the offline build this cannot run.
		Params.bFastBuild = true;
		Built->BuildFromMeshDescriptions({ &Description }, Params);

		Component->SetStaticMesh(Built);
		Component->SetMaterial(LandSection, Land);
		if (Job.bHasWater && Water != nullptr)
		{
			Component->SetMaterial(WaterSection, Water);
		}
	}

	void ClearPatch(UMeshComponent& Mesh)
	{
		if (UProceduralMeshComponent* Procedural = Cast<UProceduralMeshComponent>(&Mesh))
		{
			Procedural->ClearAllMeshSections();
			return;
		}
		CastChecked<UStaticMeshComponent>(&Mesh)->SetStaticMesh(nullptr);
	}
}
