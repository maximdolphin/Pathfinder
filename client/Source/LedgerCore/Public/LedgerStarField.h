// The stars behind everything else. T077.
//
// **A catalogue in three dimensions, not a picture on the inside of a sphere.**
// That distinction is the whole task. A skybox is the same from everywhere, so
// two systems light years apart would share a sky, and travelling between them
// would be a loading screen dressed as a journey. Stars with positions give the
// opposite for free: the constellations hold across a solar system, because a
// planet's orbit is nothing against a parsec, and they come apart between
// systems, because a parsec is not nothing against a parsec.
//
// Positions in parsecs, magnitudes on the usual backwards scale where smaller
// is brighter and five steps is a factor of a hundred.

#pragma once

#include "CoreMinimal.h"

/// One star, as catalogued: where it is and what it is.
struct FLedgerCatalogueStar
{
	/// Relative to the catalogue's origin, parsecs.
	FVector3d PositionParsecs = FVector3d::ZeroVector;

	/// What it would look like from ten parsecs away.
	double AbsoluteMagnitude = 0.0;

	/// Surface temperature, kelvin. Colour comes from this, as it does for the
	/// system's own star -- there is one rule for what a star looks like.
	double TemperatureKelvin = 5772.0;
};

/// The same star as seen from somewhere.
struct FLedgerSkyStar
{
	/// Which catalogue entry.
	int32 Index = INDEX_NONE;

	/// Unit vector towards it, in the catalogue's frame.
	FVector3d Direction = FVector3d::ZeroVector;

	/// How bright it looks from here.
	double ApparentMagnitude = 0.0;

	double TemperatureKelvin = 5772.0;
	double DistanceParsecs = 0.0;
};

namespace LedgerStarField
{
	/// The naked-eye limit on a dark night, near enough.
	constexpr double NakedEyeMagnitude = 6.5;

	/// A catalogue from a seed. Deterministic, like everything else generated.
	///
	/// Stars are spread uniformly through the volume rather than uniformly in
	/// direction, which is what makes distance mean something: most of them end
	/// up far away and faint, a few end up near and bright, and that
	/// distribution is what a constellation is made of.
	LEDGERCORE_API void Generate(
		uint32 Seed, int32 Count, double RadiusParsecs,
		TArray<FLedgerCatalogueStar>& Out);

	/// How bright a star looks from a place. The distance modulus.
	LEDGERCORE_API double ApparentMagnitude(
		const FLedgerCatalogueStar& Star, const FVector3d& ObserverParsecs);

	/// Everything brighter than a limit, seen from a place, brightest first.
	LEDGERCORE_API void Visible(
		const TArray<FLedgerCatalogueStar>& Catalogue,
		const FVector3d& ObserverParsecs, double MagnitudeLimit,
		TArray<FLedgerSkyStar>& Out);
}
