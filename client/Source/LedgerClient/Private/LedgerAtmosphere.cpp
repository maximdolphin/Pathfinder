#include "LedgerAtmosphere.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "LedgerLog.h"

namespace
{
	/// Unreal's sky and cloud components work in kilometres; everything else in
	/// this project is centimetres. One place to get that wrong, so one place to
	/// convert.
	constexpr double CentimetresPerKilometre = 100000.0;
}

ALedgerAtmosphere::ALedgerAtmosphere()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	Atmosphere->SetupAttachment(Root);

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("VolumetricCloud"));
	Clouds->SetupAttachment(Root);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	Fog->SetupAttachment(Root);
}

void ALedgerAtmosphere::BeginPlay()
{
	Super::BeginPlay();
}

void ALedgerAtmosphere::ConfigureForPlanet(double PlanetRadiusCm, double MaxElevationCm)
{
	const float RadiusKm = static_cast<float>(PlanetRadiusCm / CentimetresPerKilometre);

	if (Atmosphere != nullptr)
	{
		// The default mode puts the planet's *top* at the world origin, which is
		// right for a flat level and wrong for an actual sphere. We want the
		// planet centre where this actor is — which is where the terrain's
		// centre is too.
		Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
		Atmosphere->BottomRadius = RadiusKm;
		Atmosphere->AtmosphereHeight = AtmosphereHeightKm;

		// At Earth's radius these are simply Earth's numbers, and Sky Atmosphere
		// is a Bruneton model tuned for exactly them. Every previous round of
		// fighting these values was a consequence of a 60 km planet carrying a
		// proportionally impossible 5 km atmosphere — the physics had no
		// self-consistent setting, so there was nothing to find.
		//
		// Rayleigh scale height 8 km, Mie 1.2 km: the real ones.
		Atmosphere->RayleighExponentialDistribution = 8.0f;
		Atmosphere->MieExponentialDistribution = 1.2f;

		// Optical depth is coefficient times path length, and the path through
		// this atmosphere is a twelfth of Earth's — so the coefficient has to go
		// *up* for the sky to be blue from the ground, not down.
		//
		// Both ends of this were wrong once. At 0.05 with a dense height fog on
		// top, everything past a few kilometres washed out to flat pale green
		// and looked like an unlit material. At 0.012 the sky went black at
		// ground level. The fog was most of the first problem; this is the
		// setting for the second.
		Atmosphere->RayleighScatteringScale = 0.0331f;
		Atmosphere->MieScatteringScale = 0.003996f;
		Atmosphere->MieAbsorptionScale = 0.000444f;
		Atmosphere->MieAnisotropy = 0.8f;
		// Ozone. It is why the sky goes deep blue at the zenith and why the
		// twilight band above the limb is violet rather than grey — the layer
		// absorbs where Rayleigh does not, and leaving it out gives an
		// atmosphere that is technically scattering and visually flat.
		Atmosphere->OtherAbsorptionScale = 0.001881f;
		Atmosphere->MultiScatteringFactor = 1.0f;
		Atmosphere->AerialPespectiveViewDistanceScale = 1.0f;
		Atmosphere->HeightFogContribution = 1.0f;
		Atmosphere->MarkRenderStateDirty();
	}

	if (Clouds != nullptr)
	{
		// Altitudes are above the *ground*, so they must clear the tallest
		// terrain or the clouds render inside mountains.
		const float TerrainTopKm = static_cast<float>(MaxElevationCm / CentimetresPerKilometre);
		Clouds->LayerBottomAltitude = FMath::Max(CloudBaseAltitudeKm, TerrainTopKm * 1.1f);
		Clouds->LayerHeight = CloudLayerHeightKm;
		// **These two numbers were 68% of the frame.**
		//
		// A sample scale of 2.0 against a 400 km tracing distance is twice the
		// engine's sample count over eight times its distance, and the cost is
		// the product: 90 ms of a 130 ms frame, spent on a 334x188 buffer. It
		// was invisible because a screenshot does not have a frame time in it,
		// which is why the flight now records one.
		//
		// The distance was set to cover the horizon so the deck would not end
		// in mid-air on approach. It still does not: from a kilometre up the
		// horizon is 113 km, and past that the deck is a band a few pixels
		// high that the atmosphere is already fading out.
		// Second pass: 0.8 over 100 km still cost 32 ms. The deck is a texture
		// on the sky from more than a few tens of kilometres out, and paying
		// full sample counts to resolve it there buys nothing a person can see.
		Clouds->ViewSampleCountScale = 0.35f;
		Clouds->ShadowViewSampleCountScale = 0.35f;
		Clouds->PlanetRadius = RadiusKm;
		Clouds->TracingMaxDistance = 55.0f;
		Clouds->MarkRenderStateDirty();
	}

	if (Fog != nullptr)
	{
		// Aerial perspective near the ground. Volumetric so the sun shafts
		// through the cloud deck on the way down, which is most of what sells
		// the descent.
		// Very light. Sky Atmosphere already supplies aerial perspective at this
		// scale; the fog is here only for volumetric shafts through the cloud
		// deck, and stacking a dense one on top is what buried the terrain
		// before.
		// **Off.** Exponential height fog is a flat-world approximation: its
		// density is a function of absolute Z against an infinite horizontal
		// plane. On a sphere that plane cuts through the planet, and from orbit
		// it fills space itself — which is why the sky outside the atmosphere
		// came back navy instead of black.
		//
		// Sky Atmosphere already provides aerial perspective, and unlike the fog
		// it is spherical and knows where the ground is. The component is kept so
		// the shape of the decision is visible rather than mysteriously absent.
		Fog->SetFogDensity(0.0f);
		Fog->SetVolumetricFog(false);
		Fog->SetVisibility(false);
		Fog->MarkRenderStateDirty();
	}

	UE_LOG(LogLedger, Log,
		TEXT("atmosphere configured: planet %.1f km, air %.1f km, clouds %.1f-%.1f km"),
		RadiusKm,
		AtmosphereHeightKm,
		Clouds != nullptr ? Clouds->LayerBottomAltitude : 0.0f,
		Clouds != nullptr ? Clouds->LayerBottomAltitude + Clouds->LayerHeight : 0.0f);
}
