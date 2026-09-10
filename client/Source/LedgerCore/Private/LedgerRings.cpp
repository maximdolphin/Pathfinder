#include "LedgerRings.h"

#include "LedgerEphemeris.h"
#include "LedgerMath.h"
#include "LedgerSky.h"

namespace
{
	/// Water ice, which is what most ring material is.
	constexpr double IceDensityKgPerM3 = 920.0;

	double DensityOf(const FLedgerBody& Body)
	{
		if (!(Body.RadiusMetres > 0.0))
		{
			return 0.0;
		}
		const double Volume = (4.0 / 3.0) * LedgerPi
			* Body.RadiusMetres * Body.RadiusMetres * Body.RadiusMetres;
		return Body.MassKg / Volume;
	}

	/// The resonances that clear a gap, as ratios of a moon's period.
	///
	/// The strong low-order ones. A particle at the 2:1 gets the same kick at
	/// the same place every second orbit, and after enough of them it is
	/// somewhere else -- which is what a gap is.
	constexpr double GapResonances[] = { 2.0, 3.0 / 2.0, 5.0 / 3.0, 3.0, 7.0 / 3.0 };

	/// How wide a gap a resonance clears, as a fraction of its radius.
	constexpr double GapHalfWidth = 0.012;
}

namespace LedgerRings
{
	double RocheLimitMetres(const FLedgerBody& Primary, double SatelliteDensityKgPerM3)
	{
		const double PrimaryDensity = DensityOf(Primary);
		if (!(PrimaryDensity > 0.0) || !(SatelliteDensityKgPerM3 > 0.0))
		{
			return 0.0;
		}
		return 2.44 * Primary.RadiusMetres
			* FMath::Pow(PrimaryDensity / SatelliteDensityKgPerM3, 1.0 / 3.0);
	}

	FLedgerRings For(const FLedgerSystem& System, int32 BodyIndex)
	{
		FLedgerRings Rings;
		if (!System.Bodies.IsValidIndex(BodyIndex))
		{
			return Rings;
		}
		const FLedgerBody& Body = System.Bodies[BodyIndex];
		Rings.BodyIndex = BodyIndex;

		// The outer edge is where ice stops being pulled apart, because beyond
		// it the rubble would have collected into a moon long ago.
		const double Outer = RocheLimitMetres(Body, IceDensityKgPerM3);

		// The inner edge is a little above the body itself: closer than that
		// and the ring is inside the atmosphere, where drag brings it down in
		// far less than the age of a system. A fifth of a radius of clearance,
		// which is about where Saturn's D ring gives out.
		const double Inner = Body.RadiusMetres * 1.2;

		// **The Roche limit decides how far a ring reaches, not whether there is
		// one, and that correction came from a failing test.**
		//
		// The first version refused rings to rocky bodies on the grounds that
		// their Roche limit sits barely outside their surface. It does not: the
		// limit scales with the CUBE ROOT OF THE PRIMARY'S DENSITY, so a dense
		// rocky planet's limit for icy rubble is 4.4 of its radii while a
		// puffy gas giant's is under 2. Rock has MORE room for rings than gas
		// does, and the test said so with the numbers side by side.
		//
		// The real reason the inner planets have none is supply. Ring material
		// is ice, ice is not solid inside the frost line, and what little
		// arrives there is swept up or dragged down long before anybody looks.
		// So the condition is about where the body is and what it is, which is
		// a formation question -- and the Roche limit does the job it actually
		// does, which is setting the outer edge.
		const bool bIcyRegion = Body.Kind == ELedgerBodyKind::GasGiant
			|| Body.Orbit.SemiMajorAxisMetres > 3.0 * 1.495978707e11;
		if (!bIcyRegion || !(Outer > Inner * 1.15))
		{
			Rings.InnerRadiusMetres = 0.0;
			Rings.OuterRadiusMetres = 0.0;
			return Rings;
		}

		Rings.InnerRadiusMetres = Inner;
		Rings.OuterRadiusMetres = Outer;
		Rings.PeakOpticalDepth = 1.2;
		return Rings;
	}

