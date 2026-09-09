// The cube-sphere projection, and the inverse that was missing.
//
// Three functions here have to agree with each other, and for a long time two
// of them did not: `DirectionToFace` inverts `FaceToCube`, `CubeToSphere` warps
// the cube onto the sphere, and nothing inverted the warp. Everything that
// asked "which patch is under this point" used the pair that does not compose.

#include "LedgerTerrainMath.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerProjectionSphereRoundTrips,
	"Ledger.Projection.SphereToFaceRoundTrips",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerProjectionSphereRoundTrips::RunTest(const FString&)
{
	double Worst = 0.0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y <= 20; ++Y)
		{
			for (int32 X = 0; X <= 20; ++X)
			{
				const double U = X / 20.0;
				const double V = Y / 20.0;
				const FVector3d Point = LedgerTerrain::CubeToSphere(
					LedgerTerrain::FaceToCube(static_cast<ELedgerCubeFace>(Face), U, V));

				ELedgerCubeFace Back = static_cast<ELedgerCubeFace>(Face);
				double BackU = 0.0;
				double BackV = 0.0;
				LedgerTerrain::SphereToFace(Point, Back, BackU, BackV);

				// The face can legitimately differ on a shared edge or corner,
				// where the point belongs to two faces or three.
				const bool bOnSeam = U <= 0.0 || U >= 1.0 || V <= 0.0 || V >= 1.0;
				if (Back != static_cast<ELedgerCubeFace>(Face))
				{
					if (!bOnSeam)
					{
						AddError(FString::Printf(
							TEXT("face %d (%.2f, %.2f) came back as face %d"),
							Face, U, V, static_cast<int32>(Back)));
						return false;
					}
					continue;
				}
				Worst = FMath::Max(Worst,
					FMath::Max(FMath::Abs(BackU - U), FMath::Abs(BackV - V)));
			}
		}
	}

	AddInfo(FString::Printf(TEXT("worst round-trip error: %.3e of a face"), Worst));
	TestTrue(FString::Printf(TEXT("round trip is exact (%.3e)"), Worst), Worst < 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerProjectionGnomonicIsNotTheInverse,
	"Ledger.Projection.DirectionToFaceIsNotTheSphereInverse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerProjectionGnomonicIsNotTheInverse::RunTest(const FString&)
{
	// The bug, pinned as a number so that nobody quietly swaps the two back.
	//
	// It is not a rounding error and it is not small: it peaks at the quarter
	// points of a face, where it is 0.0657 of a face -- 657 km on a planet
	// whose cube edge is 10,008 km. If this test ever starts reporting a tiny
	// number, `CubeToSphere` has stopped warping and `SphereToFace` can go.
	double Worst = 0.0;
	for (int32 Y = 0; Y <= 20; ++Y)
	{
		for (int32 X = 0; X <= 20; ++X)
		{
			const double U = X / 20.0;
			const double V = Y / 20.0;
			const FVector3d Point = LedgerTerrain::CubeToSphere(
				LedgerTerrain::FaceToCube(ELedgerCubeFace::PositiveX, U, V));

			ELedgerCubeFace Back = ELedgerCubeFace::PositiveX;
			double BackU = 0.0;
			double BackV = 0.0;
			LedgerTerrain::DirectionToFace(Point, Back, BackU, BackV);
			if (Back != ELedgerCubeFace::PositiveX)
			{
				continue;
			}
			Worst = FMath::Max(Worst,
				FMath::Max(FMath::Abs(BackU - U), FMath::Abs(BackV - V)));
		}
	}

	AddInfo(FString::Printf(
		TEXT("DirectionToFace on a sphere point is out by %.4f of a face"), Worst));
	TestTrue(TEXT("and it is a large error, not a rounding one"), Worst > 0.05);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
