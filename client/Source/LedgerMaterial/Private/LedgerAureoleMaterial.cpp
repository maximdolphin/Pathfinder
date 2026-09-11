// The aureole: the sky near the sun, channel by channel. T090.
//
// Unreal's Sky Atmosphere gives the aerosol one Henyey-Greenstein lobe for all
// three channels. Dust a micron or two across does not scatter like that: much
// of what it scatters is diffraction, a peak around the sun whose width goes as
// the wavelength over the radius -- narrower in the blue, and so brighter there
// at its centre. That is the blue glow round a Martian sunset, and no single
// lobe makes it. This pass multiplies each channel of the sky by the ratio of
// the phase function with the peak to the engine's without it,
//
//     (1 - F) + F * Diffraction(theta) / HG(theta)
//
// on sky pixels only. Both phase functions integrate to one, so the pass moves
// light towards the sun rather than adding any. F and the widths come from the
// composition (LedgerAir::AureoleFraction, AureoleWidthRadians).
//
// ponytail: applied to the whole sky pixel, multiple scattering and gas
// included, as if all of it near the sun were the aerosol's single scattering;
// on a world where F is large that is nearly true, and where it is not, F is
// small.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"

namespace LedgerSurface
{
	UMaterialInterface* BuildAureoleMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Aureole"));
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_PostProcess;
		// In linear light, before the tonemapper: the pass is a ratio of radiances,
		// and after tonemapping the sky near the sun is already clipped.
		Material->BlendableLocation = BL_SceneColorAfterDOF;

		FGraph Graph;
		Graph.Material = Material;

		UMaterialExpressionSceneTexture* SceneColour = Graph.Make<UMaterialExpressionSceneTexture>();
		SceneColour->SceneTextureId = PPI_PostProcessInput0;
		UMaterialExpressionSceneTexture* SceneDepth = Graph.Make<UMaterialExpressionSceneTexture>();
		SceneDepth->SceneTextureId = PPI_SceneDepth;
		UMaterialExpressionCameraVectorWS* Eye = Graph.Make<UMaterialExpressionCameraVectorWS>();

		UMaterialExpressionCustom* Pass = Graph.Make<UMaterialExpressionCustom>();
		Pass->OutputType = CMOT_Float3;
		Pass->Description = TEXT("LedgerAureole");
		// Sky pixels have no depth written -- anything past 10,000 km is sky.
		Pass->Code = TEXT(
			"float c = clamp(dot(-normalize(Eye), normalize(Sun)), -1.0, 1.0);\n"
			"float t = acos(c);\n"
			"float hg = (1.0 - G * G) / (12.5663706 * pow(max(1.0 + G * G - 2.0 * G * c, 1e-6), 1.5));\n"
			"float3 w2 = max(Width * Width, 1e-8);\n"
			"float3 peak = exp(-(t * t) / w2) / (3.14159265 * w2);\n"
			"float3 ratio = (1.0 - Fraction) + Fraction * peak / max(hg, 1e-6);\n"
			"float sky = Depth > 1.0e9 ? 1.0 : 0.0;\n"
			"return Scene * lerp(float3(1.0, 1.0, 1.0), ratio, sky);");
		Pass->Inputs.Reset();
		auto In = [Pass](const TCHAR* Name, UMaterialExpression* Expression)
		{
			FCustomInput& Input = Pass->Inputs.AddDefaulted_GetRef();
			Input.InputName = Name;
			Input.Input.Expression = Expression;
		};
		In(TEXT("Scene"), Graph.Mask(SceneColour, true, true, true));
		In(TEXT("Depth"), Graph.Mask(SceneDepth, true, false, false));
		In(TEXT("Eye"), Eye);
		In(TEXT("Sun"), Graph.Mask(Graph.VectorParameter(TEXT("SunDirection"), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f)), true, true, true));
		In(TEXT("Width"), Graph.Mask(Graph.VectorParameter(TEXT("AureoleWidth"), FLinearColor(0.5f, 0.5f, 0.5f, 0.0f)), true, true, true));
		In(TEXT("Fraction"), Graph.Mask(Graph.VectorParameter(TEXT("AureoleFraction"), FLinearColor::Black), true, true, true));
		In(TEXT("G"), Graph.ScalarParameter(TEXT("Anisotropy"), 0.75f));

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		// Post-process materials write their result to emissive.
		EditorData->EmissiveColor.Expression = Pass;

		Material->PostEditChange();
		return Material;
	}
}

#endif
