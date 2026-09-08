#include "LedgerSnapshot.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/// Total by construction: every enumerator has a spelling, and adding one
	/// without a spelling is caught by the switch below rather than defaulting
	/// to something plausible.
	bool ParseArchetype(const FString& Text, ELedgerArchetype& Out)
	{
		if (Text == TEXT("Bounty"))
		{
			Out = ELedgerArchetype::Bounty;
			return true;
		}
		if (Text == TEXT("Audit"))
		{
			Out = ELedgerArchetype::Audit;
			return true;
		}
		if (Text == TEXT("Collection"))
		{
			Out = ELedgerArchetype::Collection;
			return true;
		}
		return false;
	}

	bool RequireArray(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		const TArray<TSharedPtr<FJsonValue>>*& Out,
		FString& OutError)
	{
		if (!Object->TryGetArrayField(Field, Out))
		{
			OutError = FString::Printf(TEXT("missing or non-array field '%s'"), Field);
			return false;
		}
		return true;
	}
}

const TCHAR* LexToString(ELedgerArchetype Archetype)
{
	switch (Archetype)
	{
	case ELedgerArchetype::Bounty:
		return TEXT("Bounty");
	case ELedgerArchetype::Audit:
		return TEXT("Audit");
	case ELedgerArchetype::Collection:
		return TEXT("Collection");
	case ELedgerArchetype::Unknown:
	default:
		return TEXT("Unknown");
	}
}

const FLedgerCorp* FLedgerSnapshot::FindCorp(int32 CorpId) const
{
	return Corps.FindByPredicate([CorpId](const FLedgerCorp& Corp) { return Corp.Id == CorpId; });
}

FString FLedgerSnapshot::RegionName(int32 RegionId) const
{
	const FLedgerRegion* Found =
		Regions.FindByPredicate([RegionId](const FLedgerRegion& Region) { return Region.Id == RegionId; });
	return Found ? Found->Name : FString::Printf(TEXT("region %d"), RegionId);
}

bool FLedgerSnapshot::ValidateConservation(FString& OutError) const
{
	int32 CountedFromCorps = 0;
	for (const FLedgerCorp& Corp : Corps)
	{
		CountedFromCorps += Corp.Lanes;
	}

	if (CountedFromCorps != LaneOwners.Num())
	{
		OutError = FString::Printf(
			TEXT("territory is not conserved: corps claim %d lanes, ownership table has %d"),
			CountedFromCorps,
			LaneOwners.Num());
		return false;
	}

	for (int32 Lane = 0; Lane < LaneOwners.Num(); ++Lane)
	{
		if (FindCorp(LaneOwners[Lane]) == nullptr)
		{
			OutError = FString::Printf(TEXT("lane %d is owned by unknown corp %d"), Lane, LaneOwners[Lane]);
			return false;
		}
	}

	return true;
}

bool ParseLedgerSnapshot(const FString& Json, FLedgerSnapshot& Out, FString& OutError)
{
	Out = FLedgerSnapshot();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("snapshot is not valid JSON");
		return false;
	}

	Out.Schema = Root->GetIntegerField(TEXT("schema"));
	if (Out.Schema != LedgerSnapshotSchemaVersion)
	{
		// Refuse rather than partially parse. A snapshot from a newer sim may
		// have moved a field this build would otherwise read as a default.
		OutError = FString::Printf(
			TEXT("snapshot schema %d, this build understands %d"),
			Out.Schema,
			LedgerSnapshotSchemaVersion);
		return false;
	}

	Out.Seed = static_cast<int64>(Root->GetNumberField(TEXT("seed")));
	Out.Tick = static_cast<int64>(Root->GetNumberField(TEXT("tick")));
	Out.EventCount = static_cast<int64>(Root->GetNumberField(TEXT("eventCount")));

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;

	if (!RequireArray(Root, TEXT("regions"), Array, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		const TSharedPtr<FJsonObject>& Object = Value->AsObject();
		FLedgerRegion Region;
		Region.Id = Object->GetIntegerField(TEXT("id"));
		Region.Name = Object->GetStringField(TEXT("name"));
		Out.Regions.Add(MoveTemp(Region));
	}

	if (!RequireArray(Root, TEXT("corps"), Array, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		const TSharedPtr<FJsonObject>& Object = Value->AsObject();
		FLedgerCorp Corp;
		Corp.Id = Object->GetIntegerField(TEXT("id"));
		Corp.Name = Object->GetStringField(TEXT("name"));
		// The scaled integer, not the pre-formatted string beside it. The sim
		// ships both; the string is for humans reading the file.
		Corp.CashRaw = static_cast<int64>(Object->GetNumberField(TEXT("cashRaw")));
		Corp.Lanes = Object->GetIntegerField(TEXT("lanes"));
		Corp.HomeRegion = Object->GetIntegerField(TEXT("home"));
		Out.Corps.Add(MoveTemp(Corp));
	}

	if (!RequireArray(Root, TEXT("laneOwners"), Array, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		Out.LaneOwners.Add(static_cast<int32>(Value->AsNumber()));
	}

	if (!RequireArray(Root, TEXT("contracts"), Array, OutError))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		const TSharedPtr<FJsonObject>& Object = Value->AsObject();
		FLedgerContract Contract;
		Contract.Id = Object->GetIntegerField(TEXT("id"));

		const FString ArchetypeText = Object->GetStringField(TEXT("archetype"));
		if (!ParseArchetype(ArchetypeText, Contract.Archetype))
		{
			OutError = FString::Printf(TEXT("unknown contract archetype '%s'"), *ArchetypeText);
			return false;
		}

		Contract.Poster = Object->GetStringField(TEXT("poster"));
		Contract.Target = Object->GetStringField(TEXT("target"));
		Contract.TargetId = Object->GetIntegerField(TEXT("targetId"));
		Contract.PostedTick = static_cast<int64>(Object->GetNumberField(TEXT("posted")));
		Contract.TensionIncurredTick = static_cast<int64>(Object->GetNumberField(TEXT("tensionIncurred")));
		Contract.bResolved = Object->GetBoolField(TEXT("resolved"));
		Out.Contracts.Add(MoveTemp(Contract));
	}

	const TSharedPtr<FJsonObject>* InvestigationObject = nullptr;
	if (Root->TryGetObjectField(TEXT("investigation"), InvestigationObject) && InvestigationObject != nullptr)
	{
		FLedgerInvestigation& Investigation = Out.Investigation;
		Investigation.bActive = true;
		Investigation.ContractId = (*InvestigationObject)->GetIntegerField(TEXT("contract"));
		Investigation.Target = (*InvestigationObject)->GetStringField(TEXT("target"));
		Investigation.Inquiries = (*InvestigationObject)->GetIntegerField(TEXT("inquiries"));

		const TArray<TSharedPtr<FJsonValue>>* Volume = nullptr;
		if ((*InvestigationObject)->TryGetArrayField(TEXT("searchVolume"), Volume))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Volume)
			{
				Investigation.SearchVolume.Add(static_cast<int32>(Value->AsNumber()));
			}
		}
	}

	return true;
}
