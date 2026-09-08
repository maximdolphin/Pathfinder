#include "LedgerSimSubsystem.h"

#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogLedger);

FString ULedgerSimSubsystem::SnapshotPath()
{
	// The UE project lives at <repo>/client, the sim writes to <repo>/out.
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("snapshot.json")));
}

void ULedgerSimSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Ledger.Refresh"),
		TEXT("Reload the sim snapshot from disk."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this] { Refresh(); }),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Ledger.Board"),
		TEXT("Print the contract board from the current sim snapshot."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this] { LogContractBoard(); }),
		ECVF_Default));

	if (Refresh())
	{
		LogContractBoard();
	}
}

void ULedgerSimSubsystem::Deinitialize()
{
	for (IConsoleObject* Command : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Command);
	}
	ConsoleCommands.Empty();

	Super::Deinitialize();
}

bool ULedgerSimSubsystem::Refresh()
{
	const FString Path = SnapshotPath();

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogLedger, Warning,
			TEXT("no sim snapshot at %s - run the sim first (cargo run --release)"), *Path);
		return false;
	}

	FLedgerSnapshot Parsed;
	FString Error;
	if (!ParseLedgerSnapshot(Json, Parsed, Error))
	{
		UE_LOG(LogLedger, Error, TEXT("snapshot at %s is unreadable: %s"), *Path, *Error);
		return false;
	}

	// The sim is authoritative, so a conservation failure here can only be a
	// transport bug — which is exactly the class of bug a typed boundary is
	// supposed to catch, so it is worth the handful of cycles.
	FString ConservationError;
	if (!Parsed.ValidateConservation(ConservationError))
	{
		UE_LOG(LogLedger, Error, TEXT("snapshot violates a sim invariant: %s"), *ConservationError);
		return false;
	}

	Snapshot = MoveTemp(Parsed);
	bLoaded = true;

	UE_LOG(LogLedger, Log,
		TEXT("loaded snapshot: seed %lld, tick %lld, %lld events, %d corps, %d lanes, %d contracts"),
		Snapshot.Seed, Snapshot.Tick, Snapshot.EventCount,
		Snapshot.Corps.Num(), Snapshot.LaneOwners.Num(), Snapshot.Contracts.Num());

	return true;
}

void ULedgerSimSubsystem::LogContractBoard() const
{
	if (!bLoaded)
	{
		UE_LOG(LogLedger, Warning, TEXT("no snapshot loaded; try Ledger.Refresh"));
		return;
	}

	UE_LOG(LogLedger, Log, TEXT("=== CONTRACT BOARD ==="));

	int32 Open = 0;
	for (const FLedgerContract& Contract : Snapshot.Contracts)
	{
		if (Contract.bResolved)
		{
			continue;
		}
		++Open;

		// The board says how old the grudge is, because that is the thing that
		// makes it read as authored rather than generated (§6.6).
		const int64 Age = Contract.PostedTick - Contract.TensionIncurredTick;
		UE_LOG(LogLedger, Log,
			TEXT("  [%s] %s wants %s  - posted t%lld, tension %lld ticks older%s"),
			LexToString(Contract.Archetype),
			*Contract.Poster,
			*Contract.Target,
			Contract.PostedTick,
			Age,
			Contract.ReferencesPreExistingTension() ? TEXT("") : TEXT("  <- MANUFACTURED, not consequent"));
	}

	if (Open == 0)
	{
		UE_LOG(LogLedger, Log, TEXT("  (no open contracts at this tick)"));
	}

	const FLedgerInvestigation& Investigation = Snapshot.Investigation;
	if (Investigation.bActive)
	{
		// A region set, never a pin (§6.3).
		TArray<FString> Names;
		for (const int32 RegionId : Investigation.SearchVolume)
		{
			Names.Add(Snapshot.RegionName(RegionId));
		}
		UE_LOG(LogLedger, Log,
			TEXT("  searching for %s after %d inquiries - %d of %d regions still in play: %s"),
			*Investigation.Target,
			Investigation.Inquiries,
			Investigation.SearchVolume.Num(),
			Snapshot.Regions.Num(),
			*FString::Join(Names, TEXT(", ")));
	}

	UE_LOG(LogLedger, Log, TEXT("=== STANDINGS ==="));
	for (const FLedgerCorp& Corp : Snapshot.Corps)
	{
		UE_LOG(LogLedger, Log, TEXT("  %-32s %12.2f  %2d lanes"),
			*Corp.Name, Corp.CashDisplay(), Corp.Lanes);
	}
}
