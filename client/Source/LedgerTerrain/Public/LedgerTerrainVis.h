// The terrain, coloured by what it is doing. T066.
//
// Every terrain bug this project has had was found by adding code: a log line,
// a counter, a fixture, and in the worst cases four of them in a row that each
// measured the wrong thing. The acceptance for this task is that the next one
// is diagnosable without any of that.
//
// `-terrainvis=<mode>` replaces the ground's colour with a diagnostic and
// draws it unlit, so what is on screen is the number and not the number times
// a sun angle. It is written into the patch at generation time, which is the
// only place that knows most of these -- whether the patch came off disk,
// whether it was asked for with collision, how deep it is -- and none of which
// survive into the material.
//
//   lod         depth as a ramp, blue at the root through to red at the finest
//   patch       a hash colour per patch, so every boundary is a hard edge
//   collision   green where a body could stand, red where it would fall through
//   biome       the three palette weights as red, green and blue
//   climate     temperature in red, moisture in blue
//   cache       blue served from disk, orange generated this run
//
// The counters that are not per-vertex -- the streaming queue, the section
// pool, the LOD brake -- stay in ALedgerPlanet::LogStats, because a number that
// is one number is a worse picture than a sentence.

#pragma once

#include "CoreMinimal.h"

struct FLedgerPatchJob;

enum class ELedgerTerrainVis : uint8
{
	Off,
	Lod,
	Patch,
	Collision,
	Biome,
	Climate,
	Cache,
};

namespace LedgerTerrainVis
{
	/// Parsed once from the command line. Off unless `-terrainvis=` names a
	/// mode; an unrecognised name logs what the choices are rather than
	/// silently rendering the game.
	LEDGERTERRAIN_API ELedgerTerrainVis Mode();

	/// True when anything is being visualised, which is the check both the
	/// generator and the material make.
	inline bool IsOn() { return Mode() != ELedgerTerrainVis::Off; }

	/// Overwrites the patch's vertex colours with the current mode's
	/// diagnostic. Called last in generation, after the disk store, so what is
	/// cached is the real patch.
	LEDGERTERRAIN_API void Paint(FLedgerPatchJob& Job, bool bFromDisk);
}
