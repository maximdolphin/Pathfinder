// The disk cache, and the two things T065 asks of it.
//
//   A second visit to the same ground generates nothing and is bit-identical.
//
// Both are checkable without a world: build a patch twice and compare what came
// out, with the cache's own counters saying which run did the work.

#include "LedgerPatchDisk.h"

#include "HAL/FileManager.h"
#include "LedgerPatchGenerator.h"
#include "LedgerPlanet.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/// A patch over land, at the finest LOD.
	///
	/// `Seed` is a parameter because the first test needs ground this machine
	/// has provably never generated before. Deleting the cache directory would
	/// do the same job and would also throw away however many hours of real
	/// cache the last flight built, which is a rude thing for a test to do.
	void FillDiskJob(FLedgerPatchJob& Job, uint64 Key, uint32 Seed = 1337)
	{
		Job.Key = Key;
		Job.Face = ELedgerCubeFace::PositiveZ;
		Job.Params.Seed = Seed;
		Job.Params.Radius = 637100000.0;
		Job.Params.MaxElevation = 900000.0;
		Job.Params.SeaLevel = 0.14;
		Job.Side = 33;
		Job.bWithCollision = false;
		Job.WorldSize = 30500.0;
		Job.Extent = Job.WorldSize / (Job.Params.Radius * 2.0);

		// Land, found rather than assumed -- most of this planet is ocean and a
		// patch over the sea would test the cache on a flat plane.
		Job.U = 0.5;
		Job.V = 0.5;
		for (int32 Step = 0; Step < 400; ++Step)
		{
			const double TryU = 0.02 + (Step % 20) * 0.048;
			const double TryV = 0.02 + (Step / 20) * 0.048;
			const FVector3d Candidate = LedgerTerrain::CubeToSphere(
				LedgerTerrain::FaceToCube(Job.Face, TryU, TryV));
			if (LedgerTerrain::Elevation(Candidate, Job.Params) > 5000.0)
			{
				Job.U = TryU;
				Job.V = TryV;
				break;
			}
		}

		const FVector3d Direction = LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(
			Job.Face, Job.U + Job.Extent * 0.5, Job.V + Job.Extent * 0.5));
		Job.Centre = Direction
			* (Job.Params.Radius + LedgerTerrain::Elevation(Direction, Job.Params));
	}

	/// Everything a patch produced, as one number.
	uint64 Fingerprint(const FLedgerPatchJob& Job)
	{
		uint64 Hash = 1469598103934665603ull;
		auto Feed = [&Hash](const void* Data, int32 Bytes)
		{
			const uint8* Byte = static_cast<const uint8*>(Data);
			for (int32 Index = 0; Index < Bytes; ++Index)
			{
				Hash ^= Byte[Index];
				Hash *= 1099511628211ull;
			}
		};

		Feed(Job.Vertices.GetData(), Job.Vertices.Num() * sizeof(FVector));
		Feed(Job.Triangles.GetData(), Job.Triangles.Num() * sizeof(int32));
		Feed(Job.Normals.GetData(), Job.Normals.Num() * sizeof(FVector));
		Feed(Job.UVs.GetData(), Job.UVs.Num() * sizeof(FVector2D));
		Feed(Job.MorphUVs.GetData(), Job.MorphUVs.Num() * sizeof(FVector2D));
		Feed(Job.Colors.GetData(), Job.Colors.Num() * sizeof(FColor));
		Feed(Job.WaterVertices.GetData(), Job.WaterVertices.Num() * sizeof(FVector));
		Feed(Job.WaterColors.GetData(), Job.WaterColors.Num() * sizeof(FColor));
		return Hash;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerPatchDiskSecondVisitIsFree,
	"Ledger.PatchDisk.SecondVisitGeneratesNothingAndIsIdentical",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerPatchDiskSecondVisitIsFree::RunTest(const FString&)
{
	// A planet nobody has ever generated, so the first visit is a first visit
	// however many times this has run before. The first attempt at this test
	// deleted the cache directory instead and failed on its own leftovers: the
	// delete silently did nothing (that call removes files, not trees), a patch
	// from an earlier session was still sitting there, and the run that was
	// supposed to be cold opened with a cache hit.
	const uint32 Seed = static_cast<uint32>(FDateTime::UtcNow().GetTicks());

	FLedgerPatchJob First;
	FillDiskJob(First, 0xD15C0FFEEull, Seed);

	int32 HitsBefore = 0;
	int32 MissesBefore = 0;
	int32 WritesBefore = 0;
	LedgerPatchDisk::Stats(HitsBefore, MissesBefore, WritesBefore);

	LedgerGeneratePatch(First);
	const uint64 ColdPrint = Fingerprint(First);

	int32 HitsAfterCold = 0;
	int32 MissesAfterCold = 0;
	int32 WritesAfterCold = 0;
	LedgerPatchDisk::Stats(HitsAfterCold, MissesAfterCold, WritesAfterCold);

	if (!TestEqual(TEXT("the first visit missed"), MissesAfterCold - MissesBefore, 1))
	{
		return false;
	}
	if (!TestEqual(TEXT("and wrote what it built"), WritesAfterCold - WritesBefore, 1))
	{
		return false;
	}

	// The same ground again, into a job that knows nothing about the first.
	FLedgerPatchJob Second;
	FillDiskJob(Second, 0xD15C0FFEEull, Seed);
	LedgerGeneratePatch(Second);
	const uint64 WarmPrint = Fingerprint(Second);

	int32 HitsAfterWarm = 0;
	int32 MissesAfterWarm = 0;
	int32 WritesAfterWarm = 0;
	LedgerPatchDisk::Stats(HitsAfterWarm, MissesAfterWarm, WritesAfterWarm);

	TestEqual(TEXT("the second visit hit"), HitsAfterWarm - HitsAfterCold, 1);
	TestEqual(TEXT("and generated nothing to write"),
		WritesAfterWarm - WritesAfterCold, 0);

	// Bit-identical, not nearly. The whole claim is that a cached patch is the
	// patch, and a tolerance here would be a licence for it not to be.
	TestEqual(TEXT("bit-identical"), WarmPrint, ColdPrint);

	// And it really was a patch, not an empty one that trivially matches.
	TestTrue(TEXT("the patch has geometry in it"), Second.Vertices.Num() > 100);
	TestTrue(TEXT("and is not flat"), Second.Elevations.Num() > 100);

	// Tidy up after itself: this planet exists for one run of one test and
	// leaving its entry behind would grow the cache by a patch a run forever.
	IFileManager::Get().Delete(
		*LedgerPatchDisk::PathFor(LedgerPatchDisk::ContentKey(First)), false, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerPatchDiskKeyCoversWhatMatters,
	"Ledger.PatchDisk.ChangingAnythingChangesTheKey",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerPatchDiskKeyCoversWhatMatters::RunTest(const FString&)
{
	// A field missing from the key is a patch served from a cache that no
	// longer describes it, and the failure would look like terrain rather than
	// like a cache bug. Every input that changes what generation produces has
	// to change the address.
	FLedgerPatchJob Base;
	FillDiskJob(Base, 1);
	const uint64 Original = LedgerPatchDisk::ContentKey(Base);

	// Rebuilt rather than copied -- a job carries an atomic and cannot be
	// copied, which is the right thing for a job and merely inconvenient here.
	auto Differs = [this, Original](const TCHAR* What, TFunctionRef<void(FLedgerPatchJob&)> Change)
	{
		FLedgerPatchJob Changed;
		FillDiskJob(Changed, 1);
		Change(Changed);
		TestNotEqual(What, LedgerPatchDisk::ContentKey(Changed), Original);
	};

	Differs(TEXT("the face"), [](FLedgerPatchJob& Job)
		{ Job.Face = ELedgerCubeFace::NegativeY; });
	Differs(TEXT("where on it"), [](FLedgerPatchJob& Job) { Job.U += 0.001; });
	Differs(TEXT("how big"), [](FLedgerPatchJob& Job) { Job.Extent *= 2.0; });
	Differs(TEXT("the grid"), [](FLedgerPatchJob& Job) { Job.Side = 65; });
	Differs(TEXT("a stitched edge"), [](FLedgerPatchJob& Job) { Job.bStitchLeft = true; });
	Differs(TEXT("the seed"), [](FLedgerPatchJob& Job) { Job.Params.Seed = 7; });
	Differs(TEXT("sea level"), [](FLedgerPatchJob& Job) { Job.Params.SeaLevel += 0.01; });
	Differs(TEXT("peak elevation"), [](FLedgerPatchJob& Job) { Job.Params.MaxElevation *= 2.0; });
	Differs(TEXT("the season"), [](FLedgerPatchJob& Job) { Job.SeasonPhase = 0.5; });
	Differs(TEXT("whether it carries collision"),
		[](FLedgerPatchJob& Job) { Job.bWithCollision = true; });

	// And the ground somebody changed.
	Differs(TEXT("an edit to the ground"), [](FLedgerPatchJob& Job)
	{
		FLedgerTerrainEdit Edit;
		Edit.Centre = FVector3d(1.0, 0.0, 0.0);
		Edit.RadiusMetres = 40.0;
		Edit.TargetAltitudeMetres = 100.0;
		Job.Params.Delta = MakeShared<FLedgerTerrainDelta>()->With(Edit);
	});

	// And the biomes, which decide the colours and the scatter.
	Differs(TEXT("a biome"), [](FLedgerPatchJob& Job)
	{
		TSharedRef<TArray<FLedgerBiome>> Biomes = MakeShared<TArray<FLedgerBiome>>();
		FLedgerBiome Biome;
		Biome.Name = TEXT("Test");
		Biome.SurfaceSet = TEXT("dry_sand_xfhsfao");
		Biomes->Add(Biome);
		Job.Biomes = Biomes;
	});
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
