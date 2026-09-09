// Caves, as the one thing a height field cannot say. T056, §6.8.
//
// **A separate field, not a change to the height function.** Elevation is a
// function of direction, so it has exactly one answer per direction and can
// never describe a passage with ground above and below it. Caves are therefore
// a second, volumetric field, and the terrain surface consults it only to know
// where to leave a hole.
//
// **Sparse on purpose, and sparse by construction.** A tunnel is the
// intersection of two noise level sets -- where two independent 3D fields are
// both near zero, which in three dimensions is a curve rather than a region.
// That gives passages rather than caverns without any carving, culling or
// post-processing, and it makes "is there a cave anywhere near here" a cheap
// question with a mostly-no answer.
//
// The field is only defined in a shell around the surface. Below it there is no
// point meshing anything nobody can reach, and above it there is no rock.

#pragma once

#include "CoreMinimal.h"
#include "LedgerTerrainMath.h"

namespace LedgerCaves
{
	/// How far above and below the surface caves may run, in metres. A passage
	/// has to be able to breach, or there are no mouths and the whole thing is
	/// a sealed void nobody will ever see.
	constexpr double ShellAboveMetres = 60.0;
	constexpr double ShellBelowMetres = 600.0;

	/// Metres between one tunnel and the next, roughly. Not a spacing that is
	/// enforced -- it is the wavelength of the noise the level sets are taken
	/// from, so it sets the scale rather than a guarantee.
	constexpr double TunnelSpacingMetres = 2200.0;

	/// Passage radius at the widest, in metres.
	constexpr double PassageRadiusMetres = 14.0;

	/// How much of the planet has caves under it at all, as a fraction. The
	/// region mask keeps them in cave country instead of putting a tunnel under
	/// every hill, which is both cheaper and more interesting.
	constexpr double CaveCountryFraction = 0.22;

	/// Positive inside a cave, negative in rock, in metres.
	///
	/// This is the whole field. The surface reads its sign to know whether to
	/// leave a triangle out, and the mesher reads its value to find the wall.
	///
	/// **Both altitudes, and the ground one is not optional.** The shell is
	/// measured from the ground, and the tunnels are positioned by absolute
	/// altitude so a passage runs level instead of following the hillside. The
	/// first version took only the absolute altitude and treated it as a depth,
	/// which put the whole shell at sea level: on ground 197 m up, the survey
	/// dug its box two hundred metres below the surface and found a passage
	/// with no mouth. Asking the caller for the ground costs nothing -- it
	/// always has it -- and hiding an `Elevation` call in here would cost the
	/// most expensive thing in the project, per sample.
	LEDGERTERRAIN_API double Density(
		const FVector3d& UnitSphere, double AltitudeMetres, double GroundMetres,
		const FLedgerTerrainParams& Params);

	/// Whether a cave could possibly exist anywhere in a ball of this radius
	/// around a point. False is a promise; true is only a suspicion.
	///
	/// The reason the volumetric layer costs nothing where there are no caves:
	/// a patch that fails this is never sampled again.
	LEDGERTERRAIN_API bool MightContainCaves(
		const FVector3d& UnitSphere, double RadiusMetres,
		const FLedgerTerrainParams& Params);
}
