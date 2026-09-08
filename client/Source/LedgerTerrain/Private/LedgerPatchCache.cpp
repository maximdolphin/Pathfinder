#include "LedgerPatchCache.h"

int64 FLedgerCachedPatch::Bytes() const
{
	// GetAllocatedSize rather than Num() times sizeof: TArray over-allocates on
	// growth, and a budget measured against the useful part of an allocation
	// overshoots by whatever the slack happens to be.
	return Land.ProcVertexBuffer.GetAllocatedSize() + Land.ProcIndexBuffer.GetAllocatedSize()
		+ Water.ProcVertexBuffer.GetAllocatedSize() + Water.ProcIndexBuffer.GetAllocatedSize();
}

FLedgerCachedPatch* FLedgerPatchCache::Take(uint64 Key)
{
	TSharedPtr<FLedgerCachedPatch>* Found = Entries.Find(Key);
	if (Found == nullptr || !Found->IsValid())
	{
		return nullptr;
	}
	(*Found)->LastUsed = ++Clock;
	return Found->Get();
}

void FLedgerPatchCache::Remove(uint64 Key)
{
	TSharedPtr<FLedgerCachedPatch> Entry;
	if (Entries.RemoveAndCopyValue(Key, Entry) && Entry.IsValid())
	{
		HeldBytes -= Entry->Bytes();
	}
}

void FLedgerPatchCache::Empty()
{
	Entries.Empty();
	HeldBytes = 0;
}

void FLedgerPatchCache::Insert(uint64 Key, TSharedPtr<FLedgerCachedPatch> Entry)
{
	if (!Entry.IsValid() || BudgetBytes <= 0)
	{
		return;
	}

	Entry->LastUsed = ++Clock;

	// Replacing an entry has to subtract the old one first. The one time this
	// was written the other way round, the byte count drifted upward until the
	// cache believed it was full and evicted everything.
	Remove(Key);

	HeldBytes += Entry->Bytes();
	Entries.Add(Key, Entry);

	// ponytail: linear scan for the least-recently-used entry. Eviction only
	// runs once the budget is already exceeded, over a few hundred entries, so
	// it is microseconds — swap in an intrusive list if it ever shows up in a
	// profile.
	while (HeldBytes > BudgetBytes && Entries.Num() > 1)
	{
		uint64 Oldest = 0;
		uint64 OldestUse = MAX_uint64;
		for (const TPair<uint64, TSharedPtr<FLedgerCachedPatch>>& Candidate : Entries)
		{
			if (Candidate.Value.IsValid() && Candidate.Value->LastUsed < OldestUse)
			{
				OldestUse = Candidate.Value->LastUsed;
				Oldest = Candidate.Key;
			}
		}
		if (OldestUse == MAX_uint64)
		{
			break;
		}
		Remove(Oldest);
		++EvictionCount;
	}
}
