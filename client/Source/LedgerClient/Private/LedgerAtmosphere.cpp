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

void ALedgerAtmosphere::ConfigureForAir(
	double PlanetRadiusCm, double MaxElevationCm, const FLedgerAirProfile& Air)
{
	const float RadiusKm = static_cast<float>(PlanetRadiusCm / CentimetresPerKilometre);
	const bool bHasAir = Air.HasAir() && Air.ScaleHeightMetres > 0.0;

	if (Atmosphere != nullptr)
	{
		// The default mode puts the planet's *top* at the world origin, which is
		// right for a flat level and wrong for an actual sphere. We want the
		// planet centre where this actor is -- which is where the terrain's
		// centre is too.
		Atmosphere->TransformMode =
			ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
		Atmosphere->BottomRadius = RadiusKm;
		Atmosphere->SetVisibility(bHasAir);

		if (bHasAir)
		{
			// Sky Atmosphere works in kilometres and per-kilometre coefficients.
			// The profile is in metres and per metre, because that is what the
			// physics is written in; the conversion happens once, here.
			Atmosphere->AtmosphereHeight =
				static_cast<float>(Air.TopMetres / 1000.0);
			Atmosphere->RayleighExponentialDistribution =
				static_cast<float>(Air.ScaleHeightMetres / 1000.0);
			Atmosphere->MieExponentialDistribution =
				static_cast<float>(FMath::Max(Air.MieScaleHeightMetres, 1.0) / 1000.0);

			// **The colour is the shape and the scale is the strength.** Unreal
			// multiplies one by the other, so the three coefficients are
			// normalised against their largest and the largest becomes the
			// scale. Doing it the other way round -- three absolute numbers in
			// the colour and a scale of one -- clips, because the colour is a
			// linear colour and the blue channel of a dense atmosphere is not
			// in [0, 1].
			const FVector3d PerKm = Air.RayleighPerMetre * 1000.0;
			const double Largest = FMath::Max3(PerKm.X, PerKm.Y, PerKm.Z);
			if (Largest > 0.0)
			{
				Atmosphere->RayleighScattering = FLinearColor(
					static_cast<float>(PerKm.Z / Largest),
					static_cast<float>(PerKm.Y / Largest),
					static_cast<float>(PerKm.X / Largest));
				Atmosphere->RayleighScatteringScale = static_cast<float>(Largest);
			}

			const double MiePerKm = Air.MiePerMetre * 1000.0;
			Atmosphere->MieScatteringScale = static_cast<float>(MiePerKm);
			// Aerosols absorb about a ninth of what they scatter. That ratio is
			// Earth's default and is a property of the particles rather than of
			// how many there are, so it rides along with the amount.
			Atmosphere->MieAbsorptionScale = static_cast<float>(MiePerKm * 0.111);
			Atmosphere->MieAnisotropy = 0.8f;

			// Ozone, and only where there is oxygen to make it from. It is why
			// Earth's zenith is deep blue and why the band above the limb at
			// twilight is violet rather than grey -- a carbon-dioxide sky has
			// neither, and now it does not get them.
			Atmosphere->OtherAbsorptionScale =
				static_cast<float>(Air.OzoneAbsorptionPerMetre * 1000.0);
			Atmosphere->MultiScatteringFactor = 1.0f;
			Atmosphere->AerialPespectiveViewDistanceScale = 1.0f;
			Atmosphere->HeightFogContribution = 1.0f;
		}
		Atmosphere->MarkRenderStateDirty();
	}

	if (Clouds != nullptr)
	{
		// **A deck only where something condenses.** An airless moon and a
		// carbon-dioxide world at 170 K have no water to make clouds out of,
		// and the component used to run on all three regardless.
		Clouds->SetVisibility(bHasAir && Air.bHasClouds);
		if (bHasAir && Air.bHasClouds)
		{
			// Altitudes are above the *ground*, so they must clear the tallest
			// terrain or the clouds render inside mountains.
			const float TerrainTopKm =
				static_cast<float>(MaxElevationCm / CentimetresPerKilometre);
			const float BaseKm = static_cast<float>(Air.CloudBaseMetres / 1000.0);
			Clouds->LayerBottomAltitude = FMath::Max(BaseKm, TerrainTopKm * 1.1f);
			Clouds->LayerHeight = FMath::Max(
				static_cast<float>((Air.CloudTopMetres - Air.CloudBaseMetres) / 1000.0),
				0.5f);

			// **A cautionary tale about measurement, kept because these numbers
			// are the evidence for it.**
			//
			// These were cut to 0.35 samples on the strength of a GPU profile
			// putting the cloud pass at 90 ms -- 68% of a 130 ms frame. The
			// profile was real. The machine was not idle: another game was
			// running behind the capture. On a quiet machine the *same*
			// settings cost 0.24 ms. A profile measures a machine, and a
			// profile of a machine with something else on it measures that.
			Clouds->ViewSampleCountScale = 1.2f;
			Clouds->ShadowViewSampleCountScale = 1.0f;
			Clouds->PlanetRadius = RadiusKm;
			Clouds->TracingMaxDistance = 250.0f;
		}
		Clouds->MarkRenderStateDirty();
	}

	if (Fog != nullptr)
	{
		// **Off.** Exponential height fog is a flat-world approximation: its
		// density is a function of absolute Z against an infinite horizontal
		// plane. On a sphere that plane cuts through the planet, and from orbit
		// it fills space itself -- which is why the sky outside the atmosphere
		// came back navy instead of black.
		//
		// Sky Atmosphere already provides aerial perspective, and unlike the fog
		// it is spherical and knows where the ground is. The component is kept
		// so the shape of the decision is visible rather than mysteriously
		// absent.
		Fog->SetFogDensity(0.0f);
		Fog->SetVolumetricFog(false);
		Fog->SetVisibility(false);
		Fog->MarkRenderStateDirty();
	}

	UE_LOG(LogLedger, Log,
		TEXT("atmosphere: %s, %.0f Pa at %.1f K, scale height %.2f km, top %.1f km, "
			 "Rayleigh %.4f/%.4f/%.4f per km, ozone %.5f, clouds %s"),
		LexToString(Air.Composition), Air.SurfacePressurePascals,
		Air.SurfaceTemperatureKelvin, Air.ScaleHeightMetres / 1000.0,
		Air.TopMetres / 1000.0,
		Air.RayleighPerMetre.X * 1000.0, Air.RayleighPerMetre.Y * 1000.0,
		Air.RayleighPerMetre.Z * 1000.0,
		Air.OzoneAbsorptionPerMetre * 1000.0,
		Air.bHasClouds ? TEXT("yes") : TEXT("no"));
}
