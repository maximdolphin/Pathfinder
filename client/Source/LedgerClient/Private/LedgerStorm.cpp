#include "LedgerStorm.h"

#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "LedgerAir.h"
#include "LedgerAudio.h"
#include "LedgerFrames.h"
#include "LedgerLog.h"
#include "LedgerMath.h"
#include "LedgerPlanet.h"
#include "LedgerWorld.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/CommandLine.h"

namespace
{
	/// How far off a flash can be and still be this viewer's, metres.
	constexpr double StormFlashRangeMetres = 10000.0;

	/// ponytail: sea-level speed of sound. The air's own temperature would move
	/// it by a tenth; read it from LedgerAir when a delay has to be exact.
	constexpr double StormSoundMetresPerSecond = 343.0;

	/// Thunder carries about twenty kilometres; past that it is below the wind.
	constexpr double StormEarshotMetres = 20000.0;

	constexpr double StormFlashSeconds = 0.2;

	/// A return stroke peaks at the order of 1e11 candela, which is what lights
	/// a cloud base ten kilometres off against an overcast day.
	constexpr float StormFlashCandela = 1.0e11f;

	/// How many lows the clouds are told about: every low the schedule can
	/// have alive at once. T106 found four was not enough -- thirteen storms in
	/// a hundred came from a fifth low, or from two overlapping, and the sky
	/// drew nothing over them.
	constexpr int32 StormLowsPublished = 12;

	/// A low this deep is drawn at full depth: the deepest the schedule makes.
	constexpr double StormDrawnFullPascals = 4000.0;

	const TCHAR* StormCollectionPath = TEXT("/Game/Materials/MPC_LedgerWind");
}

bool ULedgerStorm::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerStorm::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerStorm, STATGROUP_Tickables);
}

void ULedgerStorm::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Thunder is the wind's generator turned down to a rumble: broadband noise
	// through a low pass is what distant thunder is, and a second synthesiser
	// would be a second thing to keep honest.
	Thunder = NewObject<ULedgerWindVoice>(&InWorld);
	Thunder->bAlwaysPlay = true;
	Thunder->bIsUISound = true;
	Thunder->bAllowSpatialization = false;
	Thunder->RegisterComponentWithWorld(&InWorld);
	Thunder->Start();
	Thunder->SetTarget(0.0f, 90.0f);

	Flash = NewObject<UPointLightComponent>(&InWorld);
	Flash->SetMobility(EComponentMobility::Movable);
	Flash->SetIntensityUnits(ELightUnits::Candelas);
	Flash->SetIntensity(0.0f);
	Flash->SetLightColor(FLinearColor(0.80f, 0.85f, 1.0f));
	Flash->SetAttenuationRadius(static_cast<float>(StormFlashRangeMetres * 2.0 * 100.0));
	Flash->SetCastShadows(false);
	Flash->RegisterComponentWithWorld(&InWorld);
}

FLedgerStorm ULedgerStorm::StormAt(const FVector& WorldPosition) const
{
	const UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Builder == nullptr || Planet == nullptr)
	{
		return FLedgerStorm();
	}
	const FVector3d Up =
		(FVector3d(WorldPosition) - FVector3d(Planet->GetActorLocation())).GetSafeNormal();
	const FLedgerSystem& System = Builder->GetSystem();
	const int32 Home = Builder->GetHomeBodyIndex();
	const double When = Builder->GetWhenSeconds();
	return LedgerWeather::StormAt(System, Home, LedgerAir::For(System, Home, When),
		FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0)), FMath::Atan2(Up.Y, Up.X), When);
}

