// The client's window onto the sim. Design §5, §10 (`LedgerCore`).
//
// One responsibility: own the current snapshot and the lifecycle around getting
// one. Parsing lives in `LedgerSnapshot`; rendering lives above this. When the
// Phase 1 protobuf-over-TCP transport lands (ADR-0001), only `Refresh` changes.
//
// There is deliberately no Blueprint surface here beyond read-only accessors —
// design §9: "Blueprint is not a place for logic".

#pragma once

#include "CoreMinimal.h"
#include "LedgerSnapshot.h"
#include "Subsystems/EngineSubsystem.h"
#include "LedgerSimSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLedger, Log, All);

// An *engine* subsystem, not a game-instance one. The contract board is a
// developer-facing view of sim state, and tying it to a GameInstance would mean
// it — and the console commands below — only existed inside Play-In-Editor.
UCLASS()
class LEDGERCLIENT_API ULedgerSimSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/// Reloads from the sim's snapshot file. Returns false and logs on failure;
	/// the previously loaded snapshot is left untouched so a bad read cannot
	/// blank the contract board.
	bool Refresh();

	bool HasSnapshot() const { return bLoaded; }
	const FLedgerSnapshot& GetSnapshot() const { return Snapshot; }

	/// Where the sim writes. Phase 0 hands state over as a file on disk; this is
	/// the one place that assumption lives.
	static FString SnapshotPath();

	/// Renders the contract board to the log. This is the client half of the
	/// §13.1 gate — the same state the text dump describes, arriving through the
	/// wire contract instead of through a print statement.
	void LogContractBoard() const;

private:
	FLedgerSnapshot Snapshot;
	bool bLoaded = false;

	TArray<IConsoleObject*> ConsoleCommands;
};
