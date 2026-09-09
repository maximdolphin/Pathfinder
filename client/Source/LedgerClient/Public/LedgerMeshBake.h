// Generated geometry, saved as a static mesh asset.
//
// ADR-0006. A procedural mesh component has no Nanite, no LOD chain, no mesh
// distance field and no instancing; the same triangles saved as a static mesh
// have all four for free. This is the one-way door between the two.

#pragma once

#include "CoreMinimal.h"

struct FLedgerMeshBuilder;
class UStaticMesh;

namespace LedgerMesh
{
	/// Where baked meshes live. One place, derived from, never typed twice.
	inline constexpr const TCHAR* MeshPackageRoot = TEXT("/Game/Meshes/");

#if WITH_EDITOR
	/// Builds and saves one static mesh from accumulated geometry.
	///
	/// `Line` comes back describing what was produced -- triangle count, and
	/// whether Nanite, the LOD chain, collision and the distance field are
	/// actually on it. Reported rather than assumed, because "I set the flag" and
	/// "the asset has it" are different claims and only the second one matters.
	/// `bNanite` is a choice, not a default. Nanite is for meshes dense enough
	/// that per-cluster detail selection beats drawing the whole thing, and it
	/// does not carry mesh vertex colours through to the material -- which on a
	/// forty-six triangle tree costs the only colour it has and buys nothing.
	LEDGERCLIENT_API UStaticMesh* Bake(
		const FLedgerMeshBuilder& Builder,
		const FString& PackageName,
		const TCHAR* AssetName,
		FString& Line,
		bool bNanite = true,
		int32 SecondGroupStartTriangle = INDEX_NONE);
#endif
}
