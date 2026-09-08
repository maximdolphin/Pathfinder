// Typed view of a sim snapshot. Design §5.1, §8.2.
//
// This translation unit knows how to turn bytes into typed state and nothing
// else — no engine lifecycle, no file access, no UObjects. That is what makes
// it testable without an editor, and what will let the Phase 1 protobuf
// transport replace the byte source without touching anything downstream.
//
// **No magic strings.** Everything the sim classifies is a `UENUM` here, parsed
// once at the boundary. A string from the wire that does not match a known
// variant is a parse failure, not a silently-defaulted value.

#pragma once

#include "CoreMinimal.h"
#include "LedgerSnapshot.generated.h"

/// Mirrors `event::Archetype` in the sim. Design §6.6.
UENUM()
enum class ELedgerArchetype : uint8
{
	Unknown,
	Bounty,
	Audit,
	Collection
};

LEDGERCLIENT_API const TCHAR* LexToString(ELedgerArchetype Archetype);

USTRUCT()
struct LEDGERCLIENT_API FLedgerRegion
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	FString Name;
};

USTRUCT()
struct LEDGERCLIENT_API FLedgerCorp
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	FString Name;

	/// Fixed-point, scaled by 10^6. Design §5.2: authoritative values cross the
	/// boundary as integers. The float conversion happens in `CashDisplay` and
	/// nowhere else, because presentation is the only place a float is allowed.
	UPROPERTY()
	int64 CashRaw = 0;

	UPROPERTY()
	int32 Lanes = 0;

	UPROPERTY()
	int32 HomeRegion = 0;

	double CashDisplay() const
	{
		return static_cast<double>(CashRaw) / 1000000.0;
	}
};

USTRUCT()
struct LEDGERCLIENT_API FLedgerContract
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	ELedgerArchetype Archetype = ELedgerArchetype::Unknown;

	UPROPERTY()
	FString Poster;

	UPROPERTY()
	FString Target;

	UPROPERTY()
	int32 TargetId = 0;

	UPROPERTY()
	int64 PostedTick = 0;

	UPROPERTY()
	int64 TensionIncurredTick = 0;

	UPROPERTY()
	bool bResolved = false;

	/// §6.6 / §12: a contract whose tension was incurred at the moment of
	/// posting is a template wearing a simulation's clothes. The client can
	/// check this for itself, which is the point of shipping the tick.
	bool ReferencesPreExistingTension() const
	{
		return TensionIncurredTick < PostedTick;
	}
};

/// The open search volume. Design §6.3: a set of regions, never a pin. The
/// client renders a region set; if this ever collapses into a single
/// coordinate, the investigation has become a quest marker.
USTRUCT()
struct LEDGERCLIENT_API FLedgerInvestigation
{
	GENERATED_BODY()

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	int32 ContractId = 0;

	UPROPERTY()
	FString Target;

	UPROPERTY()
	int32 Inquiries = 0;

	UPROPERTY()
	TArray<int32> SearchVolume;
};

USTRUCT()
struct LEDGERCLIENT_API FLedgerSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Schema = 0;

	UPROPERTY()
	int64 Seed = 0;

	UPROPERTY()
	int64 Tick = 0;

	UPROPERTY()
	int64 EventCount = 0;

	UPROPERTY()
	TArray<FLedgerRegion> Regions;

	UPROPERTY()
	TArray<FLedgerCorp> Corps;

	/// Indexed by lane id; the value is the owning corp id. Ownership partitions
	/// the set — every lane has exactly one owner (§6.5), which
	/// `ValidateConservation` checks rather than assumes.
	UPROPERTY()
	TArray<int32> LaneOwners;

	UPROPERTY()
	TArray<FLedgerContract> Contracts;

	UPROPERTY()
	FLedgerInvestigation Investigation;

	const FLedgerCorp* FindCorp(int32 CorpId) const;
	FString RegionName(int32 RegionId) const;

	/// Re-checks the sim's conservation law on the client's own copy. The sim is
	/// authoritative, so this can only ever detect a transport bug — which is
	/// exactly why it is worth doing at the boundary.
	bool ValidateConservation(FString& OutError) const;
};

/// The schema version this build understands. A snapshot from the future is
/// refused rather than partially parsed.
inline constexpr int32 LedgerSnapshotSchemaVersion = 1;

/// Parses a snapshot document. Returns false and fills `OutError` on malformed
/// input, an unknown enum variant, or a schema mismatch.
LEDGERCLIENT_API bool ParseLedgerSnapshot(const FString& Json, FLedgerSnapshot& Out, FString& OutError);
