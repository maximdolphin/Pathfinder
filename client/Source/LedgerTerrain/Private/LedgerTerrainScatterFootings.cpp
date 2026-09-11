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

#include "LedgerPlanet.h"

#include "CollisionQueryParams.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"

int32 ALedgerPlanet::MeasureScatterFootings(
	const FVector& Near, double WithinMetres, FString& OutReport) const
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
		double RangeMetres = 0.0;
		int32 Component = 0;
		int32 Instance = 0;
	};

	const FVector3d Centre = FVector3d(GetActorLocation());
	TArray<FFooting> Off;
	int32 Measured = 0;
	int32 Missed = 0;
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

			++Measured;
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
			Footing.RangeMetres = Range;
			Footing.Component = ComponentIndex;
			Footing.Instance = Instance;
			// ponytail: 1.5 m, because a one-metre stone pivoted at its middle is
			// legitimately half a metre off; the fault being hunted is tens.
			if (FMath::Abs(Footing.AboveDrawnMetres) > 1.5)
			{
				Off.Add(Footing);
			}
		}
	}

	Off.Sort([](const FFooting& A, const FFooting& B)
	{
		return FMath::Abs(A.AboveDrawnMetres) > FMath::Abs(B.AboveDrawnMetres);
	});

	OutReport += FString::Printf(
		TEXT("stones within %.0f m      %d measured, %d with no ground under them, %d more than 1.5 m off it\n"),
		WithinMetres, Measured, Missed, Off.Num());
	for (int32 Index = 0; Index < FMath::Min(Off.Num(), 8); ++Index)
	{
		const FFooting& Footing = Off[Index];
		OutReport += FString::Printf(
			TEXT("  %+8.2f m above the drawn ground, %+8.2f m above the function, %5.0f m away  (component %d = variant %d bucket %d, instance %d)\n"),
			Footing.AboveDrawnMetres, Footing.AboveFunctionMetres, Footing.RangeMetres,
			Footing.Component, Footing.Component / ScatterBuckets,
			Footing.Component % ScatterBuckets, Footing.Instance);
	}
	return Off.Num();
}
