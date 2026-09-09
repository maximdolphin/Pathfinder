// The two ways a generated patch can become geometry the renderer will draw.
//
// **Why this is a seam and not a call.** UProceduralMeshComponent was the
// prototype's choice and was never compared against anything, which by T423 is
// not a decision — it is a default that survived. The alternative worth testing
// is a static mesh built at runtime, because everything the rest of this
// milestone gained by baking (Nanite, LODs, distance fields, the ordinary
// renderer's culling) is available to a UStaticMeshComponent and to nothing
// else. Putting both behind one interface is what lets the same scripted flight
// be run twice and the numbers put side by side.
//
// The measured answer is in docs/comparisons/terrain-component.md. This seam
// stays afterwards: it costs a virtual-free switch on an enum in a path that
// already spends milliseconds, and the next person asking the same question
// gets to answer it in an afternoon rather than a week.

#pragma once

#include "CoreMinimal.h"

struct FLedgerPatchJob;
class UMaterialInterface;
class UMeshComponent;

/// Which component type the planet's mesh pool is made of.
enum class ELedgerPatchComponent : uint8
{
	/// UProceduralMeshComponent. Interleaved buffers straight into a render
	/// resource, with an async collision cook.
	Procedural,

	/// A UStaticMeshComponent whose UStaticMesh is built per patch, per upload.
	Static,
};

namespace LedgerTerrain
{
	/// Reads `-terraincomponent=procedural|static` from the command line, so a
	/// comparison run is a command line and not a rebuild.
	LEDGERTERRAIN_API ELedgerPatchComponent PatchComponentKind();

	LEDGERTERRAIN_API const TCHAR* PatchComponentName(ELedgerPatchComponent Kind);

	/// A pooled, initially hidden component of the requested type, registered
	/// on the owner.
	LEDGERTERRAIN_API UMeshComponent* MakePatchComponent(
		AActor& Owner, ELedgerPatchComponent Kind);

	/// Puts a finished patch into a pooled component. Section 0 is land,
	/// section 1 the sea.
	LEDGERTERRAIN_API void UploadPatch(
		UMeshComponent& Mesh, FLedgerPatchJob& Job,
		UMaterialInterface* Land, UMaterialInterface* Water);

	/// Empties a component without destroying it, so the pool can hand it out
	/// again.
	LEDGERTERRAIN_API void ClearPatch(UMeshComponent& Mesh);
}
