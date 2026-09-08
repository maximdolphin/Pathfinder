#include "LedgerPerf.h"

#include "DynamicRHI.h"
#include "Engine/World.h"
#include "LedgerLog.h"
#include "Misc/FileHelper.h"
#include "RenderTimer.h"

namespace
{
	/// Percentile from a sorted array. Nearest-rank, because interpolating
	/// between two frame times invents a frame that did not happen.
	double Percentile(const TArray<double>& Sorted, double Fraction)
	{
		if (Sorted.Num() == 0)
		{
			return 0.0;
		}
		const int32 Index = FMath::Clamp(
			FMath::CeilToInt(Fraction * Sorted.Num()) - 1, 0, Sorted.Num() - 1);
		return Sorted[Index];
	}

	FString Bar(double Milliseconds, double Budget)
	{
		// One column per millisecond up to twice the budget, so the eye lands on
		// the phase that is over rather than on the phase with the longest name.
		const int32 Columns = FMath::Clamp(
			FMath::RoundToInt(Milliseconds), 0, FMath::RoundToInt(Budget * 2.0));
		return FString::ChrN(Columns, TEXT('#'));
	}
}

void ULedgerPerfSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BeginPhase(TEXT("startup"));
}

bool ULedgerPerfSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerPerfSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerPerfSubsystem, STATGROUP_Tickables);
}

void ULedgerPerfSubsystem::BeginPhase(const FString& Name)
{
	FLedgerPerfPhase Phase;
	Phase.Name = Name;
	Phase.StartedAt = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	Phase.FrameMs.Reserve(4096);
	Phases.Add(MoveTemp(Phase));
}

void ULedgerPerfSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Phases.Num() == 0 || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const double Milliseconds = static_cast<double>(DeltaSeconds) * 1000.0;

	// The engine's own counters, in cycles. GGPUFrameTime lags by a frame or
	// two by construction; over a phase of hundreds of frames that does not
	// matter, and the alternative is a fence every frame.
	FLedgerPerfPhase& Current = Phases.Last();
	Current.FrameMs.Add(Milliseconds);
	Current.GameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
	Current.RenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
	Current.GpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));

	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;
	if (Milliseconds >= StallMs && Now >= WarmupSeconds)
	{
		UE_LOG(LogLedger, Warning, TEXT("stall: %.1f ms at t=%.1fs during %s"),
			Milliseconds, Now, *Phases.Last().Name);
	}
}

bool ULedgerPerfSubsystem::WriteReport(const FString& Path) const
{
	TArray<double> All;
	TArray<TPair<double, FString>> Worst;

	FString Body;
	Body += TEXT("Frame times for the scripted flight.\n");
	Body += FString::Printf(TEXT("budget %.1f ms (%.0f fps), warm-up %.0f s excluded from the verdict\n\n"),
		BudgetMs, 1000.0 / BudgetMs, WarmupSeconds);

	Body += TEXT("phase              frames    mean   median     p95     p99     max   over    game  render     gpu\n");

	for (const FLedgerPerfPhase& Phase : Phases)
	{
		if (Phase.FrameMs.Num() == 0)
		{
			continue;
		}

		TArray<double> Sorted = Phase.FrameMs;
		Sorted.Sort();

		double Sum = 0.0;
		int32 Over = 0;
		for (const double Milliseconds : Phase.FrameMs)
		{
			Sum += Milliseconds;
			if (Milliseconds > BudgetMs)
			{
				++Over;
			}
		}

		const bool bCounts = Phase.StartedAt >= WarmupSeconds;
		if (bCounts)
		{
			All.Append(Phase.FrameMs);
			Worst.Add(TPair<double, FString>(Sorted.Last(), Phase.Name));
		}

		auto Average = [](const TArray<double>& Values) -> double
		{
			if (Values.Num() == 0)
			{
				return 0.0;
			}
			double Total = 0.0;
			for (const double Value : Values)
			{
				Total += Value;
			}
			return Total / Values.Num();
		};

		Body += FString::Printf(
			TEXT("%-18s %6d  %6.1f  %6.1f  %6.1f  %6.1f  %6.1f  %4.0f%%  %6.1f  %6.1f  %6.1f%s\n"),
			*Phase.Name, Phase.FrameMs.Num(),
			Sum / Phase.FrameMs.Num(),
			Percentile(Sorted, 0.50),
			Percentile(Sorted, 0.95),
			Percentile(Sorted, 0.99),
			Sorted.Last(),
			100.0 * Over / Phase.FrameMs.Num(),
			Average(Phase.GameMs), Average(Phase.RenderMs), Average(Phase.GpuMs),
			bCounts ? TEXT("") : TEXT("   (warm-up)"));
	}

	TArray<double> Sorted = All;
	Sorted.Sort();

	double Sum = 0.0;
	int32 Over = 0;
	for (const double Milliseconds : All)
	{
		Sum += Milliseconds;
		if (Milliseconds > BudgetMs)
		{
			++Over;
		}
	}

	const double Mean = All.Num() > 0 ? Sum / All.Num() : 0.0;
	const double P99 = Percentile(Sorted, 0.99);
	const double Max = Sorted.Num() > 0 ? Sorted.Last() : 0.0;

	Body += TEXT("\n");
	Body += FString::Printf(TEXT("overall  %d frames, mean %.1f ms (%.0f fps), p99 %.1f ms, max %.1f ms\n"),
		All.Num(), Mean, Mean > 0.0 ? 1000.0 / Mean : 0.0, P99, Max);
	Body += FString::Printf(TEXT("         %.1f%% of frames over budget\n\n"),
		All.Num() > 0 ? 100.0 * Over / All.Num() : 0.0);

	// The profile, drawn. A table of numbers hides the shape and the shape is
	// the diagnosis: a plateau is a cost, a spike is a stall.
	Body += TEXT("p99 per phase, one column per millisecond:\n");
	for (const FLedgerPerfPhase& Phase : Phases)
	{
		if (Phase.FrameMs.Num() == 0)
		{
			continue;
		}
		TArray<double> PhaseSorted = Phase.FrameMs;
		PhaseSorted.Sort();
		const double Value = Percentile(PhaseSorted, 0.99);
		Body += FString::Printf(TEXT("  %-18s %6.1f |%s\n"), *Phase.Name, Value, *Bar(Value, BudgetMs));
	}

	// The verdict is the milestone's acceptance line, checked here rather than
	// left to whoever reads the log.
	const bool bPassed = All.Num() > 0 && P99 <= BudgetMs;
	Body += FString::Printf(TEXT("\nVERDICT: %s (p99 %.1f ms against a %.1f ms budget)\n"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"), P99, BudgetMs);

	FFileHelper::SaveStringToFile(Body, *Path);

	UE_LOG(LogLedger, Log, TEXT("performance -> %s"), *Path);
	UE_LOG(LogLedger, Log, TEXT("  mean %.1f ms (%.0f fps), p99 %.1f ms, max %.1f ms, %s"),
		Mean, Mean > 0.0 ? 1000.0 / Mean : 0.0, P99, Max,
		bPassed ? TEXT("PASS") : TEXT("FAIL"));

	return bPassed;
}
