// The project's log category.
//
// It used to live in LedgerSimSubsystem.h, which meant every file in the
// project included a gameplay header in order to write a line to the log. That
// is how a dependency graph becomes a complete graph: not by design, but one
// convenience at a time.

#pragma once

#include "CoreMinimal.h"

LEDGERCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogLedger, Log, All);
