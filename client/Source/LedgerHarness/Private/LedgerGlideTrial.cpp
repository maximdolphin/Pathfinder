#include "LedgerGlideTrial.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerAir.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerShipSystemsComponent.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr double GlideSettleSeconds = 15.0;
	constexpr double GlideHeightMetres = 4000.0;
	constexpr double GlideVacuumMetres = 150000.0;
	constexpr double GlideAirSeconds = 60.0;
	constexpr double GlideCompareSeconds = 20.0;
}

bool ULedgerGlideTrial::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerGlideTrial::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerGlideTrial, STATGROUP_Tickables);
}

void ULedgerGlideTrial::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("glidetrial"));
	Lines.Add(TEXT("A winged ship, unpowered, in the air and out of it (T135)."));
	Lines.Add(TEXT(""));
}

void ULedgerGlideTrial::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World->GetSubsystem<ULedgerWorldBuilder>();
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World->GetFirstPlayerController();
	ALedgerShip* Ship = Controller != nullptr ? Cast<ALedgerShip>(Controller->GetPawn()) : nullptr;
	if (Planet == nullptr || Ship == nullptr || !Ship->Definition().Aero.bWinged)
	{
		return;
	}
	const FVector3d Centre = FVector3d(Planet->GetActorLocation());
	Clock += DeltaSeconds;

	auto Datum = [&]()
	{
		return ((FVector3d(Ship->GetActorLocation()) - Centre).Length() - Planet->Radius) / 100.0;
	};

	// Let go over the site on the glide the wings trim to: the nose where the
	// pitching moment is nil, the path as far below the horizon as drag over
	// lift, at the speed where that lift carries the weight. Let go anywhere
	// else and it flies a phugoid, and a minute of one says nothing about a
	// glide -- which is what the first run of this measured.
	auto Hold = [&](double HeightMetres)
	{
		Up = Builder->GetSiteDirection().GetSafeNormal();
		const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Up).GetSafeNormal();
		Ship->SetFlightEnabled(false);
		Ship->SetFlightMode(ELedgerFlightMode::AssistOff);
		Ship->SetAutoThrottle(0.0f);
		Ship->SetAutoLift(0.0f);
		Ship->SetActorLocation(FVector(Centre + Up * (Planet->SurfaceRadiusAt(Up) + HeightMetres * 100.0)));
		if (TrimSpeed <= 0.0)
		{
			const FLedgerShipAero& Aero = Ship->Definition().Aero;
			const double Mass = Ship->GetSystems() != nullptr ? Ship->GetSystems()->MassKg() : Ship->Definition().MassKg();
			const double Ratio = Aero.SpanMetres * Aero.SpanMetres / Aero.WingAreaM2;
			const double Slope = UE_DOUBLE_TWO_PI * Ratio / (2.0 + FMath::Sqrt(Ratio * Ratio + 4.0));
			const double Belly = Mass / Ship->Definition().Flight.BallisticKgPerM2 / Aero.WingAreaM2;
			TrimAlpha = -Aero.PitchMoment0 / Aero.PitchStability;
			const double Wing = Slope * FMath::Sin(TrimAlpha);
			const double Lift = Wing + Belly * FMath::Square(FMath::Sin(TrimAlpha)) * FMath::Cos(TrimAlpha);
			const double Drag = Aero.ZeroLiftDrag + Wing * Wing / (UE_DOUBLE_PI * Aero.Oswald * Ratio)
				+ Belly * FMath::Pow(FMath::Sin(TrimAlpha), 3.0);
			TrimGamma = FMath::Atan(Drag / Lift);
			const FLedgerAirProfile AirHere = LedgerAir::For(Builder->GetSystem(), Builder->GetHomeBodyIndex(), Builder->GetWhenSeconds());
			const double Density = LedgerAir::DensityAt(AirHere, (Planet->SurfaceRadiusAt(Up) - Planet->Radius) / 100.0 + HeightMetres);
			TrimSpeed = FMath::Sqrt(2.0 * Mass * Ship->SurfaceGravity / 100.0 * FMath::Cos(TrimGamma)
				/ (FMath::Max(Density, 1.0e-6) * Aero.WingAreaM2 * Lift));
		}
		const FVector3d Path = East * FMath::Cos(TrimGamma) - Up * FMath::Sin(TrimGamma);
		const double Nose = TrimAlpha - TrimGamma;
		const FVector3d Forward = East * FMath::Cos(Nose) + Up * FMath::Sin(Nose);
		Ship->SetActorRotation(FRotationMatrix::MakeFromXZ(FVector(Forward), FVector(Up)).Rotator());
		Ship->SetVelocity(FVector(Path * TrimSpeed * 100.0));
	};
	auto Release = [&]()
	{
		StartPosition = FVector3d(Ship->GetActorLocation());
		StartHeight = Datum();
		Gravity = Ship->SurfaceGravity / 100.0 * FMath::Square(Planet->Radius / (StartPosition - Centre).Length());
		// Already sinking down the glide path when let go: the fall is on top.
		SinkAtRelease = -FVector3d::DotProduct(FVector3d(Ship->GetVelocity()), Up) / 100.0;
		Ship->SetFlightEnabled(true);
		Clock = 0.0;
		SinceLine = 0.0;
	};
	const double Lost = StartHeight - Datum();
	const double Sink = -FVector3d::DotProduct(FVector3d(Ship->GetVelocity()), Up) / 100.0;
	const FVector3d Travel = FVector3d(Ship->GetActorLocation()) - StartPosition;
	const double Along = (Travel - Up * FVector3d::DotProduct(Travel, Up)).Length() / 100.0;

	if (Stage == 0)
	{
		Hold(GlideHeightMetres);
		if (Clock < GlideSettleSeconds)
		{
			return;
		}
		Release();
		Lines.Add(FString::Printf(TEXT("in the air: let go %.0f m above the ground on its trimmed glide -- nose %.1f deg above the path, the path %.1f deg down, %.0f m/s -- assist off, no thrust"),
			Ship->AltitudeMetres(), FMath::RadiansToDegrees(TrimAlpha), FMath::RadiansToDegrees(TrimGamma), TrimSpeed));
		Lines.Add(TEXT("   s   lost m   along m   m/s   sink m/s   alpha deg   q kPa   lift"));
		Stage = 1;
		return;
	}
	if (Stage == 1)
	{
		SinceLine += DeltaSeconds;
		if (SinceLine >= 5.0)
		{
			SinceLine = 0.0;
			const FLedgerAeroState& Air = Ship->Aerodynamic();
			Lines.Add(FString::Printf(TEXT("%4.0f  %7.0f  %8.0f  %5.0f  %8.1f  %9.1f  %6.1f  %5.2f%s"), Clock, Lost, Along,
				Ship->GetVelocity().Size() / 100.0, Sink, FMath::RadiansToDegrees(Air.AngleOfAttack), Air.DynamicPressure / 1000.0,
				Air.Lift, Air.bStalled ? TEXT(" stall") : TEXT("")));
		}
		if (AirLost20 < 0.0 && Clock >= GlideCompareSeconds)
		{
			AirLost20 = Lost;
			AirSink20 = Sink;
		}
		if (Clock < GlideAirSeconds && Ship->AltitudeMetres() > 20.0)
		{
			return;
		}
		AirRatio = Lost > 0.0 ? Along / Lost : 0.0;
		AirSpeedEnd = Ship->GetVelocity().Size() / 100.0;
		Lines.Add(FString::Printf(TEXT("after %.0f s: %.0f m lost over %.0f m flown, a glide of %.1f to 1, still at %.0f m/s"),
			Clock, Lost, Along, AirRatio, AirSpeedEnd));
		Lines.Add(TEXT(""));
		Stage = 2;
		Clock = 0.0;
		return;
	}
	if (Stage == 2)
	{
		Hold(GlideVacuumMetres);
		if (Clock < 3.0)
		{
			return;
		}
		Release();
		Lines.Add(FString::Printf(TEXT("out of the air: let go the same way at %.0f km"), StartHeight / 1000.0));
		Stage = 3;
		return;
	}
	if (Clock < GlideCompareSeconds)
	{
		return;
	}
	VacuumLost20 = Lost;
	VacuumSink20 = Sink;
	bRunning = false;
	Ship->SetFlightMode(ELedgerFlightMode::Decoupled);
	Finish();
}

