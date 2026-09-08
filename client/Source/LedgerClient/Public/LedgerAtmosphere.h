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
#include "LedgerAtmosphere.generated.h"

class UExponentialHeightFogComponent;
class USkyAtmosphereComponent;
class UVolumetricCloudComponent;
class UMaterialInterface;

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

	/// Configures every component against the planet's actual radius. Called by
	/// the world builder once the planet exists, because every altitude below is
	/// meaningful only relative to it.
	void ConfigureForPlanet(double PlanetRadiusCm, double MaxElevationCm);

	/// Atmosphere thickness in kilometres.
	///
	/// Earth's is about 1% of its radius. On a 60 km planet that would be 600 m
	/// — physically consistent and visually nothing. This is deliberately
	/// exaggerated, the same way the terrain's relief is, because the point of
	/// the spike is to see whether reentry reads. Scale it back when the planet
	/// scales up.
	UPROPERTY(EditAnywhere, Category = "Ledger|Atmosphere")
	float AtmosphereHeightKm = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Ledger|Clouds")
	float CloudBaseAltitudeKm = 1.1f;

	UPROPERTY(EditAnywhere, Category = "Ledger|Clouds")
	float CloudLayerHeightKm = 1.6f;

private:
	UPROPERTY()
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY()
	TObjectPtr<UVolumetricCloudComponent> Clouds;

	UPROPERTY()
	TObjectPtr<UExponentialHeightFogComponent> Fog;
};

namespace LedgerMaterials
{
	/// A lit surface material driven by vertex colour.
	///
	/// The debug vertex-colour material the terrain used before this is
	/// **unlit**, which is why the planet read as a flat cut-out: the normals
	/// the LOD works so hard to get right were not shading anything. This builds
	/// the smallest material that fixes that.
	///
	/// Editor-only: shaders compile in an editor build, and a cooked game needs
	/// a real asset instead. That is a known ceiling, not an oversight.
	LEDGERCLIENT_API UMaterialInterface* CreateTerrainMaterial(UObject* Outer);
}
