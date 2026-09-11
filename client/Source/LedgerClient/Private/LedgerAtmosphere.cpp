#include "LedgerAtmosphere.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerLog.h"
#include "LedgerSurface.h"
#include "Misc/CommandLine.h"
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
		// `-noclouds` hides the deck: a control arm, so a frame full of something
		// can be asked whether the something is cloud.
		Clouds->SetVisibility(bHasAir && Air.bHasClouds && !FParse::Param(FCommandLine::Get(), TEXT("noclouds")));
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
			bTracingInside = false;
			// **From orbit too.** A ray only starts marching if the layer is within
			// this many kilometres, and the default 350 is a sixth of the way to
			// the air show's orbit frame -- which came back with no cloud at all
			// even with the probe putting density everywhere.
			Clouds->TracingStartMaxDistance = 30000.0f;
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
		// Unless a ground fog has asked for volumetric fog, which it needs for
		// its froxels and nothing else.
		if (VolumetricOnlyMetres > 0.0)
		{
			UseVolumetricFogOnly(VolumetricOnlyMetres);
		}
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

void ALedgerAtmosphere::UseVolumetricFogOnly(double DistanceMetres)
{
	VolumetricOnlyMetres = DistanceMetres;
	if (Fog == nullptr)
	{
		return;
	}
	// The height fog itself stays as good as off -- ConfigureForAir says why.
	// What turning it on buys is the froxel grid, which a volume material on
	// a mesh is voxelised into, and which ends at this distance. Not zero: the
	// engine drops a height fog whose density is under 1e-8 from the scene,
	// volumetric fog and all. 1e-7 with its opacity capped at a thousandth is
	// nothing, even from orbit.
	Fog->SetFogDensity(1.0e-7f);
	Fog->SetFogMaxOpacity(0.001f);
	Fog->SetVolumetricFog(true);
	// And none of the height fog's own density in the froxels. It is a plane
	// in world Z, and a camera in orbit can stand thousands of kilometres
	// "below" it, where its exponential saturates whatever the density: the
	// orbit frame came back as a white planet under an orange sky. The opacity
	// cap bounds the analytic fog, not what volumetric fog integrates.
	Fog->SetVolumetricFogExtinctionScale(0.0f);
	Fog->SetVolumetricFogDistance(static_cast<float>(DistanceMetres * 100.0));
	Fog->SetVisibility(true);
	Fog->MarkRenderStateDirty();
}

void ALedgerAtmosphere::StopVolumetricFog()
{
	VolumetricOnlyMetres = 0.0;
	if (Fog == nullptr)
	{
		return;
	}
	Fog->SetFogDensity(0.0f);
	Fog->SetVolumetricFog(false);
	Fog->SetVisibility(false);
	Fog->MarkRenderStateDirty();
}

void ALedgerAtmosphere::SetViewerAltitude(double MetresAboveSea)
{
	if (Clouds == nullptr || LayerTopMetres <= LayerBottomMetres)
	{
		return;
	}

	// **Short steps inside the layer.** The engine spreads at most 768 samples
	// (x1.2 here) along a view ray over the whole traced distance past 15 km,
	// and from inside the layer that is the full 250 km: a step of about
	// 270 m, and every step drawn as a spherical shell in the cloud noise --
	// the concentric sepia bands that filled every frame taken from inside
	// the deck (T054's cliff cameras at 2 km, T094). From outside, a ray
	// only crosses the layer's thickness and the 250 km is what grazing
	// views at the horizon need. Inside, the density hides anything thirty
	// kilometres off, and thirty makes the step about 30 m.
	const bool bInside = MetresAboveSea > LayerBottomMetres - 200.0
		&& MetresAboveSea < LayerTopMetres + 200.0;
	if (bInside == bTracingInside)
	{
		return;
	}
	bTracingInside = bInside;
	Clouds->TracingMaxDistance = bInside ? 30.0f : 250.0f;
	Clouds->MarkRenderStateDirty();
	UE_LOG(LogLedger, Log, TEXT("clouds: viewer %s the layer (%.0f m; layer %.0f-%.0f m), tracing to %.0f km"),
		bInside ? TEXT("inside") : TEXT("outside"), MetresAboveSea, LayerBottomMetres, LayerTopMetres,
		Clouds->TracingMaxDistance);
}

