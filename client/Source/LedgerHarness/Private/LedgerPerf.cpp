#include "LedgerPerf.h"

#include "ContentStreaming.h"
#include "LedgerPatchDisk.h"

#include "DynamicRHI.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "LedgerLog.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "EngineUtils.h"
#include "LedgerPlanet.h"
#include "RenderTimer.h"
#include "UnrealClient.h"

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

	// Skip while a screenshot is outstanding, and one frame after it lands.
	//
	// A fixed count was tried first and was the wrong shape: two frames removed
	// nine stalls of twenty-two and left the coast and underwater captures
	// still in the list, because the request is served whenever the render
	// thread gets to it rather than on the next tick. Choosing a larger number
	// until the list looked clean would have been choosing the answer.
	if (FScreenshotRequest::IsScreenshotRequested())
	{
		FramesToSkip = FMath::Max(FramesToSkip, 1);
	}
	if (FramesToSkip > 0)
	{
		--FramesToSkip;
		LastTickAt = FPlatformTime::Seconds();
		return;
	}

	// Wall clock, not DeltaSeconds.
	//
	// They are the same number in a normal run and they are not the same
	// number under `-useFixedTimeStep`, which is how the capture runs are made
	// reproducible: there DeltaSeconds is exactly 1/60 on every frame however
	// long the frame actually took, so a report built from it says 16.7 ms for
	// a run that was visibly stuttering. The frame-time half of the packaged
	// gate would have passed on any machine at any speed.
	const double TickedAt = FPlatformTime::Seconds();
	const double Milliseconds = LastTickAt > 0.0 ? (TickedAt - LastTickAt) * 1000.0
												 : static_cast<double>(DeltaSeconds) * 1000.0;
	LastTickAt = TickedAt;

	// The engine's own counters, in cycles. GGPUFrameTime lags by a frame or
	// two by construction; over a phase of hundreds of frames that does not
	// matter, and the alternative is a fence every frame.
	FLedgerPerfPhase& Current = Phases.Last();
	Current.FrameMs.Add(Milliseconds);
	Current.GameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
	Current.RenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
	Current.GpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));

	// One planet, found by iteration rather than cached, because the harness
	// deliberately holds no world state -- and this runs once a frame beside a
	// tick that costs milliseconds.
	for (TActorIterator<ALedgerPlanet> It(GetWorld()); It; ++It)
	{
		Current.TerrainMs.Add(It->GetStats().LastTickMs);
		break;
	}

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
	Body += TEXT("\n");
	Body += TEXT("VALID ONLY ON AN IDLE MACHINE. Anything else using the GPU is measured\n");
	Body += TEXT("as part of this. A day went into optimising a cloud pass that costs\n");
	Body += TEXT("0.24 ms, because the profile saying 90 ms was taken through another\n");
	Body += TEXT("game running behind the capture.\n");
	Body += TEXT("\n");
	Body += FString::Printf(TEXT("budget %.1f ms (%.0f fps), warm-up %.0f s excluded from the verdict\n\n"),
		BudgetMs, 1000.0 / BudgetMs, WarmupSeconds);

	Body += TEXT("phase              frames    mean   median     p95     p99     max   over    game  render     gpu terrain\n");

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
			TEXT("%-18s %6d  %6.1f  %6.1f  %6.1f  %6.1f  %6.1f  %4.0f%%  %6.1f  %6.1f  %6.1f  %6.2f%s\n"),
			*Phase.Name, Phase.FrameMs.Num(),
			Sum / Phase.FrameMs.Num(),
			Percentile(Sorted, 0.50),
			Percentile(Sorted, 0.95),
			Percentile(Sorted, 0.99),
			Sorted.Last(),
			100.0 * Over / Phase.FrameMs.Num(),
			Average(Phase.GameMs), Average(Phase.RenderMs), Average(Phase.GpuMs),
			Average(Phase.TerrainMs),
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

	// ---- what the frame time was actually waiting on -----------------------
	//
	// Two things upstream of the renderer decide whether a fast frame is also a
	// good-looking one, and neither shows up in a frame time. Both are cheap to
	// ask and both have been wrong for a whole milestone without anybody
	// noticing, so they go in the report rather than in a console command
	// somebody has to remember to run.
	Body += TEXT("\n---- streaming and caches ----\n\n");

	// The texture pool. A streamer that cannot fit the working set does not
	// fail; it serves a lower mip, everywhere, silently. "Required" over "pool"
	// is the whole diagnosis.
	if (IStreamingManager::Get().IsTextureStreamingEnabled())
	{
		IRenderAssetStreamingManager& Textures =
			IStreamingManager::Get().GetTextureStreamingManager();
		const int64 Pool = Textures.GetPoolSize();
		const int64 Required = Textures.GetRequiredPoolSize();
		Body += FString::Printf(
			TEXT("texture pool   %5.0f MB pool, %5.0f MB required  %s\n"),
			Pool / 1048576.0, Required / 1048576.0,
			Required > Pool ? TEXT("OVER -- the ground is being drawn from low mips")
							: TEXT("fits"));
	}
	else
	{
		Body += TEXT("texture pool   streaming disabled\n");
	}

	// The patch cache on disk. On a second visit to the same ground these
	// should be nearly all hits and no writes; if they are not, generation is
	// repeating work it has already done.
	int32 Hits = 0;
	int32 Misses = 0;
	int32 Writes = 0;
	LedgerPatchDisk::Stats(Hits, Misses, Writes);
	Body += FString::Printf(
		TEXT("patch cache    %5d hits, %5d misses, %5d written  (%.0f%% served from disk)\n"),
		Hits, Misses, Writes,
		Hits + Misses > 0 ? 100.0 * Hits / (Hits + Misses) : 0.0);

	// ---- the regression block, for a script rather than a person ----------
	//
	// T067. Key and value, one per line, no prose: the four failure modes M02's
	// gate names, in a form tools/terrain_regression.py can compare against a
	// reference without parsing a table meant for reading.
	Body += TEXT("\n---- regression metrics ----\n\n");
	if (const UWorld* MetricWorld = GetWorld())
	{
		for (TActorIterator<ALedgerPlanet> It(const_cast<UWorld*>(MetricWorld)); It; ++It)
		{
			const FLedgerTerrainStats& Terrain = It->GetStats();
			Body += FString::Printf(TEXT("holes_worst %d\n"), Terrain.WorstUnfilled);
			Body += FString::Printf(TEXT("cracks_stitch_rejects %d\n"), Terrain.StitchRejects);
			Body += FString::Printf(TEXT("pop_hidden_quads %.3f\n"), Terrain.WorstMorphQuads);
			Body += FString::Printf(TEXT("imbalanced_edges %d\n"), Terrain.ImbalancedEdges);
			break;
		}
	}
	// Whether the transition above is being hidden. The planet cannot know:
	// morphing is a material parameter, and the switch that disables it is
	// read where it is used. Both numbers are reported because they fail
	// for different reasons -- the transition growing is an LOD problem,
	// and morphing being off is a rendering one.
	Body += FString::Printf(TEXT("pop_morph_enabled %d\n"),
		FParse::Param(FCommandLine::Get(), TEXT("breakmorph")) ? 0 : 1);
	Body += FString::Printf(TEXT("budget_p99_ms %.2f\n"), P99);
	Body += FString::Printf(TEXT("budget_max_ms %.2f\n"), Max);
	Body += FString::Printf(TEXT("budget_mean_ms %.2f\n"), Mean);
	// The resolution, in the block, forever.
	//
	// Screen-space error scales with viewport width, so every number above is
	// a function of it. T429 shipped holes at 1080p and was signed off on a
	// 720p run of the same build that reported zero. A performance number
	// without its resolution is not a number.
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		ViewportWidth = FMath::RoundToInt(Size.X);
		ViewportHeight = FMath::RoundToInt(Size.Y);
	}
	Body += FString::Printf(TEXT("viewport_width %d\n"), ViewportWidth);
	Body += FString::Printf(TEXT("viewport_height %d\n"), ViewportHeight);
	Body += FString::Printf(TEXT("frames %d\n"), All.Num());

	FFileHelper::SaveStringToFile(Body, *Path);

	UE_LOG(LogLedger, Log, TEXT("performance -> %s"), *Path);
	UE_LOG(LogLedger, Log, TEXT("  mean %.1f ms (%.0f fps), p99 %.1f ms, max %.1f ms, %s"),
		Mean, Mean > 0.0 ? 1000.0 / Mean : 0.0, P99, Max,
		bPassed ? TEXT("PASS") : TEXT("FAIL"));

	return bPassed;
}
