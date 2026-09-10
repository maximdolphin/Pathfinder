#include "LedgerAtmosphere.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "LedgerLog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

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

			// **The aerosol is coloured, and that is where a carbon-dioxide sky
			// stops being blue.** T090. Scattering and absorption are separate
			// per-channel quantities, because the two are what the particles do
			// with the light and they are not the same shape: Martian dust
			// scatters two thirds of the blue it intercepts and nineteen
			// twentieths of the red.
			const FVector3d ScatterKm = Air.MieScatteringPerMetre * 1000.0;
			const FVector3d AbsorbKm = Air.MieAbsorptionPerMetre * 1000.0;
			const double ScatterMax =
				FMath::Max3(ScatterKm.X, ScatterKm.Y, ScatterKm.Z);
			const double AbsorbMax = FMath::Max3(AbsorbKm.X, AbsorbKm.Y, AbsorbKm.Z);
			if (ScatterMax > 0.0)
			{
				Atmosphere->MieScattering = FLinearColor(
					static_cast<float>(ScatterKm.Z / ScatterMax),
					static_cast<float>(ScatterKm.Y / ScatterMax),
					static_cast<float>(ScatterKm.X / ScatterMax));
				Atmosphere->MieScatteringScale = static_cast<float>(ScatterMax);
			}
			if (AbsorbMax > 0.0)
			{
				Atmosphere->MieAbsorption = FLinearColor(
					static_cast<float>(AbsorbKm.Z / AbsorbMax),
					static_cast<float>(AbsorbKm.Y / AbsorbMax),
					static_cast<float>(AbsorbKm.X / AbsorbMax));
				Atmosphere->MieAbsorptionScale = static_cast<float>(AbsorbMax);
			}
			Atmosphere->MieAnisotropy = static_cast<float>(Air.MieAnisotropy);

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
			// **Altitudes are SetDecks's business now.** T094. What is left
			// here is everything that does not change with the weather: how far
			// the tracing reaches, how many samples it takes, and which planet
			// it is wrapped around.

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
			 "Rayleigh %.4f/%.4f/%.4f per km, dust scatter %.5f/%.5f/%.5f absorb "
			 "%.5f/%.5f/%.5f g=%.2f, ozone %.5f, clouds %s"),
		LexToString(Air.Composition), Air.SurfacePressurePascals,
		Air.SurfaceTemperatureKelvin, Air.ScaleHeightMetres / 1000.0,
		Air.TopMetres / 1000.0,
		Air.RayleighPerMetre.X * 1000.0, Air.RayleighPerMetre.Y * 1000.0,
		Air.RayleighPerMetre.Z * 1000.0,
		Air.MieScatteringPerMetre.X * 1000.0, Air.MieScatteringPerMetre.Y * 1000.0,
		Air.MieScatteringPerMetre.Z * 1000.0,
		Air.MieAbsorptionPerMetre.X * 1000.0, Air.MieAbsorptionPerMetre.Y * 1000.0,
		Air.MieAbsorptionPerMetre.Z * 1000.0, Air.MieAnisotropy,
		Air.OzoneAbsorptionPerMetre * 1000.0,
		Air.bHasClouds ? TEXT("yes") : TEXT("no"));
}

void ALedgerAtmosphere::SetDecks(const FLedgerCloudDecks& Decks)
{
	if (Clouds == nullptr)
	{
		return;
	}

	const FLedgerCloudDeck& Deck = Decks.Cumulus;
	Clouds->SetVisibility(Deck.bPresent);
	if (!Deck.bPresent)
	{
		return;
	}

	// **The base is the lifting condensation level and nothing else.** It used
	// to be a two-kilometre default with a comment about cumulus; it is now
	// whatever height the air's own dew point depression and lapse rate put it
	// at, which moves when the weather does.
	Clouds->LayerBottomAltitude =
		static_cast<float>(Deck.BaseMetres / 1000.0);
	Clouds->LayerHeight = FMath::Max(
		static_cast<float>((Deck.TopMetres - Deck.BaseMetres) / 1000.0), 0.5f);

	if (CloudMaterial == nullptr)
	{
		if (UMaterialInterface* Source = Clouds->GetMaterial())
		{
			CloudMaterial = UMaterialInstanceDynamic::Create(Source, this);
			if (CloudMaterial != nullptr)
			{
				Clouds->SetMaterial(CloudMaterial);
			}
		}
	}

	if (CloudMaterial != nullptr
		&& FMath::Abs(Deck.Coverage - LastCoverage) > 0.001)
	{
		// **This is the line T088 needed.** The deck used to cover the whole
		// sky at whatever the material shipped with, so a moon the ephemeris
		// had placed correctly was behind cloud in every frame of a night. It
		// now covers as much as the pressure overhead says, which on a ridge
		// day is under half.
		CloudMaterial->SetScalarParameterValue(
			TEXT("Cloud_GlobalCoverage"), static_cast<float>(Deck.Coverage));
		CloudMaterial->SetScalarParameterValue(
			TEXT("Cloud_GlobalDensity"), static_cast<float>(Deck.Opacity));
		LastCoverage = Deck.Coverage;

		UE_LOG(LogLedger, Log,
			TEXT("clouds: cumulus %.0f to %.0f m, %.0f%% cover; middle %s; "
				 "cirrus %s; tropopause %.0f m"),
			Deck.BaseMetres, Deck.TopMetres, Deck.Coverage * 100.0,
			Decks.Middle.bPresent
				? *FString::Printf(TEXT("%.0f to %.0f m at %.0f%%"),
					Decks.Middle.BaseMetres, Decks.Middle.TopMetres,
					Decks.Middle.Coverage * 100.0)
				: TEXT("none"),
			Decks.Cirrus.bPresent
				? *FString::Printf(TEXT("%.0f to %.0f m at %.0f%%"),
					Decks.Cirrus.BaseMetres, Decks.Cirrus.TopMetres,
					Decks.Cirrus.Coverage * 100.0)
				: TEXT("none"),
			Decks.TropopauseMetres);
	}
	Clouds->MarkRenderStateDirty();
}
