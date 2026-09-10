// What a gas giant looks like. T079.
//
// **The bands are a field, not a texture.** A function of latitude, longitude
// and time that anything can sample -- a material, a test, a chart -- rather
// than an image somebody painted. That is what lets the banding be checked
// rather than admired: a test can ask whether the zones alternate, whether the
// storms sit inside a belt, and whether the whole thing shears with latitude
// the way a rotating fluid must.
//
// Latitude and longitude in radians. Time in seconds, because bands drift.

#pragma once

#include "CoreMinimal.h"
#include "LedgerBody.h"

namespace LedgerGiant
{
	/// How fast the atmosphere runs at a latitude, relative to the body.
	///
	/// Alternating prograde and retrograde jets, strongest near the equator and
	/// dying towards the poles. This is what makes a gas giant look like a gas
	/// giant: the bands are not stripes, they are the boundaries between belts
	/// of air moving at different speeds, and everything drawn on them shears.
	LEDGERCORE_API double ZonalWindAt(double LatitudeRadians, uint32 Seed);

	/// Where in the band structure a latitude falls, -1 to 1.
	///
	/// Negative is a belt -- the darker, sinking, warmer lanes -- and positive
	/// is a zone. The sign is the thing a test can check, because a giant whose
	/// bands do not alternate is a beach ball.
	LEDGERCORE_API double BandAt(double LatitudeRadians, uint32 Seed);

	/// The full field: band, turbulence and storms, as a value from 0 to 1
	/// that a palette maps to colour.
	LEDGERCORE_API double SurfaceAt(
		double LatitudeRadians, double LongitudeRadians, uint32 Seed, double Seconds);

	/// How strongly a storm covers a point, 0 to 1.
	///
	/// Storms are long-lived ovals that sit in a belt and are carried around by
	/// its wind. They live at the shear line between two jets, which is where
	/// the real ones are, rather than wherever a noise function happened to
	/// put a blob.
	LEDGERCORE_API double StormAt(
		double LatitudeRadians, double LongitudeRadians, uint32 Seed, double Seconds);
}
