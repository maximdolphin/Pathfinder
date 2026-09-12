// The collision transect: fly low and fast in a straight line, and trace
// downward every single frame.
//
// **A separate mode rather than another step in the scripted flight.** Two
// hundred kilometres at 900 m/s is three and a half minutes, which would more
// than double a run that already takes three. Launch with `-transect` and this
// runs instead of the flight.
//
// The question it answers cannot be answered by looking. Streaming collision
// is cooked near the camera and ahead along the velocity vector, and the
// failure mode is that it does not arrive: a trace that hits nothing, on one
// frame, somewhere in the middle of a long fast run. A ship falls through the
// world for a moment and is back before anybody has finished being surprised.
// Counting the misses is the only honest way to know.

#include "LedgerTransect.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "UnrealClient.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "ShaderCompiler.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/// How high above the ground the ship is held, in centimetres. Fifty metres
	/// by default, stricter than the gate -- M02 names 300 m, which
	/// `-transectaltitude=300` flies: nearer the ground the tree wants finer
	/// patches, and that is where streaming falls behind at speed.
	const double TransectAltitude = []()
	{
		double Metres = 50.0;
		FParse::Value(FCommandLine::Get(), TEXT("transectaltitude="), Metres);
		return FMath::Max(Metres, 1.0) * 100.0;
	}();

	/// Ground speed, cm/s. 900 m/s is the number in M02's gate.
	constexpr double TransectSpeed = 90000.0;

	/// How far, in centimetres.
	constexpr double TransectDistance = 20000000.0; // 200 km

	/// The trace looks this far down. Generous: it is asking whether *any*
	/// collision exists beneath, not measuring a height.
	// Five kilometres, not four hundred metres. The ship is held a fixed height
	// above the exact surface; the ground drawn under a fast ship is often a
	// coarse patch whose surface sits hundreds of metres from that, and a trace
	// that stops 100 m below the exact ground called its collision absent.
	// Whether collision is there is one question; how far it is from the true
	// surface is another, and the report now answers both.
	constexpr double TraceDepth = 500000.0;

	/// Ticks since this fixture last asked for a photograph: the capture stalls
	/// the pipeline for a second, which is the harness's cost and not the
	/// terrain's, so those frames and the two after are left out of the timing.
	int32 GTicksSincePhoto = 1000;
}

bool ULedgerTransect::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULedgerTransect::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!FParse::Param(FCommandLine::Get(), TEXT("transect")))
	{
		return;
	}

	bRunning = true;
	Travelled = 0.0;
	Frames = 0;
	Misses = 0;
	LongestMiss = 0;
	CurrentMiss = 0;
	FCoreUObjectDelegates::GetPostGarbageCollect().AddWeakLambda(this, [this]() { ++GCsSeen; });

	UE_LOG(LogLedger, Log, TEXT("transect: %.0f km at %.0f m/s, %.0f m up, tracing every frame"),
		TransectDistance / 100000.0, TransectSpeed / 100.0, TransectAltitude / 100.0);
}