FVector ULedgerStorm::GustAt(const FVector& WorldPosition) const
{
	const UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Builder == nullptr || Planet == nullptr)
	{
		return FVector::ZeroVector;
	}
	const FVector3d Offset = FVector3d(WorldPosition) - FVector3d(Planet->GetActorLocation());
	const FVector3d Up = Offset.GetSafeNormal();
	const FLedgerSystem& System = Builder->GetSystem();
	const int32 Home = Builder->GetHomeBodyIndex();
	const double When = Builder->GetWhenSeconds();
	const FVector3d Local = LedgerWeather::GustAt(System, Home, LedgerAir::For(System, Home, When),
		FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0)), FMath::Atan2(Up.Y, Up.X),
		(Offset.Length() - Planet->Radius) / 100.0, When);
	if (Local.IsZero())
	{
		return FVector::ZeroVector;
	}
	// East, north and up here, turned into the world frame the way the wind is.
	const FVector3d East = LedgerFrames::ToBody({ Home, Up, FVector3d(1.0, 0.0, 0.0) }).Metres.GetSafeNormal();
	const FVector3d North = LedgerFrames::ToBody({ Home, Up, FVector3d(0.0, 1.0, 0.0) }).Metres.GetSafeNormal();
	return FVector((East * Local.X + North * Local.Y + Up * Local.Z) * 100.0);
}

