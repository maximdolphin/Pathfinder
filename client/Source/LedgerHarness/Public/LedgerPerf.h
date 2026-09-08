// Frame-time recording for the scripted flight.
//
// **Why this exists.** "It felt smooth" is not a measurement, and the place a
// streaming planet drops frames is precisely the place a person is not looking
// at the frame counter — coming in to land, with the LOD refining four levels
// in three seconds. A run that does not record its frame times is a run that
// can regress without anybody noticing until it is ten regressions deep.
//
// Every scripted run now writes out/performance.txt: percentiles overall and
// per phase, the worst frames with the phase they happened in, and a pass or
// fail against the budget. The budget is the acceptance criterion, so it is
// checked by the harness rather than by the person reading the log.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerPerf.generated.h"

/// One phase of the scripted flight, with its own statistics.
///
/// Frame time alone says a run is slow. The three thread timings say which of
/// the three is the reason, which is the difference between a diagnosis and a
/// guess: a game-thread bound run is fixed nowhere near where a GPU-bound one
/// is.
struct FLedgerPerfPhase
{
	FString Name;
	double StartedAt = 0.0;
	TArray<double> FrameMs;
	TArray<double> GameMs;
	TArray<double> RenderMs;
	TArray<double> GpuMs;
	TArray<int32> DrawCalls;
	TArray<int32> Primitives;
};

UCLASS()
class LEDGERHARNESS_API ULedgerPerfSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/// Names the phase subsequent frames belong to. Called by the flight script.
	void BeginPhase(const FString& Name);

	/// Writes the report and returns whether the budget held.
	bool WriteReport(const FString& Path) const;

	/// Milliseconds a frame may take. 16.7 is sixty per second.
	double BudgetMs = 16.7;

	/// A frame over this is logged the moment it happens, with its phase, so a
	/// stall has a timestamp before anybody goes looking for it.
	double StallMs = 33.0;

private:
	TArray<FLedgerPerfPhase> Phases;

	/// The first seconds of a run are shader compilation and warm-up and say
	/// nothing about steady state. Frames before this are recorded and excluded
	/// from the verdict.
	double WarmupSeconds = 6.0;
};
