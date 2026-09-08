// Turns one quadtree node into vertex buffers. Design SS6.8.
//
// A free function over a job struct, deliberately: it runs on a worker thread
// and every input it needs is copied into the job at launch, so it can touch
// no actor, no quadtree and nothing else the game thread may be mutating.
// That is the whole thread-safety argument, and keeping it a free function is
// what keeps the argument checkable by looking at the signature.

#pragma once

#include "CoreMinimal.h"

struct FLedgerPatchJob;

/// Generates the land and water geometry for a patch, in place.
LEDGERTERRAIN_API void LedgerGeneratePatch(FLedgerPatchJob& Job);
