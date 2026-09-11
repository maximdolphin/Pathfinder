// The scanned scatter's own material: rocks and plants from Poly Haven, each
// drawn with the textures it came with. Realism.
//
// **Not the importer's material.** Interchange gives every glTF mesh an
// instance of its M_Default, which switches each texture on with a static
// switch -- a separate shader permutation per combination -- and in these runs
// those instances drew untextured: grey boulders, and fern fronds as grey
// opaque cards with the alpha never reaching the mask. One material that always
// samples its textures, and a dynamic instance per mesh holding them, has no
// permutation to be missing.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "Engine/Texture2D.h"
#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"

namespace LedgerSurface
{
	UMaterialInterface* BuildScatterMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Scatter"));
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);
		// Cut out and two-sided: a frond is a card with its shape in the alpha,
		// seen from both sides. A stone's alpha is one everywhere, so it is solid.
		Material->BlendMode = BLEND_Masked;
		Material->TwoSided = true;
		Material->bUsedWithInstancedStaticMeshes = true;
		Material->bUsedWithNanite = true;

		// Defaults of the same kinds the imported textures are -- a colour
		// texture for the colour, a normal map for the normal -- because a
		// texture parameter whose default does not match its sampler type fails
		// to compile, and a failed material is drawn as the engine default.
		UTexture2D* White = LoadObject<UTexture2D>(nullptr,
			TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		UTexture2D* Flat = LoadObject<UTexture2D>(nullptr,
			TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));

		FGraph Graph;
		Graph.Material = Material;
		// The glTF parameter names, so an instance is filled from the imported
		// material one for one.
		UMaterialExpression* Albedo = Graph.SampleParameter(
			TEXT("BaseColorTexture"), White, nullptr, SAMPLERTYPE_Color);
		UMaterialExpression* Normal = Graph.SampleParameter(
			TEXT("NormalTexture"), Flat, nullptr, SAMPLERTYPE_Normal);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->BaseColor.Expression = Albedo;
		EditorData->BaseColor.OutputIndex = 0;
		EditorData->OpacityMask.Expression = Albedo;
		EditorData->OpacityMask.OutputIndex = 4;
		EditorData->Normal.Expression = Normal;
		EditorData->Normal.OutputIndex = 0;
		// ponytail: one roughness for rock and leaf alike; sample the glTF
		// metal-roughness texture when a surface needs to shine.
		EditorData->Roughness.Expression = Graph.Constant(0.85f);
		EditorData->Metallic.Expression = Graph.Constant(0.0f);

		Material->PostEditChange();
		return Material;
	}
}

#endif