void ULedgerStorm::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	const APlayerController* Controller =
		World != nullptr ? World->GetFirstPlayerController() : nullptr;
	ULedgerWorldBuilder* Builder =
		World != nullptr ? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Controller == nullptr || Builder == nullptr || Planet == nullptr
		|| DeltaSeconds <= 0.0f)
	{
		return;
	}
	const FLedgerSystem& System = Builder->GetSystem();
	const int32 Home = Builder->GetHomeBodyIndex();
	if (!System.Bodies.IsValidIndex(Home))
	{
		return;
	}
	const double When = Builder->GetWhenSeconds();

	// ---- the lows the clouds draw, again when the clock has moved a minute
	if (FMath::Abs(When - PublishedWhen) >= 60.0)
	{
		PublishedWhen = When;
		TArray<FLedgerPressureCell> Cells;
		LedgerWeather::DeepestLows(System, Home, When, StormLowsPublished, Cells);
		Lows.Reset();
		LowDepths.Reset();
		for (int32 Index = 0; Index < FMath::Min(Cells.Num(), StormLowsPublished); ++Index)
		{
			const FLedgerPressureCell& Cell = Cells[Index];
			const double CosLat = FMath::Cos(Cell.LatitudeRadians);
			Lows.Add(FVector4d(
				CosLat * FMath::Cos(Cell.LongitudeRadians),
				CosLat * FMath::Sin(Cell.LongitudeRadians),
				FMath::Sin(Cell.LatitudeRadians),
				Cell.RadiusMetres / System.Bodies[Home].RadiusMetres));
			LowDepths.Add(FMath::Clamp(-Cell.AnomalyPascals / StormDrawnFullPascals, 0.0, 1.0));
		}

		// A diagnostic: one low over the whole planet, so a frame shows at a
		// glance whether the clouds read this list at all.
		if (FParse::Param(FCommandLine::Get(), TEXT("stormeverywhere")))
		{
			Lows.Reset();
			LowDepths.Reset();
			Lows.Add(FVector4d(0.0, 0.0, 1.0, 3.2));
			LowDepths.Add(1.0);
		}
		if (!bLookedForCollection)
		{
			bLookedForCollection = true;
			Collection = LoadObject<UMaterialParameterCollection>(nullptr, StormCollectionPath);
		}
		if (Lows.Num() > 0 && FMath::Abs(When - LoggedWhen) > 3600.0)
		{
			LoggedWhen = When;
			UE_LOG(LogLedger, Log, TEXT("storm: the deepest low at %.2f,%.2f, %.0f%% deep, %.3f rad across; collection %s"),
				FMath::RadiansToDegrees(FMath::Asin(Lows[0].Z)), FMath::RadiansToDegrees(FMath::Atan2(Lows[0].Y, Lows[0].X)),
				LowDepths[0] * 100.0, Lows[0].W, Collection != nullptr ? TEXT("found") : TEXT("NOT FOUND"));
		}
		if (UMaterialParameterCollectionInstance* Instance = Collection != nullptr
			? World->GetParameterCollectionInstance(Collection) : nullptr)
		{
			FLinearColor Depths[3] = { FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f) };
			for (int32 Index = 0; Index < StormLowsPublished; ++Index)
			{
				// An empty slot still needs a radius: the material divides by it.
				const FVector4d Low = Lows.IsValidIndex(Index)
					? Lows[Index] : FVector4d(0.0, 0.0, 1.0, 0.01);
				Instance->SetVectorParameterValue(*FString::Printf(TEXT("Storm%d"), Index),
					FLinearColor(static_cast<float>(Low.X), static_cast<float>(Low.Y),
						static_cast<float>(Low.Z), static_cast<float>(FMath::Max(Low.W, 0.01))));
				Depths[Index / 4].Component(Index % 4) = LowDepths.IsValidIndex(Index)
					? static_cast<float>(LowDepths[Index]) : 0.0f;
			}
			Instance->SetVectorParameterValue(TEXT("StormDepths"), Depths[0]);
			Instance->SetVectorParameterValue(TEXT("StormDepths1"), Depths[1]);
			Instance->SetVectorParameterValue(TEXT("StormDepths2"), Depths[2]);
			UE_LOG(LogLedger, Verbose, TEXT("storm: published %d lows"), Lows.Num());
		}
	}

	// ---- lightning and thunder around the viewer
	FVector Eye = FVector::ZeroVector;
	FRotator Ignored = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(Eye, Ignored);
	const FLedgerStorm Here = StormAt(Eye);

	// A Poisson draw each frame at the storm's rate, on real time rather than
	// the world clock: fixtures freeze the clock, and a frozen storm still
	// flashes.
	if (Flash != nullptr && Here.FlashesPerMinute > 0.0
		&& Dice.FRand() < Here.FlashesPerMinute / 60.0 * DeltaSeconds)
	{
		const FVector3d Centre = FVector3d(Planet->GetActorLocation());
		const FVector3d Up = (FVector3d(Eye) - Centre).GetSafeNormal();
		FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up);
		if (East.IsNearlyZero())
		{
			East = FVector3d::CrossProduct(FVector3d::UnitX(), Up);
		}
		East.Normalize();
		const FVector3d North = FVector3d::CrossProduct(Up, East);
		const double Bearing = Dice.FRandRange(0.0f, static_cast<float>(LedgerTwoPi));
		const double Range = StormFlashRangeMetres * FMath::Sqrt(Dice.FRand());
		const FVector3d Across =
			(East * FMath::Cos(Bearing) + North * FMath::Sin(Bearing)) * Range * 100.0;

		// In the base of the cloud, where a stroke leaves it.
		const FLedgerAirProfile Air = LedgerAir::For(System, Home, When);
		const FVector3d At = Centre + (Up * Planet->Radius + Across).GetSafeNormal()
			* (Planet->Radius + (Air.CloudBaseMetres + 1000.0) * 100.0);
		Flash->SetWorldLocation(FVector(At));
		Flash->SetIntensity(StormFlashCandela);
		FlashLeft = StormFlashSeconds;
		++Flashes;

		const double Metres = (At - FVector3d(Eye)).Length() / 100.0;
		// Heard only within earshot. Seen from orbit a flash is a flash, and
		// its thunder is an hour of sound through air that is not there.
		if (Metres < StormEarshotMetres)
		{
			Rumbles.Add({ Metres / StormSoundMetresPerSecond,
				static_cast<float>(FMath::Clamp(1.0 - Metres / StormEarshotMetres, 0.15, 1.0)) });
		}
		UE_LOG(LogLedger, Log,
			TEXT("storm: flash %d at %.0f m, severity %.2f; thunder due in %.1f s"),
			Flashes, Metres, Here.Severity, Metres / StormSoundMetresPerSecond);
	}
	if (FlashLeft > 0.0)
	{
		FlashLeft -= DeltaSeconds;
		if (FlashLeft <= 0.0 && Flash != nullptr)
		{
			Flash->SetIntensity(0.0f);
		}
	}

	// Thunder arrives when the sound does.
	for (int32 Index = Rumbles.Num() - 1; Index >= 0; --Index)
	{
		Rumbles[Index].Key -= DeltaSeconds;
		if (Rumbles[Index].Key <= 0.0)
		{
			ThunderLevel = FMath::Max(ThunderLevel, static_cast<double>(Rumbles[Index].Value));
			++Thunders;
			Rumbles.RemoveAt(Index);
		}
	}
	ThunderLevel *= FMath::Exp(-DeltaSeconds / 1.5);
	if (Thunder != nullptr)
	{
		Thunder->SetTarget(static_cast<float>(ThunderLevel), 90.0f);
	}
}
