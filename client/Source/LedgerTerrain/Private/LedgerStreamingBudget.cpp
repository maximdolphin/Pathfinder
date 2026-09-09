#include "LedgerStreamingBudget.h"

void FLedgerStreamingBudget::Begin(double TotalMs)
{
	BudgetMs = FMath::Max(0.0, TotalMs);
	for (int32 Class = 0; Class < static_cast<int32>(ELedgerStreamClass::Count); ++Class)
	{
		ByClass[Class] = 0.0;
		Denied[Class] = 0;
	}
}

double FLedgerStreamingBudget::TotalSpentMs() const
{
	double Total = 0.0;
	for (const double Ms : ByClass)
	{
		Total += Ms;
	}
	return Total;
}

int32 FLedgerStreamingBudget::TotalRefused() const
{
	int32 Total = 0;
	for (const int32 Count : Denied)
	{
		Total += Count;
	}
	return Total;
}

double FLedgerStreamingBudget::RemainingMs() const
{
	return BudgetMs - TotalSpentMs();
}

bool FLedgerStreamingBudget::Allows(ELedgerStreamClass Class) const
{
	// Collision is not on the budget. It is on the budget's conscience: the
	// stats say how far over it went, and a run where that number is large is
	// a run where the collision radius or the patch size is wrong. What it is
	// not is a frame where the player falls through the floor.
	if (Class == ELedgerStreamClass::Collision)
	{
		return true;
	}
	return RemainingMs() > 0.0;
}

void FLedgerStreamingBudget::Spent(ELedgerStreamClass Class, double Ms)
{
	ByClass[static_cast<int32>(Class)] += FMath::Max(0.0, Ms);
}

void FLedgerStreamingBudget::Refused(ELedgerStreamClass Class)
{
	++Denied[static_cast<int32>(Class)];
}
