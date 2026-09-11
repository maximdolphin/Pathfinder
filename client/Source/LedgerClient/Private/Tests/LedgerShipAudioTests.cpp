// The engine a player hears. M5P, T442.
//
// The first playtest heard forward thrust as static: a raw saw, a pump hiss
// near 1 kHz rising with the reactor's heat, and gains that stepped once a
// frame. These run the generator with no audio device and measure what it
// makes, so "it sounds like an engine" has a number under it.

#include "LedgerShipAudio.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 Rate = 48000;

	/// Renders the generator the way the mixer does, in its own block size.
	void Render(FLedgerShipToneGenerator& Generator, TArray<float>& Out, int32 From, int32 Count)
	{
		for (int32 Offset = 0; Offset < Count; Offset += 1024)
		{
			Generator.OnGenerateAudio(Out.GetData() + From + Offset, FMath::Min(1024, Count - Offset));
		}
	}

	/// Share of the energy in a window below a frequency, by a plain DFT.
	double ShareBelow(const TArray<float>& Samples, int32 From, int32 Count, double Hz)
	{
		double Below = 0.0;
		double Total = 0.0;
		for (int32 Bin = 1; Bin < Count / 2; ++Bin)
		{
			const double W = UE_DOUBLE_TWO_PI * Bin / Count;
			double Re = 0.0;
			double Im = 0.0;
			for (int32 N = 0; N < Count; ++N)
			{
				Re += Samples[From + N] * FMath::Cos(W * N);
				Im -= Samples[From + N] * FMath::Sin(W * N);
			}
			const double Power = Re * Re + Im * Im;
			Total += Power;
			Below += static_cast<double>(Bin) * Rate / Count < Hz ? Power : 0.0;
		}
		return Total > 0.0 ? Below / Total : 0.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipAudioIsARumble,
	"Ledger.ShipAudio.FullThrottleIsARumbleNotAHiss",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipAudioIsARumble::RunTest(const FString&)
{
	// Full throttle with the reactor hot: the loudest the engine and the pumps get.
	FLedgerShipToneGenerator Generator(Rate);
	Generator.EngineHz.store(115.0f);
	Generator.EngineGain.store(0.35f);
	Generator.PumpGain.store(0.3f);
	TArray<float> Out;
	Out.SetNumZeroed(Rate);
	Render(Generator, Out, 0, Out.Num());

	// Past the glide, a window of about 85 ms.
	const double Share = ShareBelow(Out, Rate / 2, 4096, 400.0);
	AddInfo(FString::Printf(TEXT("%.1f%% of the energy is below 400 Hz"), Share * 100.0));
	TestTrue(TEXT("most of the energy is below 400 Hz"), Share > 0.8);

	float Peak = 0.0f;
	for (const float Value : Out)
	{
		Peak = FMath::Max(Peak, FMath::Abs(Value));
	}
	TestTrue(TEXT("it is audible"), Peak > 0.05f);
	TestTrue(TEXT("it does not clip"), Peak < 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipAudioGlides,
	"Ledger.ShipAudio.LevelsGlideRatherThanStep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipAudioGlides::RunTest(const FString&)
{
	// Silence, then full throttle asked for between two blocks -- what the game
	// thread does when the stick goes forward.
	FLedgerShipToneGenerator Generator(Rate);
	Generator.EngineHz.store(115.0f);
	TArray<float> Out;
	Out.SetNumZeroed(8192);
	Render(Generator, Out, 0, 4096);
	Generator.EngineGain.store(0.35f);
	Render(Generator, Out, 4096, 4096);

	float WorstJump = 0.0f;
	for (int32 Index = 4096; Index < Out.Num(); ++Index)
	{
		WorstJump = FMath::Max(WorstJump, FMath::Abs(Out[Index] - Out[Index - 1]));
	}
	AddInfo(FString::Printf(TEXT("worst sample-to-sample change %.4f"), WorstJump));
	// A stepped gain lands the whole note at once: a jump of tenths. Glided,
	// the note's own slope is all there is.
	TestTrue(TEXT("no step when the level changes"), WorstJump < 0.05f);
	return true;
}

#endif
