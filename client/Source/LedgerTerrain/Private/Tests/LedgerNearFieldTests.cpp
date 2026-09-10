// How much the ground changes when the LOD does. T429, and the bug it shipped.
//
// The near-field band is band-limited per patch, so the height field is a
// function of the grid as well as the position. That is deliberate and argued
// in LedgerTerrainMath.h -- and it has a failure mode that took a person
// flying the game to notice: if adjacent LOD depths disagree by too much, the
// ground re-forms as the LOD rings sweep across it, which reads as the terrain
// sliding and rolling under a moving camera.
//
// Morphing cannot hide it. A morph target is the bilinear average of a patch's
// own samples -- what the parent would be if the parent were a smoothed copy of
// this patch. When the parent samples a materially different field, the morph
// eases towards a surface the parent never draws.
//
// So the quantity that matters is the disagreement between adjacent depths, and
// it is measurable in milliseconds without a world.

#include "LedgerPlanet.h"
#include "LedgerTerrainMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// Vertex spacing at each depth, metres. 64 quads per patch over a
	/// 6,371 km planet: (R * pi/2) / 2^depth / 64.
	double SpacingAt(int32 Depth)
	{
		return (6371000.0 * PI * 0.5) / FMath::Pow(2.0, static_cast<double>(Depth)) / 64.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerNearFieldStaysStillAcrossLod,
	"Ledger.NearField.AdjacentDepthsAgree",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerNearFieldStaysStillAcrossLod::RunTest(const FString&)
{
	FLedgerTerrainParams Params;
	Params.Seed = 1337;
	Params.Radius = 637100000.0;
	Params.MaxElevation = 900000.0;
	Params.SeaLevel = 0.14;

	// Land, found rather than assumed. Most of this planet is ocean and the
	// near-field band is only interesting where there is ground under it.
	TArray<FVector3d> Land;
	for (int32 Step = 0; Step < 4000 && Land.Num() < 200; ++Step)
	{
		const double Latitude = -80.0 + (Step % 80) * 2.0;
		const double Longitude = (Step / 80) * 3.7;
		const double Lat = FMath::DegreesToRadians(Latitude);
		const double Lon = FMath::DegreesToRadians(Longitude);
		const FVector3d Point(
			FMath::Cos(Lat) * FMath::Cos(Lon),
			FMath::Cos(Lat) * FMath::Sin(Lon),
			FMath::Sin(Lat));
		if (LedgerTerrain::Elevation(Point, Params) / 100.0 > 50.0)
		{
			Land.Add(Point.GetSafeNormal());
		}
	}
	if (!TestTrue(TEXT("found land to measure"), Land.Num() > 50))
	{
		return false;
	}

	// The finest four depths, which are the ones that exist near a standing
	// camera and therefore the ones whose transitions are seen up close.
	double WorstStep = 0.0;
	int32 WorstBetween = 0;
	double WorstTotal = 0.0;

	for (int32 Depth = 15; Depth < 18; ++Depth)
	{
		const double Fine = SpacingAt(Depth + 1);
		const double Coarse = SpacingAt(Depth);
		for (const FVector3d& Point : Land)
		{
			const double Step = FMath::Abs(
				LedgerTerrain::Elevation(Point, Params, Fine)
				- LedgerTerrain::Elevation(Point, Params, Coarse)) / 100.0;
			if (Step > WorstStep)
			{
				WorstStep = Step;
				WorstBetween = Depth;
			}
		}
	}

	// And the whole band, from the coarsest depth that carries none of it to
	// the finest that carries all of it. This is what a camera walking in from
	// far away accumulates, and it is allowed to be larger than one step.
	for (const FVector3d& Point : Land)
	{
		WorstTotal = FMath::Max(WorstTotal, FMath::Abs(
			LedgerTerrain::Elevation(Point, Params, SpacingAt(18))
			- LedgerTerrain::Elevation(Point, Params, SpacingAt(14))) / 100.0);
	}

	AddInfo(FString::Printf(
		TEXT("worst single-depth step %.3f m (between depth %d and %d); "
		     "whole band 14 -> 18 is %.3f m"),
		WorstStep, WorstBetween, WorstBetween + 1, WorstTotal));

	// **A quarter of a metre.**
	//
	// The band is worth about 1.5 m in total. Fading it as one unit made a
	// single ring worth up to half of that, which is what the ground was seen
	// to do. Per octave, the largest single step is one octave's amplitude,
	// and the coarsest octave is the biggest of them -- so this bound is set
	// where a morph over a whole node's collapse can absorb it.
	TestTrue(*FString::Printf(
		TEXT("adjacent depths agree to 0.25 m (worst %.3f m)"), WorstStep),
		WorstStep <= 0.25);

	// The band is still there. A fade so aggressive that nothing survives at
	// any depth would pass the test above and delete the feature.
	TestTrue(*FString::Printf(
		TEXT("the band still exists (whole band %.3f m)"), WorstTotal),
		WorstTotal >= 0.15);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