void ULedgerTransect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// **What this fixture costs the frame it is measuring.** Scoped rather than
	// stopped by hand: Tick leaves by three separate returns, and the frames
	// worth explaining are as likely to leave by an early one as by the end.
	struct FFixtureClock
	{
		double Started = FPlatformTime::Seconds();
		double* Out = nullptr;
		~FFixtureClock() { if (Out != nullptr) { *Out = (FPlatformTime::Seconds() - Started) * 1000.0; } }
	} FixtureClock{ FPlatformTime::Seconds(), &FixtureMs };

	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;

	if (Planet == nullptr || Ship == nullptr)
	{
		return;
	}

	// The first frames are startup; the terrain has not streamed anything yet
	// and counting misses there would be measuring the loading screen.
	//
	// **`-transectsettle` waits for the queue to go quiet as well (T068).**
	// Seven of the worst eight frames in chain169 were inside the first
	// kilometre, with 85 to 160 patch jobs still in flight: the world's first
	// load, which a player meets behind a loading screen and not at 900 m/s.
	// This arm says how much of the count is that and how much is the flight
	// the gate is actually about -- it reports the split rather than deciding
	// where the criterion's edge belongs. Capped at a minute so a queue that
	// never drains cannot hang the fixture.
	// **Latched, and that is the whole of it.** Unlatched, this re-entered the
	// moment the queue got busy again in flight -- which it does constantly --
	// froze the ship, put it back at the start, and left the frame clock
	// holding a stale timestamp across the gap: a 10,558 ms "frame" and a run
	// that measured nothing.
	static const bool bSettleFirst = FParse::Param(FCommandLine::Get(), TEXT("transectsettle"));
	if (!bSettled && Planet->GetStats().JobsInFlight == 0 && Warmup >= 3.0)
	{
		bSettled = true;
	}
	if (Warmup < 3.0 || (bSettleFirst && !bSettled && Warmup < 60.0))
	{
		Warmup += DeltaSeconds;
		Ship->SetFlightEnabled(false);
		Place(*Planet, *Ship, *Controller, 0.0);
		return;
	}

	// Holes the flight actually met. The terrain's own WorstUnfilled is kept
	// from the planet's first frame, and reads 1,689 at t=0 -- the world
	// loading, which this fixture does not fly through and the gate does not
	// ask about.
	const int32 UnfilledNow = Planet->GetStats().UnfilledNodes;
	FramesWithHoles += UnfilledNow > 0 ? 1 : 0;
	if (UnfilledNow > WorstUnfilledInFlight)
	{
		WorstUnfilledInFlight = UnfilledNow;
		WorstUnfilledAtKm = Travelled / 100000.0;
	}
	// Where the holes stop is the whole question. 124 frames of 13,334 had one
	// and the worst was at 0.0 km, which reads as the world still arriving --
	// but "reads as" is not a measurement, so this says how far in they go.
	if (UnfilledNow > 0)
	{
		LastHoleKm = Travelled / 100000.0;
		HoleFramesPastFirstKm += Travelled > 100000.0 ? 1 : 0;
	}

	{
		const double Now = FPlatformTime::Seconds();
		++GTicksSincePhoto;
		if (LastWallSeconds > 0.0 && GTicksSincePhoto > 3)
		{
			const double FrameMs = (Now - LastWallSeconds) * 1000.0;
			WorstFrameMs = FMath::Max(WorstFrameMs, FrameMs);
			FramesOverBudget += FrameMs > 16.7 ? 1 : 0;
			FrameMsSum += FrameMs;
			// **Read every frame, not only the slow ones (T068).** These are the
			// engine's counters for the frame just finished, and the classifier
			// trusts that they describe the wall-clock interval just measured.
			// That trust is what "a wait" rests on -- 90 of 118 frames over
			// budget with nothing busy -- and the same kilometre reported every
			// counter low in one run and a 51.5 ms render thread in the next,
			// which is what an off-by-one looks like. So the frame before a
			// spike is carried and printed beside it: if the cost shows up
			// there, the classifier is reading the wrong frame.
			const double GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
			const double RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
			const double GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
			const double RhiMs = FPlatformTime::ToMilliseconds(GRHIThreadTime);
			if (FrameMs > 16.7)
			{
				// The three a frame can be spent waiting in without any thread above
				// being busy: the RHI thread, the game thread's own wait for the render
				// thread, and the present.
				const double WaitMs = FPlatformTime::ToMilliseconds(GGameThreadWaitTime);
				const double SwapMs = FPlatformTime::ToMilliseconds(GSwapBufferTime);
				// And the two a frame can pass in with nothing working: the render
				// thread waiting on the GPU or the RHI, and the engine idling to pace
				// the frame. 295 of the 402 frames over budget in chain120 had no
				// thread and not the GPU over budget.
				const double RenderWaitMs = FPlatformTime::ToMilliseconds(GRenderThreadWaitTime);
				const double IdleMs = FApp::GetIdleTime() * 1000.0;
				const double UploadMs = Planet->GetStats().LastFrameUploadMs;
				const bool bGC = GCsSeen != GCsAtLastFrame;
				const bool bShaders = GShaderCompilingManager != nullptr && GShaderCompilingManager->GetNumRemainingJobs() > 0;
				const double Longest = FMath::Max3(GameMs, RenderMs, GpuMs);
				// **The RHI thread is a class of its own (T068).** The rank above
				// is game, render and GPU, so a frame the RHI thread spent 47 ms
				// in counted as nobody being busy -- and "a wait" is 159 of the
				// 188 frames over budget in the best clean run. The worst frames'
				// RHI column reaches that, and their render-thread trace grows
				// CreateRHIBuffer and vertex-buffer InitRHI, so this splits the
				// ones with a busy RHI thread out of the unexplained pile.
				const bool bRhiBound = Longest <= 16.7 && RhiMs > 16.7;
				OverRhi += bRhiBound ? 1 : 0;
				OverWaiting += Longest <= 16.7 && !bRhiBound ? 1 : 0;
				OverGame += Longest > 16.7 && Longest == GameMs ? 1 : 0;
				OverRender += Longest > 16.7 && Longest == RenderMs && Longest != GameMs ? 1 : 0;
				OverGpu += Longest > 16.7 && Longest == GpuMs && Longest != GameMs && Longest != RenderMs ? 1 : 0;
				OverWithUpload += UploadMs > 4.0 ? 1 : 0;
				OverWithGC += bGC ? 1 : 0;
				OverWithShaders += bShaders ? 1 : 0;
				WorstFrames.Add({ FrameMs, FString::Printf(
					TEXT("%.1f ms at %.1f km: game %.1f, render %.1f, GPU %.1f, RHI %.1f, game waiting %.1f, swap %.1f, render waiting %.1f, idle %.1f, patch upload %.1f, proxy %.2f, sections free %d, jobs in flight %d, fixture %.1f, the frame before: game %.1f, render %.1f, GPU %.1f, RHI %.1f, %.0f m above sea level%s%s"),
					FrameMs, Travelled / 100000.0, GameMs, RenderMs, GpuMs, RhiMs, WaitMs, SwapMs, RenderWaitMs, IdleMs, UploadMs, Planet->ProxyBuildMs,
					Planet->GetStats().SectionsFree, Planet->GetStats().JobsInFlight,
					// The previous frame's, because the scope guard writes it as
					// Tick leaves -- and the previous frame is what this
					// wall-clock interval spans, which is the point.
					FixtureMs,
					LastGameMs, LastRenderMs, LastGpuMs, LastRhiMs,
					(FVector3d(Ship->GetActorLocation()) - FVector3d(Planet->GetActorLocation())).Length() / 100.0 - Planet->Radius / 100.0,
					bGC ? TEXT(", a GC") : TEXT(""), bShaders ? TEXT(", shaders compiling") : TEXT("")) });
				WorstFrames.Sort([](const TPair<double, FString>& A, const TPair<double, FString>& B) { return A.Key > B.Key; });
				if (WorstFrames.Num() > 12)
				{
					WorstFrames.SetNum(12);
				}
			}
			LastGameMs = GameMs;
			LastRenderMs = RenderMs;
			LastGpuMs = GpuMs;
			LastRhiMs = RhiMs;
		}
		LastWallSeconds = Now;
		GCsAtLastFrame = GCsSeen;
	}
	// **Whether the patches meet, every 20 km** (T068's "no popping that reads as
	// a seam"). The edge-gap probe measures every drawn patch edge against the
	// patch across it, with the vertices the renderer was given; it is slow, so
	// it runs ten times and its frames are kept out of the timing.
	if (FMath::FloorToInt32(Travelled / 2000000.0) != FMath::FloorToInt32((Travelled + TransectSpeed * DeltaSeconds) / 2000000.0))
	{
		FString Gaps;
		Planet->MeasureEdgeGaps(Gaps);
		TArray<FString> GapLines;
		Gaps.ParseIntoArrayLines(GapLines);
		SeamSamples.Add(FString::Printf(TEXT("at %.0f km: %s | %s"), Travelled / 100000.0,
			GapLines.IsValidIndex(2) ? *GapLines[2] : TEXT(""), GapLines.IsValidIndex(3) ? *GapLines[3] : TEXT("")));
		GTicksSincePhoto = 0;
	}
	// Two photographs on the way (T068): at 100 km, where frames are within
	// budget, and at 185 km, among the spikes, so the two can be compared.
	for (const double Mark : { 10000000.0, 18500000.0 })
	{
		if (Travelled < Mark && Travelled + TransectSpeed * DeltaSeconds >= Mark)
		{
			GTicksSincePhoto = 0;
			FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				FString::Printf(TEXT("transect-%.0fkm.png"), Mark / 100000.0))), false, false);
		}
	}
	Travelled += TransectSpeed * DeltaSeconds;
	Place(*Planet, *Ship, *Controller, Travelled);

	// The trace: straight down from the ship, asking only whether anything is
	// there. A miss means the patch below has not cooked, and a ship would have
	// nothing to land on or collide with at this moment.
	const FVector From = Ship->GetActorLocation();
	const FVector Down = -(From - Planet->GetActorLocation()).GetSafeNormal();

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LedgerTransect), false, Ship);
	const bool bHit = World->LineTraceSingleByChannel(
		Hit, From, From + Down * TraceDepth, ECC_WorldStatic, Params);

	++Frames;
	if (bHit)
	{
		CurrentMiss = 0;
		const double BelowExactMetres = (Hit.Distance - TransectAltitude) / 100.0;
		HitErrorSum += FMath::Abs(BelowExactMetres);
		HitErrorWorst = FMath::Max(HitErrorWorst, FMath::Abs(BelowExactMetres));
		FLedgerTerrainSample Under;
		HitsSampled += Planet->SampleTerrain(FVector3d(From), Under) ? 1 : 0;
	}
	else
	{
		++Misses;
		// Which kind of miss. Only a drawn section can be asked about
		// collision; no drawn section at all is a streaming failure, not a
		// collision one, and the two want different fixes.
		FLedgerTerrainSample Under;
		if (!Planet->SampleTerrain(FVector3d(From), Under))
		{
			++MissesNoGround;
		}
		else
		{
			MissPatchSizeSum += Under.PatchWorldSize;
			++(Under.bCollision ? MissesUncooked : MissesNoCollision);
		}
		++CurrentMiss;
		LongestMiss = FMath::Max(LongestMiss, CurrentMiss);
	}

	if (Travelled >= TransectDistance)
	{
		Finish();
	}
}

