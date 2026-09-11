// What T057's acceptance asks of the scatter, minus the frame rate.
//
//   Ten thousand visible instances at 60 fps, identical placement across runs,
//   and nothing floating or half-buried.
//
// The count and the frame rate are measured by the flight, which is the only
// thing that can measure them. The other two are properties of the placement
// and belong here.

#include "LedgerScatter.h"

#include "LedgerBiome.h"
#include "LedgerPatchGenerator.h"
#include "LedgerPlanet.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FLedgerTerrainParams ScatterPlanet()
	{
		FLedgerTerrainParams Params;
		Params.Seed = 1337;
		Params.Radius = 637100000.0;
		Params.MaxElevation = 900000.0;
		Params.SeaLevel = 0.14;
		return Params;
	}

	/// A patch small enough to be scattered, over a piece of land.
	///
	/// The face and the coordinates are not arbitrary: most of this planet is
	/// under water and a patch over the sea scatters nothing, which would make
	/// every test below pass by having nothing to check.
	void FillJob(FLedgerPatchJob& Job, TSharedPtr<const TArray<FLedgerBiome>> Biomes)
	{
		Job.Key = 0x5CA77E12345ull;
		Job.Face = ELedgerCubeFace::PositiveZ;
		Job.Params = ScatterPlanet();

		// The roughest land on the face, found rather than assumed.
		//
		// Two things this fixture got wrong in a row, and both produced a green
		// test. First it picked the middle of a face, which on a planet that is
		// 71% water was the ocean: nothing scattered and nothing was checked.
		// Then it picked the first land it found, which was flat -- and the
		// gap between an instance and a bilinear interpolation of flat ground
		// is exactly zero, so "nothing floats" passed by measuring a plain.
		//
		// The interpolation error this is about is proportional to curvature,
		// so the patch to test on is the one with the most relief in it.
		const double SampleSpan = 0.0002;
		double RoughestFound = -1.0;
		Job.U = 0.5;
		Job.V = 0.5;
		for (int32 Step = 0; Step < 900; ++Step)
		{
			const double TryU = 0.02 + (Step % 30) * 0.032;
			const double TryV = 0.02 + (Step / 30) * 0.032;

			double Lowest = TNumericLimits<double>::Max();
			double Highest = -TNumericLimits<double>::Max();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const FVector3d Candidate = LedgerTerrain::CubeToSphere(
					LedgerTerrain::FaceToCube(Job.Face,
						TryU + (Corner & 1) * SampleSpan,
						TryV + ((Corner >> 1) & 1) * SampleSpan));
				const double Here = LedgerTerrain::Elevation(Candidate, Job.Params);
				Lowest = FMath::Min(Lowest, Here);
				Highest = FMath::Max(Highest, Here);
			}

			if (Lowest > 5000.0 && Highest - Lowest > RoughestFound)
			{
				RoughestFound = Highest - Lowest;
				Job.U = TryU;
				Job.V = TryV;
			}
		}
		Job.Side = 65;
		Job.Biomes = Biomes;
		Job.bWithCollision = true;

		// A patch 305 m across, which is what the finest LOD is on this planet:
		// a cube face edge of 10,008 km over 2^15.
		Job.WorldSize = 30500.0;
		Job.Extent = Job.WorldSize / (Job.Params.Radius * 2.0);

		const FVector3d Direction = LedgerTerrain::CubeToSphere(
			LedgerTerrain::FaceToCube(Job.Face, Job.U + Job.Extent * 0.5, Job.V + Job.Extent * 0.5));
		Job.Centre = Direction
			* (Job.Params.Radius + LedgerTerrain::Elevation(Direction, Job.Params));
	}

	/// A biome set that scatters everywhere, so the tests are about placement
	/// rather than about which biome the test patch happens to sit in.
	TSharedPtr<const TArray<FLedgerBiome>> DenseBiomes()
	{
		TSharedPtr<TArray<FLedgerBiome>> Biomes = MakeShared<TArray<FLedgerBiome>>();
		FLedgerBiome Everywhere;
		Everywhere.Name = TEXT("Test");
		Everywhere.TemperatureC = 10.0;
		Everywhere.TemperatureToleranceC = 1000.0;
		Everywhere.Moisture = 0.5;
		Everywhere.MoistureTolerance = 1000.0;
		Everywhere.ScatterDensity = 1.0;
		Biomes->Add(Everywhere);
		return Biomes;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerScatterIsDeterministic,
	"Ledger.Scatter.SamePatchSamePlacement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerScatterIsDeterministic::RunTest(const FString&)
{
	const TSharedPtr<const TArray<FLedgerBiome>> Biomes = DenseBiomes();

	FLedgerPatchJob First;
	FLedgerPatchJob Second;
	FillJob(First, Biomes);
	FillJob(Second, Biomes);

	LedgerScatter::ScatterPatch(First);
	LedgerScatter::ScatterPatch(Second);

	TestTrue(TEXT("the patch scattered something"), First.Scatter.Num() > 0);
	if (!TestEqual(TEXT("instance count"), Second.Scatter.Num(), First.Scatter.Num()))
	{
		return false;
	}

	// Bit-for-bit, not nearly. Placement is a pure function of the patch key
	// and a cell index, so anything that makes two runs differ at all -- a
	// stream, a clock, an iteration order -- makes them differ visibly, and a
	// tolerance here would hide exactly that.
	for (int32 Index = 0; Index < First.Scatter.Num(); ++Index)
	{
		const FLedgerScatterInstance& A = First.Scatter[Index];
		const FLedgerScatterInstance& B = Second.Scatter[Index];
		if (A.Position != B.Position || A.Scale != B.Scale || A.Variant != B.Variant)
		{
			AddError(FString::Printf(TEXT("instance %d differs between runs"), Index));
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerScatterNothingFloats,
	"Ledger.Scatter.NothingFloatsOrIsHalfBuried",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerScatterNothingFloats::RunTest(const FString&)
{
	// Two halves, because "nothing floats" is two claims.
	//
	// **Not by locating each instance on the mesh.** The obvious test -- take
	// an instance, ask which grid cell it is in, interpolate -- needs to invert
	// the projection, and `DirectionToFace` inverts `FaceToCube` but not
	// `CubeToSphere`'s area-evening warp. Written that way, every instance fell
	// outside [0,1] and was skipped, and the test reported a worst gap of
	// exactly 0.000 m over four hundred instances it never looked at. The
	// hydrology lattice was wrong the same way, on the same day.
	//
	// So: the placement is at the exact surface (checked directly), and the
	// mesh is within a tolerance of the exact surface everywhere on the patch
	// (checked over the grid, which needs no inverse). Together those bound how
	// far an instance can be from the ground under it.
	const TSharedPtr<const TArray<FLedgerBiome>> Biomes = DenseBiomes();
	FLedgerPatchJob Job;
	FillJob(Job, Biomes);
	LedgerGeneratePatch(Job);

	if (Job.Scatter.Num() == 0)
	{
		AddError(TEXT("nothing was scattered, so nothing was checked"));
		return false;
	}

	// ---- how far the mesh wanders from the height function ---------------
	//
	// Sampled at cell centres, where a bilinear interpolation is furthest from
	// the function it interpolates. This is a property of the terrain's grid
	// resolution rather than of the scatter, and it is the size of the problem.
	const int32 Side = Job.Side | 1;
	double WorstMesh = 0.0;
	for (int32 Y = 0; Y + 1 < Side; ++Y)
	{
		for (int32 X = 0; X + 1 < Side; ++X)
		{
			const double LocalU = (X + 0.5) / (Side - 1);
			const double LocalV = (Y + 0.5) / (Side - 1);
			const FVector3d Direction = LedgerTerrain::CubeToSphere(
				LedgerTerrain::FaceToCube(Job.Face,
					Job.U + LocalU * Job.Extent, Job.V + LocalV * Job.Extent));
			const double Exact = Job.Params.Radius
				+ LedgerTerrain::Elevation(Direction, Job.Params);

			auto Corner = [&Job, Side](int32 CornerX, int32 CornerY)
			{
				return (FVector3d(Job.Vertices[CornerY * Side + CornerX]) + Job.Centre).Length();
			};
			const double MeshRadius = 0.25 * (Corner(X, Y) + Corner(X + 1, Y)
				+ Corner(X, Y + 1) + Corner(X + 1, Y + 1));
			WorstMesh = FMath::Max(WorstMesh, FMath::Abs(Exact - MeshRadius) / 100.0);
		}
	}

	// ---- and which surface the scatter followed --------------------------
	//
	// The same patch scattered twice: once with its mesh available and once
	// without. Without one there is nothing to follow but the height function,
	// so the difference between the two placements is exactly the correction
	// this test exists to check happened.
	FLedgerPatchJob OnFunction;
	FillJob(OnFunction, Biomes);
	LedgerScatter::ScatterPatch(OnFunction);

	// **The mesh may take cells away, never add them.** Placement also rejects
	// on the drawn mesh's own slope (T437: stones stood on 45-60 degree faces
	// the function a cell away called gentle), and a patch with no mesh has no
	// such slope to reject on. So the mesh run is a subset of the function run,
	// and the corrections are measured on the cells both kept -- paired by
	// direction, which the height does not change, rather than by index, which
	// every rejected cell shifts.
	if (Job.Scatter.Num() > OnFunction.Scatter.Num())
	{
		AddError(FString::Printf(
			TEXT("the mesh accepted cells the function did not: %d with a mesh, %d without"),
			Job.Scatter.Num(), OnFunction.Scatter.Num()));
		return false;
	}

	double WorstCorrection = 0.0;
	int32 Paired = 0;
	for (const FLedgerScatterInstance& OnMesh : Job.Scatter)
	{
		const FVector3d MeshAt = FVector3d(OnMesh.Position) + Job.Centre;
		for (const FLedgerScatterInstance& OnField : OnFunction.Scatter)
		{
			const FVector3d FieldAt = FVector3d(OnField.Position) + OnFunction.Centre;
			if (FVector3d::DotProduct(MeshAt.GetSafeNormal(), FieldAt.GetSafeNormal()) > 1.0 - 1e-12)
			{
				WorstCorrection = FMath::Max(WorstCorrection, (MeshAt - FieldAt).Length() / 100.0);
				++Paired;
				break;
			}
		}
	}
	TestEqual(TEXT("every cell the mesh kept is one the function kept too"), Paired, Job.Scatter.Num());

	AddInfo(FString::Printf(
		TEXT("%d instances; mesh wanders %.3f m from the height function, "
		     "and the scatter moved by up to %.3f m to follow it"),
		Job.Scatter.Num(), WorstMesh, WorstCorrection));

	TestTrue(TEXT("the patch has relief in it, so this measured something"),
		WorstMesh > 0.05);

	// The correction is real: the scatter is on the mesh, not on the function.
	// If somebody puts the exact elevation back, this is exactly zero.
	//
	// Five centimetres, not a fraction of the wander. The wander is the worst
	// case over four thousand cell centres and the corrections are at four
	// hundred jittered points, so the two are not the same statistic and
	// relating them was a threshold picked to feel right -- it wanted 0.611 m
	// and got 0.518 m, which said nothing about the code.
	TestTrue(FString::Printf(
		TEXT("the scatter follows the mesh (moved %.3f m against a %.3f m wander)"),
		WorstCorrection, WorstMesh), WorstCorrection > 0.05);

	// And it does not overshoot: following the mesh cannot move an instance
	// further than the mesh is from the function in the first place.
	TestTrue(FString::Printf(TEXT("and does not overshoot (%.3f m)"), WorstCorrection),
		WorstCorrection <= WorstMesh + 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerScatterStaysOffTheSeaAndTheCliffs,
	"Ledger.Scatter.NothingInTheSeaOrOnACliff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerScatterStaysOffTheSeaAndTheCliffs::RunTest(const FString&)
{
	const TSharedPtr<const TArray<FLedgerBiome>> Biomes = DenseBiomes();
	FLedgerPatchJob Job;
	FillJob(Job, Biomes);
	LedgerScatter::ScatterPatch(Job);

	if (Job.Scatter.Num() == 0)
	{
		AddError(TEXT("nothing was scattered, so nothing was checked"));
		return false;
	}

	for (const FLedgerScatterInstance& Instance : Job.Scatter)
	{
		const FVector3d World = FVector3d(Instance.Position) + Job.Centre;
		const FVector3d Direction = World.GetSafeNormal();
		const double Elevation = LedgerTerrain::Elevation(Direction, Job.Params);
		if (Elevation <= 0.0)
		{
			AddError(TEXT("an instance is below the waterline"));
			return false;
		}

		// Upright relative to the ground, not relative to the planet. The
		// rotation is built from the surface normal, so its Z axis must be
		// within the slope limit of the radial -- anything further out means
		// the normal estimate has come apart.
		const FVector3f Up = Instance.Rotation.GetAxisZ();
		const double Tilt = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector3d::DotProduct(FVector3d(Up), Direction), -1.0, 1.0)));
		if (Tilt > LedgerScatter::MaxSlopeDegrees + 5.0)
		{
			AddError(FString::Printf(TEXT("an instance leans %.1f degrees"), Tilt));
			return false;
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
