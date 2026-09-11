#include "LedgerEnvironment.h"

#include "Engine/World.h"
#include "LedgerAir.h"
#include "LedgerClimate.h"
#include "LedgerCloud.h"
#include "LedgerPlanet.h"
#include "LedgerWeather.h"
#include "LedgerWorld.h"

namespace LedgerEnvironment
{
	FLedgerAirHere At(const UWorld* World, const FVector& WorldPosition)
	{
		FLedgerAirHere Out;
		ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
		ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
		if (Builder == nullptr || Planet == nullptr)
		{
			return Out;
		}
		const FLedgerSystem& System = Builder->GetSystem();
		const int32 Home = Builder->GetHomeBodyIndex();
		const double When = Builder->GetWhenSeconds();
		const FLedgerAirProfile Air = LedgerAir::For(System, Home, When);

		const FVector3d Offset = FVector3d(WorldPosition) - FVector3d(Planet->GetActorLocation());
		const double DistanceCm = Offset.Length();
		if (!(DistanceCm > 0.0))
		{
			return Out;
		}
		const FVector3d Up = Offset / DistanceCm;
		const double Latitude = FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0));
		const double Longitude = FMath::Atan2(Up.Y, Up.X);

		Out.AltitudeMetres = (DistanceCm - Planet->Radius) / 100.0;
		const double GroundMetres = (Planet->SurfaceRadiusAt(Up) - Planet->Radius) / 100.0;
		// Below the ground -- a cave -- is the ground's temperature.
		Out.AboveGroundMetres = FMath::Max(Out.AltitudeMetres - GroundMetres, 0.0);

		Out.GroundKelvin = LedgerClimate::At(
			Up, Planet->TerrainParams(), Planet->SeasonPhase()).TemperatureC + 273.15;
		Out.LapseKelvinPerMetre = Air.HasAir() ? LedgerCloud::EnvironmentalLapseRate(Air) : 0.0;
		// Down to the tropopause, and no further: above it the air stops
		// cooling, and a lapse rate run on to orbit is negative kelvin.
		// ponytail: an isothermal stratosphere at three quarters of the datum
		// temperature, which is Earth's 217 K over 288; no ozone warming above.
		Out.Kelvin = FMath::Max(Out.GroundKelvin - Out.LapseKelvinPerMetre * Out.AboveGroundMetres,
			Air.HasAir() ? 0.75 * Air.SurfaceTemperatureKelvin : 0.0);

		if (Air.HasAir() && Air.ScaleHeightMetres > 0.0 && Air.SurfacePressurePascals > 0.0)
		{
			Out.SeaLevelPascals = LedgerWeather::PressureAt(
				System, Home, Air, Latitude, Longitude, When);
			Out.Pascals = Out.SeaLevelPascals * FMath::Exp(-Out.AltitudeMetres / Air.ScaleHeightMetres);
			// The profile's density, carried up or down by how far the weather
			// has moved the pressure off the datum's.
			Out.DensityKgPerM3 = LedgerAir::DensityAt(Air, Out.AltitudeMetres)
				* Out.SeaLevelPascals / Air.SurfacePressurePascals;
		}
		Out.bValid = true;
		return Out;
	}
}
