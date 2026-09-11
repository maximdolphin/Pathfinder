// The ground fog's cold pool, as a volume voxelised into volumetric fog. T091.
//
// **Why not a local fog volume.** Its height term is Extinction *
// exp(-Falloff * (z - Offset)) everywhere in a sphere, so a level top is an
// exponential that explodes below it -- over every piece of ground inside the
// sphere's lower cap, not just the valley -- and a top near the rim of a big
// sphere puts the pool where the radial density goes to zero. A pool twelve
// kilometres wide and eighty-six metres deep is not a shape it has. A box is,
// and its density can simply stop at the level.

#include "LedgerSurface.h"
#include "LedgerMaterialGraph.h"
#include "LedgerLog.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionLocalPosition.h"

#if WITH_EDITOR
namespace LedgerSurface
{
	UMaterialInterface* BuildGroundFogMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_GroundFog"));
		if (Material == nullptr)
		{
			return nullptr;
		}
		Material->MaterialDomain = MD_Volume;
		// Additive, as every volume material must be: see LedgerCloudMaterial.cpp.
		Material->BlendMode = BLEND_Additive;

		FGraph Graph;
		Graph.Material = Material;

		// In the unit cube's own space, -50 to 50 cm on each axis. The component
		// scales and turns the box into place, so this is float-precise however
		// far the pool is from the planet's centre, and level with the site.
		UMaterialExpressionLocalPosition* Local = Graph.Make<UMaterialExpressionLocalPosition>();
		UMaterialExpression* Height = Graph.Divide(
			Graph.Add(Graph.Mask(Local, false, false, true), Graph.Constant(50.0f)),
			Graph.Constant(100.0f));

		// Fog below the level, clear air above it, a short soft edge between.
		UMaterialExpression* Top = Graph.ScalarParameter(TEXT("TopFraction"), 0.5f);
		UMaterialExpression* Soft = Graph.ScalarParameter(TEXT("SoftFraction"), 0.05f);
		UMaterialExpression* Below = Graph.Saturate(
			Graph.Divide(Graph.Subtract(Top, Height), Soft));

		// And thinning to nothing towards the sides, so the box has no walls.
		UMaterialExpressionAbs* AcrossX = Graph.Make<UMaterialExpressionAbs>();
		AcrossX->Input.Expression = Graph.Mask(Local, true, false, false);
		UMaterialExpressionAbs* AcrossY = Graph.Make<UMaterialExpressionAbs>();
		AcrossY->Input.Expression = Graph.Mask(Local, false, true, false);
		UMaterialExpression* Across = Graph.Divide(Graph.Max(AcrossX, AcrossY), Graph.Constant(50.0f));
		UMaterialExpression* Edge = Graph.ScalarParameter(TEXT("EdgeFraction"), 0.15f);
		UMaterialExpression* Inside = Graph.Saturate(Graph.Divide(Graph.OneMinus(Across), Edge));

		UMaterialExpression* Density = Graph.ScalarParameter(TEXT("Density"), 0.0f);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		// Extinction per metre is the SubsurfaceColor pin in a volume domain.
		EditorData->BaseColor.Expression = Graph.Constant3(FLinearColor(0.92f, 0.94f, 0.97f));
		EditorData->SubsurfaceColor.Expression = Graph.Multiply(Graph.Multiply(Below, Inside), Density);
		EditorData->EmissiveColor.Expression = Graph.Constant3(FLinearColor::Black);
		Material->PostEditChange();
		return Material;
	}
}
#endif