	bool InGap(
		const FLedgerRings& Rings, const FLedgerSystem& System, double RadiusMetres)
	{
		if (!System.Bodies.IsValidIndex(Rings.BodyIndex) || !(RadiusMetres > 0.0))
		{
			return false;
		}
		const FLedgerBody& Body = System.Bodies[Rings.BodyIndex];

		// Any moon of this body is a shepherd. Its resonances clear gaps at the
		// radii whose periods are simple ratios of its own -- and since period
		// goes as the three-halves power of radius, a period ratio of N is a
		// radius ratio of N to the two-thirds.
		for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
		{
			if (System.Bodies[Index].ParentIndex != Rings.BodyIndex)
			{
				continue;
			}
			const double MoonAxis = System.Bodies[Index].Orbit.SemiMajorAxisMetres;
			if (!(MoonAxis > 0.0))
			{
				continue;
			}
			for (const double Ratio : GapResonances)
			{
				const double GapRadius = MoonAxis * FMath::Pow(1.0 / Ratio, 2.0 / 3.0);
				if (FMath::Abs(RadiusMetres - GapRadius) < GapRadius * GapHalfWidth)
				{
					return true;
				}
			}
		}
		(void)Body;
		return false;
	}

	double OpticalDepthAt(
		const FLedgerRings& Rings, const FLedgerSystem& System, double RadiusMetres)
	{
		if (!(Rings.OuterRadiusMetres > Rings.InnerRadiusMetres))
		{
			return 0.0;
		}
		if (RadiusMetres < Rings.InnerRadiusMetres
			|| RadiusMetres > Rings.OuterRadiusMetres)
		{
			return 0.0;
		}
		if (InGap(Rings, System, RadiusMetres))
		{
			return 0.0;
		}

		// Denser in the middle, thinning towards both edges. Real rings are
		// lumpier than this, but the shape -- a thick core with faint skirts --
		// is what decides how a shadow looks.
		const double Across = (RadiusMetres - Rings.InnerRadiusMetres)
			/ (Rings.OuterRadiusMetres - Rings.InnerRadiusMetres);
		const double Shape = FMath::Sin(Across * LedgerPi);
		return Rings.PeakOpticalDepth * Shape * Shape;
	}

	double ShadowAt(
		const FLedgerRings& Rings, const FLedgerSystem& System,
		const FVector3d& SurfaceDirection, double SecondsFromEpoch)
	{
		if (!(Rings.OuterRadiusMetres > Rings.InnerRadiusMetres)
			|| !System.Bodies.IsValidIndex(Rings.BodyIndex))
		{
			return 0.0;
		}
		const FLedgerBody& Body = System.Bodies[Rings.BodyIndex];

		const FVector3d Surface = SurfaceDirection.GetSafeNormal();
		if (Surface.IsNearlyZero())
		{
			return 0.0;
		}

		// Where the star is, in the frame the ring lies flat in.
		const FVector3d ToStar =
			LedgerSky::SunDirectionInBody(System, Rings.BodyIndex, SecondsFromEpoch);
		if (ToStar.IsNearlyZero())
		{
			return 0.0;
		}

		// Standing on the far side from the star is night, and night is not a
		// ring shadow.
		if (FVector3d::DotProduct(Surface, ToStar) <= 0.0)
		{
			return 0.0;
		}

		// The ring is the plane z = 0 in this frame, so the ray from the point
		// towards the star crosses it at one place, and the only question is
		// whether that place is inside the annulus.
		const FVector3d Point = Surface * Body.RadiusMetres;
		if (FMath::Abs(ToStar.Z) < 1e-12)
		{
			// The star is exactly in the ring plane. The shadow is a line of
			// zero width, which is what an equinox looks like on Saturn.
			return 0.0;
		}

		const double Along = -Point.Z / ToStar.Z;
		if (Along <= 0.0)
		{
			// The crossing is behind the observer: the ring is on the other
			// side of the body from the star, so it shades nothing here.
			return 0.0;
		}

		const FVector3d Crossing = Point + ToStar * Along;
		const double Radius = FMath::Sqrt(
			Crossing.X * Crossing.X + Crossing.Y * Crossing.Y);

		const double Depth = OpticalDepthAt(Rings, System, Radius);
		if (!(Depth > 0.0))
		{
			return 0.0;
		}

		// Beer's law along the slanted path. Light crossing the ring at a
		// grazing angle goes through more of it, which is why a ring shadow is
		// darkest where the sun is lowest.
		const double Slant = FMath::Max(FMath::Abs(ToStar.Z), 1e-6);
		return 1.0 - FMath::Exp(-Depth / Slant);
	}
}
