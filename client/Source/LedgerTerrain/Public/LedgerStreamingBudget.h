// One authority for how much streaming work a frame may do. T063, §6.8.
//
// **Because three separate budgets are not a budget.** Cache uploads, finished
// patch jobs and the scatter rebuild each had their own limit, and each was
// reasonable on its own; a frame that did all three did the sum of them. The
// only number that matters is the total, and nothing was tracking it.
//
// **And priority, not arrival order.** Work is not interchangeable. Ground the
// player is about to stand on has to arrive; ground on the horizon can wait a
// frame; ground behind them can wait until they turn round. Given a budget too
// small for everything, spending it in arrival order means the horizon can
// starve the floor, which is exactly the failure the acceptance describes as
// "holes" rather than "less detail".

#pragma once

#include "CoreMinimal.h"

/// What a piece of streaming work is for, in the order it gets paid.
enum class ELedgerStreamClass : uint8
{
	/// Ground close enough to collide with. If this does not arrive, the player
	/// falls through the world, and there is no amount of frame budget worth
	/// that. Always served, budget or not.
	Collision = 0,

	/// Ground that is visible and has nothing standing in for it -- no ancestor
	/// drawn in its place. A hole.
	Hole = 1,

	/// Ground that is visible and would be finer than what is currently drawn
	/// in its place. Detail. This is what the acceptance expects to lose.
	Detail = 2,

	/// Anything else: prefetching, scatter, work that improves a frame nobody
	/// is looking at yet.
	Speculative = 3,

	Count = 4,
};

/// A frame's worth of streaming time, and where it went.
class LEDGERTERRAIN_API FLedgerStreamingBudget
{
public:
	/// Starts a frame. Everything spent before the next call to this is charged
	/// to it.
	void Begin(double TotalMs);

	/// Whether there is time for a piece of work of this class.
	///
	/// Collision is always allowed. Everything else is allowed while the frame
	/// has time left, and the classes below it are cut off first because they
	/// are asked later -- the caller walks the classes in order.
	bool Allows(ELedgerStreamClass Class) const;

	/// Records what a piece of work actually cost.
	void Spent(ELedgerStreamClass Class, double Ms);

	/// Records that a piece of work was refused for want of budget. This is the
	/// number that says whether a budget is holding by doing less or by doing
	/// nothing.
	void Refused(ELedgerStreamClass Class);

	double SpentMs(ELedgerStreamClass Class) const { return ByClass[static_cast<int32>(Class)]; }
	int32 RefusedCount(ELedgerStreamClass Class) const { return Denied[static_cast<int32>(Class)]; }
	double TotalSpentMs() const;
	int32 TotalRefused() const;

	/// How much of the frame's allowance is left, which can be negative when
	/// collision work has overrun it.
	double RemainingMs() const;

private:
	double BudgetMs = 0.0;
	double ByClass[static_cast<int32>(ELedgerStreamClass::Count)] = {};
	int32 Denied[static_cast<int32>(ELedgerStreamClass::Count)] = {};
};
