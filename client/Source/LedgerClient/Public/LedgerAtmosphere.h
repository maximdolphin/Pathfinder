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
#include "LedgerCloud.h"

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

	/// Put the deck where the thermometer says and cover as much of the sky as
	/// the weather does. T094.
	///
	/// Called every time the clock moves rather than once: coverage is a
	/// function of the pressure overhead, and the pressure overhead is a
	/// function of time.
	void SetDecks(const FLedgerCloudDecks& Decks);

	/// **The sun as the viewer sees it, for everything the engine lights with
	/// one number.** Unreal evaluates an atmosphere sun light's global
	/// transmittance as if the camera stood at the planet's north pole --
	/// GetTransmittanceAtGroundLevel, with a TODO to make it work anywhere else
	/// -- and fog, translucency, Lumen's direct lighting and forward shading
	/// all use that number. Its elevation is floored at
	/// TransmittanceMinLightElevationAngle, so setting that to the sun's true
	/// elevation at the viewer makes the pole's answer the viewer's whenever
	/// the pole sees a lower sun. ponytail: a floor, not a fix -- when the pole
	/// sees the sun HIGHER than the viewer the global number stays the pole's;
	/// the complete answer is a world frame whose +Z is the viewer's up.
	void SetSunElevationFloorDegrees(double Degrees);

	/// Where the viewer is against the cloud layer, metres above sea level.
	/// Inside it the tracing distance comes down: see the definition.
	void SetViewerAltitude(double MetresAboveSea);

	/// Volumetric fog to this distance with the height fog's own density at
	/// zero, for a ground fog volume to voxelise into. T091.
	void UseVolumetricFogOnly(double DistanceMetres);
	/// And off again: the height fog hidden, as ConfigureForAir leaves it.
	void StopVolumetricFog();

private:
	UPROPERTY()
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY()
	TObjectPtr<UVolumetricCloudComponent> Clouds;

	UPROPERTY()
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	/// An instance of whatever material the cloud component came with, so its
	/// coverage can be driven without owning a material asset.
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> CloudMaterial;

	double LastCoverage = -1.0;

	/// The one volumetric layer's extent, from SetDecks, and whether the
	/// tracing distance is currently the short, in-layer one.
	double LayerBottomMetres = 0.0;
	double LayerTopMetres = 0.0;
	bool bTracingInside = false;

	/// Non-zero once a ground fog has asked for volumetric fog.
	double VolumetricOnlyMetres = 0.0;
};
