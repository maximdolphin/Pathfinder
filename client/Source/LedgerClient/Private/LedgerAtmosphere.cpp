#include "LedgerAtmosphere.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "LedgerSimSubsystem.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#if WITH_EDITOR
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionVertexColor.h"
#endif

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

		// Scale heights, and the setting that decides whether you can see across
		// a valley.
		//
		// Earth's Rayleigh scale height is 13% of its atmosphere, and copying
		// that ratio onto a 5 km shell puts nearly all the air in the bottom
		// 650 m. Standing in it, everything past a couple of kilometres washed
		// out to flat pale green — which read as a broken material and was in
		// fact a correctly-rendered soup. Spreading the same air over half the
		// shell restores ground visibility and keeps the sky blue.
		Atmosphere->RayleighExponentialDistribution = AtmosphereHeightKm * 0.5f;
		Atmosphere->MieExponentialDistribution = AtmosphereHeightKm * 0.12f;

		// Optical depth is coefficient times path length, and the path through
		// this atmosphere is a twelfth of Earth's — so the coefficient has to go
		// *up* for the sky to be blue from the ground, not down.
		//
		// Both ends of this were wrong once. At 0.05 with a dense height fog on
		// top, everything past a few kilometres washed out to flat pale green
		// and looked like an unlit material. At 0.012 the sky went black at
		// ground level. The fog was most of the first problem; this is the
		// setting for the second.
		Atmosphere->RayleighScatteringScale = 0.045f;
		Atmosphere->MieScatteringScale = 0.002f;
		Atmosphere->MultiScatteringFactor = 0.75f;
		Atmosphere->MarkRenderStateDirty();
	}

	if (Clouds != nullptr)
	{
		// Altitudes are above the *ground*, so they must clear the tallest
		// terrain or the clouds render inside mountains.
		const float TerrainTopKm = static_cast<float>(MaxElevationCm / CentimetresPerKilometre);
		Clouds->LayerBottomAltitude = FMath::Max(CloudBaseAltitudeKm, TerrainTopKm * 1.1f);
		Clouds->LayerHeight = CloudLayerHeightKm;
		Clouds->PlanetRadius = RadiusKm;
		// Tracing distance has to cover the horizon or the cloud deck visibly
		// ends in mid-air on the approach.
		Clouds->TracingMaxDistance = FMath::Max(50.0f, RadiusKm * 0.8f);
		Clouds->MarkRenderStateDirty();
	}

	if (Fog != nullptr)
	{
		// Aerial perspective near the ground. Volumetric so the sun shafts
		// through the cloud deck on the way down, which is most of what sells
		// the descent.
		// Light: the sky atmosphere already supplies aerial perspective, and
		// stacking a dense height fog on top of it is what buried the terrain.
		Fog->SetFogDensity(0.0012f);
		Fog->SetFogHeightFalloff(0.9f);
		Fog->SetVolumetricFog(true);
		Fog->SetVolumetricFogDistance(60000.0f);
		Fog->MarkRenderStateDirty();
	}

	UE_LOG(LogLedger, Log,
		TEXT("atmosphere configured: planet %.1f km, air %.1f km, clouds %.1f-%.1f km"),
		RadiusKm,
		AtmosphereHeightKm,
		Clouds != nullptr ? Clouds->LayerBottomAltitude : 0.0f,
		Clouds != nullptr ? Clouds->LayerBottomAltitude + Clouds->LayerHeight : 0.0f);
}

namespace LedgerMaterials
{
	UMaterialInterface* CreateTerrainMaterial(UObject* Outer)
	{
#if WITH_EDITOR
		UMaterial* Material = NewObject<UMaterial>(Outer, NAME_None, RF_Transient);
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);

		UMaterialExpressionVertexColor* VertexColour = NewObject<UMaterialExpressionVertexColor>(Material);
		UMaterialExpressionConstant* Roughness = NewObject<UMaterialExpressionConstant>(Material);
		Roughness->R = 0.92f;
		UMaterialExpressionConstant* Specular = NewObject<UMaterialExpressionConstant>(Material);
		Specular->R = 0.08f;

		Material->GetExpressionCollection().AddExpression(VertexColour);
		Material->GetExpressionCollection().AddExpression(Roughness);
		Material->GetExpressionCollection().AddExpression(Specular);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->BaseColor.Expression = VertexColour;
		EditorData->Roughness.Expression = Roughness;
		EditorData->Specular.Expression = Specular;

		Material->PostEditChange();
		return Material;
#else
		// A cooked build cannot compile a shader at runtime. This needs a real
		// asset before there is a packaged game.
		(void)Outer;
		return nullptr;
#endif
	}
}