void ALedgerAtmosphere::SetDecks(const FLedgerCloudDecks& Decks)
{
	if (Clouds == nullptr)
	{
		return;
	}

	const bool bAny = Decks.Cumulus.bPresent || Decks.Middle.bPresent
		|| Decks.Cirrus.bPresent;
	Clouds->SetVisibility(bAny && !FParse::Param(FCommandLine::Get(), TEXT("noclouds")));
	if (!bAny)
	{
		return;
	}

	// **One layer from the lowest base to the highest top**, because Unreal
	// draws one volumetric cloud per scene. The three decks live inside it as
	// bands, and the material is told where each one is.
	const double Bottom = Decks.LowestMetres;
	const double Span = FMath::Max(Decks.HighestMetres - Bottom, 500.0);
	Clouds->LayerBottomAltitude = static_cast<float>(Bottom / 1000.0);
	Clouds->LayerHeight = static_cast<float>(Span / 1000.0);
	LayerBottomMetres = Bottom;
	LayerTopMetres = Bottom + Span;

	// **Which material draws the deck, and why the choice exists.**
	//
	// `-threedecks` uses the volume material this project builds, which has a
	// band per deck and is the only way to get three of them out of one
	// component. It compiles, it is assigned, its altitude coordinate is the
	// one the engine says it is -- and it renders nothing yet. Left on by
	// default that is a sky with the clouds computed, placed, and invisible,
	// which is worse than one deck drawn correctly.
	//
	// So the default is the engine's own cloud material, driven at the
	// altitude and the coverage this project computes. One deck of three, in
	// the right place, at the right density. The flag is where the other two
	// are worked on.
	const bool bThreeDecks =
		FParse::Param(FCommandLine::Get(), TEXT("threedecks"));
	if (CloudMaterial == nullptr)
	{
		UMaterialInterface* Source = bThreeDecks
			? LedgerSurface::CreateCloudMaterial(this)
			: Clouds->GetMaterial();
		if (Source != nullptr)
		{
			CloudMaterial = UMaterialInstanceDynamic::Create(Source, this);
			if (CloudMaterial != nullptr)
			{
				Clouds->SetMaterial(CloudMaterial);
			}
		}
		else
		{
			// **No material, no clouds, and that is the safe failure.** A
			// volume component with the wrong material draws an opaque grey
			// slab across the whole sky, which is worse than a clear day.
			UE_LOG(LogLedger, Warning,
				TEXT("clouds: no cloud material, so the deck is hidden"));
			Clouds->SetVisibility(false);
			return;
		}
	}

	if (!bThreeDecks)
	{
		// The engine's material has one deck and its own parameter names, so
		// the layer is the cumulus deck alone and the coverage is that deck's.
		const FLedgerCloudDeck& Only = Decks.Cumulus;
		Clouds->LayerBottomAltitude = static_cast<float>(Only.BaseMetres / 1000.0);
		Clouds->LayerHeight = FMath::Max(
			static_cast<float>((Only.TopMetres - Only.BaseMetres) / 1000.0), 0.5f);
		// The layer is the one deck here, so that is what "inside" means.
		LayerBottomMetres = Only.BaseMetres;
		LayerTopMetres = Only.BaseMetres + FMath::Max(Only.TopMetres - Only.BaseMetres, 500.0);
		CloudMaterial->SetScalarParameterValue(
			TEXT("Cloud_GlobalCoverage"), static_cast<float>(Only.Coverage));
		CloudMaterial->SetScalarParameterValue(
			TEXT("Cloud_GlobalDensity"), static_cast<float>(Only.Opacity));
	}

	if (CloudMaterial == nullptr)
	{
		return;
	}

	// Each deck's window as a fraction of the layer, which is the coordinate
	// the material reads its own altitude in.
	auto Band = [this, Bottom, Span](
		const TCHAR* Prefix, const FLedgerCloudDeck& Deck)
	{
		const float Centre = static_cast<float>(
			((Deck.BaseMetres + Deck.TopMetres) * 0.5 - Bottom) / Span);
		// Half the deck's thickness, with a floor: a deck thinner than a
		// twentieth of the layer would be a sheet of paper and the renderer
		// would step straight over it.
		const float Width = FMath::Max(static_cast<float>(
			(Deck.TopMetres - Deck.BaseMetres) * 0.5 / Span), 0.05f);
		CloudMaterial->SetScalarParameterValue(
			*FString::Printf(TEXT("%sCentre"), Prefix), Centre);
		CloudMaterial->SetScalarParameterValue(
			*FString::Printf(TEXT("%sWidth"), Prefix), Width);
		CloudMaterial->SetScalarParameterValue(
			*FString::Printf(TEXT("%sCoverage"), Prefix),
			Deck.bPresent ? static_cast<float>(Deck.Coverage) : 0.0f);
		CloudMaterial->SetScalarParameterValue(
			*FString::Printf(TEXT("%sDensity"), Prefix),
			static_cast<float>(Deck.Opacity));
	};

	if (bThreeDecks)
	{
		// **The anchor that gives the noise its precision back.**
		//
		// The material works in camera-relative coordinates, which are small
		// enough for a float to resolve a cloud in. This puts them back in the
		// world: the camera's own position reduced modulo a hundred kilometres,
		// computed here in doubles where the precision still exists. Position
		// minus camera plus (camera mod T) is world-anchored and small.
		if (const UWorld* World = GetWorld())
		{
			if (const APlayerController* Controller =
				World->GetFirstPlayerController())
			{
				FVector Eye = FVector::ZeroVector;
				FRotator Ignored = FRotator::ZeroRotator;
				Controller->GetPlayerViewPoint(Eye, Ignored);

				constexpr double Tile = 1.0e7;   // a hundred kilometres
				const FVector3d Anchor(
					FMath::Fmod(static_cast<double>(Eye.X), Tile),
					FMath::Fmod(static_cast<double>(Eye.Y), Tile),
					FMath::Fmod(static_cast<double>(Eye.Z), Tile));
				CloudMaterial->SetVectorParameterValue(TEXT("NoiseOrigin"),
					FLinearColor(
						static_cast<float>(Anchor.X),
						static_cast<float>(Anchor.Y),
						static_cast<float>(Anchor.Z), 0.0f));

				// Which way is up, so the noise can be flattened into columns.
				// The actor sits at the planet's centre, so the viewer's own
				// position is the local vertical -- and over the few tens of
				// kilometres a cloud deck is visible across, one vertical is
				// close enough to all of them.
				const FVector3d Vertical =
					(FVector3d(Eye) - FVector3d(GetActorLocation()))
					.GetSafeNormal();
				CloudMaterial->SetVectorParameterValue(TEXT("NoiseUp"),
					FLinearColor(
						static_cast<float>(Vertical.X),
						static_cast<float>(Vertical.Y),
						static_cast<float>(Vertical.Z), 0.0f));
			}
		}

		Band(TEXT("Cumulus"), Decks.Cumulus);
		Band(TEXT("Middle"), Decks.Middle);
		Band(TEXT("Cirrus"), Decks.Cirrus);
	}

	if (FMath::Abs(Decks.Cumulus.Coverage - LastCoverage) > 0.001)
	{
		LastCoverage = Decks.Cumulus.Coverage;
		UE_LOG(LogLedger, Log,
			TEXT("clouds: layer %.0f to %.0f m; cumulus %.0f-%.0f at %.0f%%, "
				 "middle %.0f-%.0f at %.0f%%, cirrus %.0f-%.0f at %.0f%%"),
			Bottom, Bottom + Span,
			Decks.Cumulus.BaseMetres, Decks.Cumulus.TopMetres,
			Decks.Cumulus.Coverage * 100.0,
			Decks.Middle.BaseMetres, Decks.Middle.TopMetres,
			Decks.Middle.bPresent ? Decks.Middle.Coverage * 100.0 : 0.0,
			Decks.Cirrus.BaseMetres, Decks.Cirrus.TopMetres,
			Decks.Cirrus.bPresent ? Decks.Cirrus.Coverage * 100.0 : 0.0);
	}
	Clouds->MarkRenderStateDirty();
}

void ALedgerAtmosphere::SetSunElevationFloorDegrees(double Degrees)
{
	if (Atmosphere == nullptr)
	{
		return;
	}
	const float Floor = static_cast<float>(FMath::Clamp(Degrees, -90.0, 90.0));
	// Only when it moves by a meaningful amount: the setter marks the render
	// state dirty, and a tenth of a degree is below anything the sky shows.
	if (FMath::Abs(Atmosphere->TransmittanceMinLightElevationAngle - Floor) > 0.1f)
	{
		Atmosphere->SetTransmittanceMinLightElevationAngle(Floor);
	}
}