void ULedgerTransect::Place(ALedgerPlanet& Planet, ALedgerShip& Ship,
	APlayerController& Controller, double Along) const
{
	// A great circle from the town site, so the ground under it is the same
	// ground the rest of the flight visits and the run is comparable.
	const ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>();
	const FVector3d Start = Builder != nullptr ? Builder->GetSiteDirection() : FVector3d::UnitZ();
	const FVector3d East = FVector3d::CrossProduct(Start, FVector3d::UpVector).GetSafeNormal();

	const double Angle = Along / Planet.Radius;
	const FVector3d Direction = (Start * FMath::Cos(Angle) + East * FMath::Sin(Angle)).GetSafeNormal();

	const double Ground = Planet.SurfaceRadiusAt(Direction);
	Ship.SetActorLocation(Planet.GetActorLocation() + FVector(Direction * (Ground + TransectAltitude)));

	// Facing along the track, so the collision cook's velocity lead points where
	// the ship is actually going. Getting this wrong would test the wrong thing:
	// a stationary-facing ship cooks a circle rather than a corridor.
	const FVector3d Ahead = (Start * -FMath::Sin(Angle) + East * FMath::Cos(Angle)).GetSafeNormal();
	const FRotator Attitude = FRotationMatrix::MakeFromXZ(FVector(Ahead), FVector(Direction)).Rotator();
	Ship.SetActorRotation(Attitude);
	Controller.SetControlRotation(Attitude);
}

