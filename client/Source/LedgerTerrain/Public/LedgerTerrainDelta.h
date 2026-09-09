// Ground that has been changed, as a sparse delta over the generated field.
// T062, Living World §7.4.
//
// **Not a heightmap.** Storing modified ground as samples means storing them at
// some resolution, and the terrain has no single resolution -- it has fifteen.
// A delta stored per texel is wrong at every LOD but one and enormous at the
// finest. So an edit is stored as what it *is*: a place, a radius, and a height
// to bring the ground to. Evaluating it is a distance and a lerp, and it is
// correct at every LOD because it is not a sampling of anything.
//
// **And it costs nothing where nothing was modified.** The edits are bucketed
// on a coarse cube-sphere lattice; a height sample looks up one bucket and, on
// a planet nobody has dug, finds an empty map and returns. That is the whole of
// the second half of the acceptance, and it is a property of the data structure
// rather than a fast path somebody has to remember to take.

#pragma once

#include "CoreMinimal.h"
#include "LedgerTerrainMath.h"

/// One thing somebody did to the ground.
struct FLedgerTerrainEdit
{
	/// Where, as a unit vector on the sphere.
	FVector3d Centre = FVector3d(1.0, 0.0, 0.0);

	/// The flat part, in metres.
	double RadiusMetres = 0.0;

	/// How far past that the ground blends back to what it was. Without it a
	/// levelled pad is a cylinder punched into a hillside with a vertical wall
	/// round it, which is not what levelling ground looks like.
	double FalloffMetres = 0.0;

	/// What the ground is brought to inside the radius, metres above sea level.
	double TargetAltitudeMetres = 0.0;
};

/// Every edit on a planet.
///
/// Immutable once published. The terrain is sampled on worker threads while the
/// game thread may be adding an edit, so a change replaces the whole thing
/// behind a shared pointer rather than mutating one somebody is reading.
class LEDGERTERRAIN_API FLedgerTerrainDelta
{
public:
	/// Cells across a cube face for the bucket index. 64 is about 156 km a
	/// cell, which is far larger than any edit and small enough that a planet
	/// with thousands of building sites still looks at a handful each sample.
	static constexpr int32 BucketResolution = 64;

	bool IsEmpty() const { return Edits.Num() == 0; }
	int32 Num() const { return Edits.Num(); }
	const TArray<FLedgerTerrainEdit>& All() const { return Edits; }

	/// A copy of this delta with one more edit in it.
	TSharedRef<const FLedgerTerrainDelta> With(const FLedgerTerrainEdit& Edit) const;

	/// The altitude the ground should be at, given what it was generated at.
	/// Returns `BaseAltitudeMetres` untouched where nothing applies.
	double Apply(const FVector3d& UnitSphere, double BaseAltitudeMetres) const;

	/// JSON, one object per edit. Round-trips exactly.
	FString ToJson() const;
	static TSharedRef<const FLedgerTerrainDelta> FromJson(const FString& Json);

	/// Where a planet's edits live. Under Saved, because they are what this
	/// player did rather than what the project ships.
	static FString DefaultPath();

private:
	void Index();

	TArray<FLedgerTerrainEdit> Edits;

	/// Bucket -> indices into Edits. An edit lands in every bucket its radius
	/// plus falloff can reach, so a lookup never has to consider neighbours.
	TMap<int32, TArray<int32>> ByBucket;
};
