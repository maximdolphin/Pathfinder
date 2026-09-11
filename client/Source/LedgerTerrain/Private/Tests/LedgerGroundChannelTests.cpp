// T053: the ground a vertex is drawn with does not depend on which patch it is in.
//
// The palette put three biomes on each 600 km cell of a cube face, and where two
// cells chose differently a vertex on their shared edge was drawn from one set
// of three on one side and another set on the other: a hard line across the
// ground. With a channel per biome there is no palette for two patches to
// disagree about, so the shared edge of two patches in different cells has to
// carry the same ground on both sides. This finds such pairs on land and checks
// that it does.

#include "LedgerBiome.h"
#include "LedgerPatchGenerator.h"
#include "LedgerPlanet.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerGroundChannelsAgreeAcrossPaletteCells,
	"Ledger.Terrain.GroundChannelsAgreeAcrossPaletteCells",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerGroundChannelsAgreeAcrossPaletteCells::RunTest(const FString&)
{
	TArray<FString> Errors;
	const TSharedPtr<const TArray<FLedgerBiome>> Biomes =
		MakeShared<const TArray<FLedgerBiome>>(LedgerBiomes::Load(LedgerBiomes::DefaultDirectory(), Errors));
	if (!TestTrue(TEXT("the biome files load"), Biomes->Num() >= 3))
	{
		return false;
	}

	FLedgerTerrainParams Params;
	Params.Seed = 1337;
	Params.Radius = 637100000.0;
	Params.MaxElevation = 900000.0;
	Params.SeaLevel = 0.14;

	constexpr int32 Side = 65;
	constexpr double WorldSize = 244000.0; // 2.4 km: coarse enough to find land pairs quickly
	const double Extent = WorldSize / (Params.Radius * 2.0);

	auto Fill = [&](FLedgerPatchJob& Job, ELedgerCubeFace Face, double U, double V, uint64 Key)
	{
		Job.Key = Key;
		Job.Face = Face;
		Job.Params = Params;
		Job.U = U;
		Job.V = V;
		Job.Side = Side;
		Job.Biomes = Biomes;
		Job.WorldSize = WorldSize;
		Job.Extent = Extent;
		const FVector3d Direction = LedgerTerrain::CubeToSphere(
			LedgerTerrain::FaceToCube(Face, U + Extent * 0.5, V + Extent * 0.5));
		Job.Centre = Direction * (Params.Radius + LedgerTerrain::Elevation(Direction, Params));
	};

	// Palette cells are sixteenths of a face, so U = 0.5 is a boundary between
	// two of them: one patch just left of it, one just right.
	int32 Pairs = 0;
	int32 DifferentPalettes = 0;
	double Worst = 0.0;
	uint64 Key = 0x6C0D0A7E00000000ull;
	for (int32 Face = 0; Face < static_cast<int32>(ELedgerCubeFace::Count) && DifferentPalettes < 3; ++Face)
	{
		for (int32 Step = 0; Step < 32 && DifferentPalettes < 3; ++Step)
		{
			const double V = 0.03 + Step * 0.03;
			const ELedgerCubeFace CubeFace = static_cast<ELedgerCubeFace>(Face);
			const FVector3d Probe = LedgerTerrain::CubeToSphere(LedgerTerrain::FaceToCube(CubeFace, 0.5, V));
			if (LedgerTerrain::Elevation(Probe, Params) <= 5000.0)
			{
				continue; // the sea has no ground to disagree about
			}

			FLedgerPatchJob Left;
			FLedgerPatchJob Right;
			Fill(Left, CubeFace, 0.5 - Extent, V, ++Key);
			Fill(Right, CubeFace, 0.5, V, ++Key);
			LedgerGeneratePatch(Left);
			LedgerGeneratePatch(Right);
			if (Left.Colors.Num() < Side * Side || Right.Colors.Num() < Side * Side
				|| Left.GroundWeightsB.Num() < Side * Side || Right.GroundWeightsB.Num() < Side * Side)
			{
				continue;
			}
			++Pairs;
			const bool bDifferent = FMemory::Memcmp(Left.Palette.Slots, Right.Palette.Slots, sizeof(Left.Palette.Slots)) != 0;
			DifferentPalettes += bDifferent ? 1 : 0;
			if (!bDifferent)
			{
				continue;
			}

			// The left patch's right column against the right patch's left one.
			for (int32 Y = 0; Y < Side; ++Y)
			{
				const int32 A = Y * Side + Side - 1;
				const int32 B = Y * Side;
				const FColor& CA = Left.Colors[A];
				const FColor& CB = Right.Colors[B];
				Worst = FMath::Max(Worst, FMath::Abs(CA.R - CB.R) / 255.0);
				Worst = FMath::Max(Worst, FMath::Abs(CA.G - CB.G) / 255.0);
				Worst = FMath::Max(Worst, FMath::Abs(CA.B - CB.B) / 255.0);
				Worst = FMath::Max(Worst, FMath::Abs(Left.GroundWeightsB[A].X - Right.GroundWeightsB[B].X));
				Worst = FMath::Max(Worst, FMath::Abs(Left.GroundWeightsB[A].Y - Right.GroundWeightsB[B].Y));
				Worst = FMath::Max(Worst, FMath::Abs(Left.GroundWeightsC[A].X - Right.GroundWeightsC[B].X));
				Worst = FMath::Max(Worst, FMath::Abs(Left.GroundWeightsC[A].Y - Right.GroundWeightsC[B].Y));
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d land pairs across a palette cell boundary, %d with different palettes; "
		"the worst disagreement on a shared edge is %.4f of a weight"), Pairs, DifferentPalettes, Worst));
	TestTrue(TEXT("pairs of patches whose palettes differ were found to check"), DifferentPalettes > 0);
	// A byte of rounding and a little interpolation, and nothing like the
	// whole-weight jump the palette made.
	TestTrue(TEXT("the shared edge carries the same ground on both sides"), Worst <= 0.02);
	return true;
}

#endif