void ULedgerTransect::Finish()
{
	bRunning = false;

	const double MissFraction = Frames > 0 ? 100.0 * Misses / static_cast<double>(Frames) : 0.0;
	const bool bPassed = Misses == 0;

	FString Body;
	Body += TEXT("Collision transect.\n\n");
	Body += FString::Printf(TEXT("  distance      %.0f km\n"), TransectDistance / 100000.0);
	Body += FString::Printf(TEXT("  speed         %.0f m/s\n"), TransectSpeed / 100.0);
	// What the terrain thought it was doing while the traces were missing.
	// A trace that finds nothing is either a patch that never asked for
	// collision or one that asked and had not cooked, and those are
	// different bugs.
	const ULedgerWorldBuilder* ReportBuilder = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	if (const ALedgerPlanet* Planet =
		ReportBuilder != nullptr ? ReportBuilder->GetPlanet() : nullptr)
	{
		const FLedgerTerrainStats& Terrain = Planet->GetStats();
		Body += FString::Printf(
			TEXT("  nodes visible %d, of which with collision %d\n"),
			Terrain.VisibleNodes, Terrain.NodesWithCollision);
		// **Holes, from the fixture the gate names.** M02's first two checks ask
		// for zero unfilled patches on this transect, and this report never said.
		// The zeros on hand came from other runs -- a 300 m/s descent and a perf
		// flight -- and a gate recorded off a different fixture's numbers is a
		// gate recorded off something it did not measure.
		Body += FString::Printf(
			TEXT("  holes         %d unfilled now; in flight %d frames of %d had one (%d past the first km), worst %d at %.1f km, last at %.1f km; %d worst since the planet loaded (t=%.0fs)\n"),
			Terrain.UnfilledNodes, FramesWithHoles, Frames, HoleFramesPastFirstKm,
			WorstUnfilledInFlight, WorstUnfilledAtKm, LastHoleKm,
			Terrain.WorstUnfilled, Terrain.WorstUnfilledAt);
		Body += FString::Printf(
			TEXT("  worst cook    %.2f ms\n"), Terrain.WorstFrameCollisionMs);
		// T068: the collision proxy under the ship, and what rebuilding it costs a frame.
		Body += FString::Printf(TEXT("  collision proxy rebuilt %d times, %.2f ms at worst, %.2f ms last\n"),
			Planet->ProxyBuilds, Planet->ProxyWorstMs, Planet->ProxyBuildMs);
	}
	Body += FString::Printf(TEXT("  altitude      %.0f m\n"), TransectAltitude / 100.0);
	Body += FString::Printf(TEXT("  frames        %d\n"), Frames);
	Body += FString::Printf(TEXT("  frame time    %.2f ms mean, %.2f ms worst, %d over 16.7 ms\n"),
		Frames > 1 ? FrameMsSum / (Frames - 1) : 0.0, WorstFrameMs, FramesOverBudget);
	Body += FString::Printf(TEXT("    over budget, by the longest: game thread %d, render thread %d, GPU %d, RHI thread %d, none of those (a wait) %d\n"),
		OverGame, OverRender, OverGpu, OverRhi, OverWaiting);
	Body += FString::Printf(TEXT("    over budget with a patch upload over 4 ms %d, a GC %d, shaders compiling %d\n"),
		OverWithUpload, OverWithGC, OverWithShaders);
	Body += TEXT("  seams (edge-gap probe, every 20 km):\n");
	for (const FString& Sample : SeamSamples)
	{
		Body += TEXT("    ") + Sample + TEXT("\n");
	}
	for (const TPair<double, FString>& Frame : WorstFrames)
	{
		Body += TEXT("      ") + Frame.Value + TEXT("\n");
	}
	Body += FString::Printf(TEXT("  traces missed %d  (%.3f%%)\n"), Misses, MissFraction);
	Body += FString::Printf(TEXT("    no ground drawn under the ship    %d\n"), MissesNoGround);
	Body += FString::Printf(TEXT("    drawn, never asked for collision  %d\n"), MissesNoCollision);
	Body += FString::Printf(TEXT("    drawn with collision, not cooked  %d\n"), MissesUncooked);
	Body += FString::Printf(TEXT("  longest gap   %d consecutive frames\n\n"), LongestMiss);
	Body += FString::Printf(TEXT("VERDICT: %s\n"), bPassed
		? TEXT("PASS — collision present under the ship on every frame")
		: TEXT("FAIL — the ship had nothing beneath it at some point"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("transect.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);

	UE_LOG(LogLedger, Log, TEXT("transect -> %s"), *Path);
	UE_LOG(LogLedger, Log, TEXT("  %d frames, %d missed (%.3f%%), longest gap %d, %s"),
		Frames, Misses, MissFraction, LongestMiss, bPassed ? TEXT("PASS") : TEXT("FAIL"));
	const int32 DrawnMisses = MissesNoCollision + MissesUncooked;
	UE_LOG(LogLedger, Log, TEXT("  misses: %d with no ground drawn, %d drawn without collision, %d drawn with it but not cooked; the drawn ones %.0f m across on average"),
		MissesNoGround, MissesNoCollision, MissesUncooked,
		DrawnMisses > 0 ? MissPatchSizeSum / DrawnMisses / 100.0 : 0.0);
	UE_LOG(LogLedger, Log, TEXT("  hits: %d, of which the terrain query found drawn ground under %d"), Frames - Misses, HitsSampled);
	UE_LOG(LogLedger, Log, TEXT("  the collision hit was %.1f m from the exact surface on average, %.1f m at worst"),
		Frames > Misses ? HitErrorSum / (Frames - Misses) : 0.0, HitErrorWorst);

	if (GEngine != nullptr && GetWorld() != nullptr)
	{
		GEngine->Exec(GetWorld(), TEXT("Ledger.Terrain.Stats"));
	}
	FGenericPlatformMisc::RequestExit(false);
}

TStatId ULedgerTransect::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerTransect, STATGROUP_Tickables);
}
