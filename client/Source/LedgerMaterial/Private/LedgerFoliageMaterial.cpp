// Leaves that the wind actually reaches. T093's vegetation consumer.
//
// The flat material, plus one thing: a world-position offset read from the wind
// collection, so a canopy leans the way the air is going by as much as the air
// is pushing. Until this existed the collection was published every tick and
// read by nothing -- the log said "materials will not read the wind" on every
// run, and the roadmap said vegetation was wired.

#include "LedgerSurface.h"

#if WITH_EDITOR

#include "LedgerLog.h"
#include "LedgerMaterialGraph.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialParameterCollection.h"

namespace LedgerSurface
{
	UMaterialInterface* BuildFoliageMaterial(UObject* Outer)
	{
		UMaterial* Material = NewMaterial(Outer, TEXT("M_Foliage"));
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);
		Material->bUsedWithInstancedStaticMeshes = true;

		FGraph Graph;
		Graph.Material = Material;

		// The same surface as M_Flat, so a canopy with no wind looks exactly as
		// it did before this material existed.
		UMaterialExpression* Tint =
			Graph.VectorParameter(TEXT("Tint"), FLinearColor::White);
		UMaterialExpression* Roughness = Graph.ScalarParameter(TEXT("Roughness"), 0.88f);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		// The tint alone. The settlement gives each slot its own colour, and the
		// bake also writes that colour into the vertices, so tint times vertex
		// colour was the colour squared: an albedo of about 0.01, a canopy that
		// read as a black cut-out under anything but direct sun.
		EditorData->BaseColor.Expression = Tint;
		EditorData->Roughness.Expression = Roughness;
		EditorData->Specular.Expression = Graph.Constant(0.4f);

		UMaterialParameterCollection* Collection =
			LoadObject<UMaterialParameterCollection>(
				nullptr, TEXT("/Game/Materials/MPC_LedgerWind"));
		if (Collection == nullptr)
		{
			// Loud, because a foliage material that does not sway is the exact
			// failure this file was written to end, and it would otherwise look
			// like a calm day.
			UE_LOG(LogLedger, Error,
				TEXT("foliage material: no /Game/Materials/MPC_LedgerWind, so the "
					 "trees will not move. Bake it: -bakematerials"));
			Material->PostEditChange();
			return Material;
		}

		UMaterialExpression* Direction =
			Graph.CollectionParameter(Collection, TEXT("WindDirection"));
		UMaterialExpression* Speed =
			Graph.CollectionParameter(Collection, TEXT("WindSpeed"));

		// **How far it leans goes as the dynamic pressure, not the speed.** The
		// force on a canopy is drag, half rho v squared times an area, so twice
		// the wind is four times the lean. Normalised against 30 m/s -- a gale,
		// past which a real tree breaks rather than bends further -- and
		// saturated there.
		UMaterialExpression* Push = Graph.Saturate(Graph.Divide(
			Graph.Multiply(Speed, Speed), Graph.Constant(900.0f)));

		// **And it goes as the height squared.** A trunk is a cantilever fixed
		// at the ground, and a cantilever's deflection under a load grows with
		// the square of the distance from the fixed end. The mesh is authored
		// at unit scale with its base at zero, so its own local Z is exactly
		// that distance -- which an instance's scale then carries with it.
		// Offsets excluded: an offset computed from the position it is about
		// to move is a feedback loop the compiler rightly refuses.
		UMaterialExpressionLocalPosition* Local =
			Graph.Make<UMaterialExpressionLocalPosition>();
		Local->IncludedOffsets = EPositionIncludedOffsets::ExcludeOffsets;
		Local->LocalOrigin = ELocalPositionOrigin::Instance;
		UMaterialExpression* Height = Graph.Divide(
			Graph.Mask(Local, false, false, true), Graph.Constant(1800.0f));
		UMaterialExpression* Bend = Graph.Multiply(Height, Height);

		// A metre and a half at the top of the crown in a gale.
		UMaterialExpression* Lean = Graph.Multiply(
			Graph.Multiply(Direction, Graph.Multiply(Push, Bend)),
			Graph.Constant(150.0f));

		// Gusting on top of the lean, a tenth of it, at about half a hertz with
		// a phase from where the tree stands so a forest does not sway in step.
		UMaterialExpressionTime* Time = Graph.Make<UMaterialExpressionTime>();
		UMaterialExpressionWorldPosition* Where =
			Graph.Make<UMaterialExpressionWorldPosition>();
		Where->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
		UMaterialExpression* Phase = Graph.Divide(
			Graph.Add(Graph.Mask(Where, true, false, false),
				Graph.Mask(Where, false, true, false)),
			Graph.Constant(1700.0f));
		UMaterialExpression* Gust = Graph.Sine(
			Graph.Add(Graph.Multiply(Time, Graph.Constant(0.5f)), Phase));
		UMaterialExpression* Flutter = Graph.Multiply(
			Lean, Graph.Add(Graph.Constant(1.0f), Graph.Multiply(Gust, Graph.Constant(0.1f))));

		EditorData->WorldPositionOffset.Expression = Flutter;

		Material->PostEditChange();
		return Material;
	}
}

#endif
