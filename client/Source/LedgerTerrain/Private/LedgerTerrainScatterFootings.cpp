// Whether the stones are standing on the ground. T437.
//
// **Measured against what a player can walk on, not against the function the
// stones were placed from.** The savanna capture had two lines of boulders
// climbing a hillside into the sky, and every theory about it -- the patch
// gate, the slope limit, the mesh-versus-function offset -- was a theory about
// the placement code. This asks the world instead: trace straight down through
// each instance and say how far it is from the surface the trace hits, and
// beside that how far the height function thinks the ground is. A stone off
// the drawn ground but on the function is a placement that used the wrong
// surface; a stone off both is a stone in the wrong place entirely.
//
// **And how steep that ground is.** The first run found every stone within
// 400 m of all four cameras on the ground to within 1.5 m -- so a column of
// them is standing on something. The placement's slope limit is measured on
// the height function a cell away; this measures it on the surface under the
// stone, from the trace's own hit normal.

#include "LedgerPlanet.h"

#include "CollisionQueryParams.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"

int32 ALedgerPlanet::MeasureScatterFootings(
	const FVector& Near, double WithinMetres, FString& OutReport,
	TArray<FLedgerFooting>* OutAll) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0;
	}

	// The stones themselves are not ground.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LedgerScatterFooting), true);
	for (const UHierarchicalInstancedStaticMeshComponent* Component : ScatterComponents)
	{
		if (Component != nullptr)
		{
			Params.AddIgnoredComponent(Component);
		}
	}

	struct FFooting
	{
		double AboveDrawnMetres = 0.0;
		double AboveFunctionMetres = 0.0;
		double SlopeDegrees = 0.0;
		double RangeMetres = 0.0;
		int32 Component = 0;
		int32 Instance = 0;
	};

	const FVector3d Centre = FVector3d(GetActorLocation());
	TArray<FFooting> All;
	int32 Missed = 0;
	int32 SlopeBands[6] = {};   // 0-15, 15-30, ... 75-90 degrees
	for (int32 ComponentIndex = 0; ComponentIndex < ScatterComponents.Num(); ++ComponentIndex)
	{
		const UHierarchicalInstancedStaticMeshComponent* Component = ScatterComponents[ComponentIndex];
		if (Component == nullptr)
		{
			continue;
		}
		for (int32 Instance = 0; Instance < Component->GetInstanceCount(); ++Instance)
		{
			FTransform Transform;
			if (!Component->GetInstanceTransform(Instance, Transform, true))
			{
				continue;
			}
			const FVector3d Where = FVector3d(Transform.GetLocation());
			const double Range = FVector3d::Dist(Where, FVector3d(Near)) / 100.0;
			if (Range > WithinMetres)
			{
				continue;
			}

			const FVector3d Up = (Where - Centre).GetSafeNormal();
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit,
				FVector(Where + Up * 20000.0), FVector(Where - Up * 20000.0),
				ECC_Visibility, Params))
			{
				++Missed;
				continue;
			}

			FFooting Footing;
			Footing.AboveDrawnMetres =
				FVector3d::DotProduct(Where - FVector3d(Hit.ImpactPoint), Up) / 100.0;
			Footing.AboveFunctionMetres =
				((Where - Centre).Length() - SurfaceRadiusAt(Up)) / 100.0;
			Footing.SlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector3d::DotProduct(FVector3d(Hit.ImpactNormal).GetSafeNormal(), Up), -1.0, 1.0)));
			Footing.RangeMetres = Range;
			Footing.Component = ComponentIndex;
			Footing.Instance = Instance;
			++SlopeBands[FMath::Clamp(static_cast<int32>(Footing.SlopeDegrees / 15.0), 0, 5)];
			All.Add(Footing);
			if (OutAll != nullptr)
			{
				FLedgerFooting& Out = OutAll->AddDefaulted_GetRef();
				Out.Where = FVector(Where);
				Out.AboveDrawnMetres = Footing.AboveDrawnMetres;
				Out.AboveFunctionMetres = Footing.AboveFunctionMetres;
				Out.SlopeDegrees = Footing.SlopeDegrees;
				Out.RangeMetres = Footing.RangeMetres;
				Out.Component = Footing.Component;
				Out.Instance = Footing.Instance;
				const UPrimitiveComponent* Ground = Hit.GetComponent();
				Out.GroundSection = MeshPool.IndexOfByPredicate(
					[Ground](const auto& Pooled) { return Pooled.Get() == Ground; });
				Out.bGroundActive = ActiveSections.FindKey(Out.GroundSection) != nullptr;
				Out.bGroundRendered = Ground != nullptr && Ground->WasRecentlyRendered(0.5f);
				Out.bGroundBoundsHold = Ground != nullptr
					&& Ground->Bounds.GetBox().ExpandBy(100.0).IsInside(Hit.ImpactPoint);
			}
		}
	}

	// ponytail: 1.5 m, because a one-metre stone pivoted at its middle is
	// legitimately half a metre off; the fault being hunted is tens.
	TArray<FFooting> Off = All.FilterByPredicate(
		[](const FFooting& F) { return FMath::Abs(F.AboveDrawnMetres) > 1.5; });
	Off.Sort([](const FFooting& A, const FFooting& B)
	{
		return FMath::Abs(A.AboveDrawnMetres) > FMath::Abs(B.AboveDrawnMetres);
	});
	All.Sort([](const FFooting& A, const FFooting& B) { return A.SlopeDegrees > B.SlopeDegrees; });

	OutReport += FString::Printf(
		TEXT("stones within %.0f m      %d measured, %d with no ground under them, %d more than 1.5 m off it\n"),
		WithinMetres, All.Num(), Missed, Off.Num());
	OutReport += FString::Printf(
		TEXT("drawn slope under them   0-15: %d  15-30: %d  30-45: %d  45-60: %d  60-75: %d  75-90: %d (degrees)\n"),
		SlopeBands[0], SlopeBands[1], SlopeBands[2], SlopeBands[3], SlopeBands[4], SlopeBands[5]);
	auto Line = [this](const TCHAR* What, const FFooting& Footing)
	{
		return FString::Printf(
			TEXT("  %s %5.1f deg, %+7.2f m above the drawn ground, %+7.2f m above the function, %5.0f m away  (variant %d bucket %d, instance %d)\n"),
			What, Footing.SlopeDegrees, Footing.AboveDrawnMetres, Footing.AboveFunctionMetres,
			Footing.RangeMetres, Footing.Component / ScatterBuckets,
			Footing.Component % ScatterBuckets, Footing.Instance);
	};
	for (int32 Index = 0; Index < FMath::Min(Off.Num(), 8); ++Index)
	{
		OutReport += Line(TEXT("off  "), Off[Index]);
	}
	for (int32 Index = 0; Index < FMath::Min(All.Num(), 8); ++Index)
	{
		OutReport += Line(TEXT("steep"), All[Index]);
	}
	return Off.Num();
}
