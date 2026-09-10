// Atmosphere, clouds and the orbit-to-surface transition. Design §6.8.
//
// §6.8 is right that this is mostly configuration: Sky Atmosphere is a
// Bruneton-style physically-based model parameterised by planet radius and
// atmosphere height, explicitly built for ground *and* orbital viewing. The
// most visually convincing part of reentry is a component we configure, not one
// we build. The work is getting the parameters right for a planet that is not
// Earth-sized, and making sure nothing in the chain assumes it is.
//
// **There is no loading screen and there is nothing to hide.** Orbit and ground
// are the same world, the same actor, and the same coordinate space; the
// "transition" is a camera moving through an atmosphere that was always there.
// That is a consequence of the architecture, not a feature bolted onto it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LedgerAir.h"

#include "LedgerAtmosphere.generated.h"

class UExponentialHeightFogComponent;
class USkyAtmosphereComponent;
class UVolumetricCloudComponent;

/// Everything that makes the planet look like it has air around it.
///
/// Separate from `ALedgerPlanet` because the terrain and the sky have entirely
/// different reasons to change (§8.3) — one is a mesh streaming problem, the
/// other is a rendering configuration problem.
UCLASS()
class LEDGERCLIENT_API ALedgerAtmosphere : public AActor
{
	GENERATED_BODY()

public:
	ALedgerAtmosphere();

	virtual void BeginPlay() override;

	/// Configures every component from the body's own air. T089.
	///
	/// **Nothing in here is a setting any more.** The scale height, the
	/// scattering coefficients, the ozone layer and whether there is a cloud
	/// deck at all arrive in the profile, computed from what the gas is and how
	/// much of it there is. What used to be four tuned floats with a paragraph
	/// each defending them is now one row of a table, and the row for a
	/// carbon-dioxide world is not the row for this one.
	void ConfigureForAir(double PlanetRadiusCm, double MaxElevationCm,
		const FLedgerAirProfile& Air);

private:
	UPROPERTY()
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY()
	TObjectPtr<UVolumetricCloudComponent> Clouds;

	UPROPERTY()
	TObjectPtr<UExponentialHeightFogComponent> Fog;
};