void ULedgerGlideTrial::Finish()
{
	const double Falling = SinkAtRelease + Gravity * GlideCompareSeconds;
	const bool bGlides = AirLost20 >= 0.0 && AirLost20 < 0.25 * VacuumLost20 && AirRatio > 4.0 && AirSpeedEnd > 100.0;
	const bool bFalls = FMath::Abs(VacuumSink20 - Falling) < 0.1 * Falling;
	Lines.Add(FString::Printf(TEXT("in the first %.0f s: %.0f m lost and sinking at %.1f m/s in the air; %.0f m lost and sinking at %.1f m/s out of it, against %.1f m/s for a fall at %.2f m/s2 from the glide path"),
		GlideCompareSeconds, AirLost20, AirSink20, VacuumLost20, VacuumSink20, Falling, Gravity));
	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(TEXT("it glides unpowered in the air: %s"), bGlides ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("and not in vacuum, where it falls: %s"), bFalls ? TEXT("yes") : TEXT("NO")));
	Lines.Add(FString::Printf(TEXT("VERDICT: %s"), bGlides && bFalls ? TEXT("PASS") : TEXT("FAIL")));
	for (const FString& Line : Lines)
	{
		UE_LOG(LogLedger, Log, TEXT("glide trial: %s"), *Line);
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("glide.txt"))));
	FPlatformMisc::RequestExit(false);
}
