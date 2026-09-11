// The engine a player hears. M5P, T442.
//
// The first playtest heard forward thrust as static: a raw saw, a pump hiss
// near 1 kHz rising with the reactor's heat, and gains that stepped once a
// frame. These run the generator with no audio device and measure what it
// makes, so "it sounds like an engine" has a number under it.

#include "LedgerShipAudio.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 Rate = 48000;

	/// A 16-bit mono WAV, because the last clause of T442 is a person listening.
	///
	/// Hand-rolled rather than through the engine's sound asset path: this runs
	/// with no audio device and no world, and forty lines of header is less than
	/// the machinery for importing a USoundWave nobody plays in the game.
	bool WriteWav(const FString& Path, const TArray<float>& Samples, int32 SampleRate)
	{
		const int32 DataBytes = Samples.Num() * 2;
		TArray<uint8> File;
		File.Reserve(44 + DataBytes);
		auto Put32 = [&File](uint32 Value)
		{
			for (int32 Byte = 0; Byte < 4; ++Byte) { File.Add((Value >> (8 * Byte)) & 0xFF); }
		};
		auto Put16 = [&File](uint16 Value)
		{
			for (int32 Byte = 0; Byte < 2; ++Byte) { File.Add((Value >> (8 * Byte)) & 0xFF); }
		};
		auto PutTag = [&File](const char* Tag)
		{
			for (int32 Byte = 0; Byte < 4; ++Byte) { File.Add(static_cast<uint8>(Tag[Byte])); }
		};
		PutTag("RIFF");
		Put32(36 + DataBytes);
		PutTag("WAVE");
		PutTag("fmt ");
		Put32(16);
		Put16(1);
		Put16(1);
		Put32(SampleRate);
		Put32(SampleRate * 2);
		Put16(2);
		Put16(16);
		PutTag("data");
		Put32(DataBytes);
		for (const float Value : Samples)
		{
			const int32 Scaled = FMath::Clamp(FMath::RoundToInt(Value * 32767.0f), -32768, 32767);
			Put16(static_cast<uint16>(static_cast<int16>(Scaled)));
		}
		return FFileHelper::SaveArrayToFile(File, *Path);
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLedgerShipAudioWritesASample,
	"Ledger.ShipAudio.WritesASampleToListenTo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FLedgerShipAudioWritesASample::RunTest(const FString&)
{
	// **The last clause of T442 is the owner hearing a rumble**, and no number
	// stands in for that. This drives the generator the way the ship does --
	// idle, the throttle up over two seconds, held, then closed -- and writes
	// out/engine-voice.wav, so there is something to play rather than a
	// spectrum to take on trust.
	FLedgerShipToneGenerator Generator(Rate);
	TArray<float> Out;
	Out.SetNumZeroed(Rate * 6);
	for (int32 Block = 0; Block < Out.Num(); Block += 1024)
	{
		const double Seconds = static_cast<double>(Block) / Rate;
		const double Level = Seconds < 1.0 ? 0.0
			: Seconds < 3.0 ? (Seconds - 1.0) / 2.0
			: Seconds < 5.0 ? 1.0
			: FMath::Max(0.0, 1.0 - (Seconds - 5.0));
		// The ship's own mapping: 45 Hz at rest, seventy more at full throttle.
		Generator.EngineHz.store(static_cast<float>(45.0 + 70.0 * Level));
		Generator.EngineGain.store(static_cast<float>(0.05 + 0.30 * Level));
		Generator.PumpGain.store(static_cast<float>(0.05 + 0.25 * Level));
		Generator.OnGenerateAudio(Out.GetData() + Block, FMath::Min(1024, Out.Num() - Block));
	}

	float Peak = 0.0f;
	for (const float Value : Out)
	{
		Peak = FMath::Max(Peak, FMath::Abs(Value));
	}
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("engine-voice.wav")));
	const bool bWritten = WriteWav(Path, Out, Rate);
	AddInfo(FString::Printf(TEXT("six seconds of throttle written to %s, peak %.2f"), *Path, Peak));
	TestTrue(TEXT("the sample is written"), bWritten);
	TestTrue(TEXT("and there is something on it"), Peak > 0.05f && Peak < 1.0f);
	return true;
}

#endif
