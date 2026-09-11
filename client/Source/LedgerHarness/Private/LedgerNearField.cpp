#include "LedgerNearField.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "LedgerShip.h"
#include "LedgerTerrainMath.h"
#include "LedgerTerrainSample.h"
#include "LedgerWorld.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/// Long enough for the finest patches to arrive. Depth 18 is three levels
	/// below where the streamer used to stop and the ring has to walk down to
	/// it; asking at five seconds measures the streamer, not the terrain.
	constexpr double NearFieldSettleSeconds = 20.0;

	/// How long to wait for the streaming queue to drain before measuring anyway.
	constexpr double NearFieldGiveUpSeconds = 240.0;

	/// The profile: fifty metres in front of the camera, sampled every 25 cm.
	/// Fifty metres by default, which is what T429 is measured over.
	///
	/// `-profilemetres=N` lengthens it, and T049 needs that: broken stitching
	/// only cracks where two patches meet at DIFFERENT depths, and a fifty
	/// metre profile taken standing still never crosses one. `-forcedepth`
	/// puts a depth boundary two kilometres out, so the profile has to reach
	/// it to measure the thing the fault produces.
	double ProfileLength()
	{
		double Metres = 50.0;
		FParse::Value(FCommandLine::Get(), TEXT("profilemetres="), Metres);
		return FMath::Clamp(Metres, 10.0, 5000.0);
	}

	constexpr int32 ProfileSamples = 600;

	/// Eye height, and the framing the acceptance is written against.
	constexpr double EyeMetres = 1.7;
	constexpr double ViewportWidthPixels = 1920.0;
	constexpr double HorizontalFovDegrees = 90.0;

	/// Where "standing on flat ground" actually looks.
	///
	/// T429's acceptance says no triangle edge over 40 px, and the first run of
	/// this fixture measured that at the camera's own feet, 1.7 m away, where a
	/// 0.60 m quad is 337 px. No terrain can meet that: 40 px at 1.7 m is a 7 cm
	/// triangle, which over a 6,371 km planet is depth 24 and a few hundred
	/// million patches in view.
	///
	/// The number was written meaning the ground a standing person is looking
	/// at, which is a few tens of metres out, so that is what is measured -- and
	/// the figure at one's feet is printed beside it rather than dropped,
	/// because the criterion was mine and moving it quietly would be worse than
	/// having got it wrong.
	constexpr double LookingAtMetres = 20.0;

	/// Where the fixture stands: two kilometres east of the site, off the pad.
	FVector3d Standing(const FVector3d& Site, const ALedgerPlanet* Planet)
	{
		FVector3d East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Site);
		if (East.IsNearlyZero())
		{
			East = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), Site);
		}
		East.Normalize();
		return (Site + East * (200000.0 / Planet->Radius)).GetSafeNormal();
	}

	/// The direction the profile runs, and the camera looks. Tangent, and the
	/// same one both times so the numbers describe the photograph.
	FVector3d ProfileDirection(const FVector3d& Up)
	{
		FVector3d Along = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Up);
		if (Along.IsNearlyZero())
		{
			Along = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), Up);
		}
		return Along.GetSafeNormal();
	}

	/// RMS distance from the best-fit line through a profile.
	///
	/// A plane fit rather than a mean: ground on a slope is not flat, but it is
	/// also not bumpy, and measuring deviation from the mean would score every
	/// hillside as full of detail. What is being asked is whether there is
	/// anything on the ground *besides* its overall tilt.
	double RmsFromFittedLine(const TArray<double>& Along, const TArray<double>& Height)
	{
		const int32 Count = Height.Num();
		if (Count < 3)
		{
			return 0.0;
		}

		double SumX = 0.0;
		double SumY = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			SumX += Along[Index];
			SumY += Height[Index];
		}
		const double MeanX = SumX / Count;
		const double MeanY = SumY / Count;

		double Covariance = 0.0;
		double Variance = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Dx = Along[Index] - MeanX;
			Covariance += Dx * (Height[Index] - MeanY);
			Variance += Dx * Dx;
		}
		const double Slope = Variance > 0.0 ? Covariance / Variance : 0.0;
		const double Intercept = MeanY - Slope * MeanX;

		double SumSquares = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Residual = Height[Index] - (Slope * Along[Index] + Intercept);
			SumSquares += Residual * Residual;
		}
		return FMath::Sqrt(SumSquares / Count);
	}
}

bool ULedgerNearField::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULedgerNearField::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULedgerNearField, STATGROUP_Tickables);
}

void ULedgerNearField::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bRunning = FParse::Param(FCommandLine::Get(), TEXT("nearfield"));
}

void ULedgerNearField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bRunning || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// Every frame, because the scripted flight is also running and will keep
	// moving the ship through its phases if this does not hold it down. The
	// terrain query fixture learned this by tracing into empty sky a thousand
	// times and reporting a clean sweep of nothing.
	Park();

	Waited += DeltaSeconds;
	// **Settled, not merely late.** Twenty seconds was enough at the default
	// depth; at -forcedepth=18 the same twenty seconds measured a ground that
	// was a third built -- 193 of 600 samples found a patch. So the clock is a
	// floor and the streaming queue is the gate, with a ceiling so a queue that
	// never drains is a failure with a reason rather than a hang.
	const ULedgerWorldBuilder* Builder = GetWorld()->GetSubsystem<ULedgerWorldBuilder>();
	const ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	const bool bStreaming = Planet != nullptr
		&& (Planet->GetStats().JobsInFlight > 0 || Planet->GetStats().PendingBuilds > 0);
	if (Waited < NearFieldSettleSeconds
		|| (bStreaming && Waited < NearFieldGiveUpSeconds))
	{
		return;
	}
	if (bStreaming && !bLoggedGiveUp)
	{
		bLoggedGiveUp = true;
		UE_LOG(LogLedger, Warning,
			TEXT("near field: still streaming after %.0f s (%d jobs, %d builds) -- measuring anyway"),
			Waited, Planet->GetStats().JobsInFlight, Planet->GetStats().PendingBuilds);
	}

	// Straight down from eight metres, once the standing shot is taken.
	//
	// A grazing view cannot answer whether the ground tiles: perspective makes
	// the tile's period on screen a function of distance, so it smears any
	// repeat into a gradient. Looking straight down, one metre of ground is one
	// number of pixels everywhere in frame, and a 2 m tile repeated across a
	// patch shows up as a peak in the autocorrelation at exactly 2 m. That is
	// the before and after for T431.
	if (bCaptured && !bLookedDown)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				TEXT("near-field-down.png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bLookedDown = true;
		return;
	}

	// The photograph first, so the numbers below describe the frame that was
	// captured rather than one after it.
	if (!bCaptured)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"),
				TEXT("near-field-standing.png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		bCaptured = true;
		return;
	}

	if (!bLookedDown)
	{
		return;
	}

	bRunning = false;
	const bool bHolds = Measure();
	UE_LOG(LogLedger, Log, TEXT("near field: %s"),
		bHolds ? TEXT("VERDICT PASS") : TEXT("VERDICT FAIL"));
	FPlatformMisc::RequestExit(false);
}

void ULedgerNearField::Park()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr
		? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Planet == nullptr || Controller == nullptr)
	{
		return;
	}

	ALedgerShip* Ship = Cast<ALedgerShip>(Controller->GetPawn());
	if (Ship == nullptr)
	{
		return;
	}

	// **Not the site.** The site is the town, and the town is a landing pad --
	// the first capture from this fixture came back as a grey plane with yellow
	// stripes on it, photographed from above, and the whole point is natural
	// ground at eye height. Two kilometres east of it, which is off the pad and
	// still inside the collision ring.
	const FVector3d Site = Builder->GetSiteDirection().GetSafeNormal();
	const FVector3d Eye3d = Standing(Site, Planet);
	const double Ground = Planet->SurfaceRadiusAt(Eye3d);
	const FVector3d Origin = FVector3d(Planet->GetActorLocation());

	// The ship is hidden and parked well above: it is the streaming anchor, and
	// a ship sitting in frame is not a photograph of the ground.
	Ship->SetFlightEnabled(false);
	Ship->SetVelocity(FVector::ZeroVector);
	Ship->SetActorHiddenInGame(true);
	Ship->SetActorLocation(FVector(Origin + Eye3d * (Ground + 400000.0)));

	// Eye height, looking out along the profile and a little down -- what a
	// person standing on this ground would be looking at. The first version of
	// this fixture left the player's boom camera alone, which meant the
	// "standing" capture was a top-down shot of the parked ship.
	const FVector3d Ahead = ProfileDirection(Eye3d);
	const FVector3d TargetDirection =
		(Eye3d + Ahead * (LookingAtMetres * 100.0 / Planet->Radius)).GetSafeNormal();
	const FVector Target = FVector(Origin
		+ TargetDirection * Planet->SurfaceRadiusAt(TargetDirection));

	// From the local up. FVector::Rotation() has zero roll in world terms, which
	// on a sphere puts the horizon down the side of the frame everywhere but one
	// longitude.
	// Down the radial for the overhead shot, out along the profile for the
	// standing one. Eight metres up, which frames about sixteen metres of
	// ground at 90 degrees -- eight tile repeats of a 2 m scan.
	const bool bDown = bCaptured && !bLookedDown;
	const FVector Eye = bDown
		? FVector(Origin + Eye3d * (Ground + 800.0 * 100.0))
		: FVector(Origin + Eye3d * (Ground + EyeMetres * 100.0));
	const FRotator Look = bDown
		? FRotationMatrix::MakeFromXZ(
			FVector(-Eye3d), FVector(ProfileDirection(Eye3d))).Rotator()
		: FRotationMatrix::MakeFromXZ(Target - Eye, FVector(Eye3d)).Rotator();

	if (Camera == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		Camera = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Eye, Look, SpawnParams);
		if (Camera != nullptr)
		{
			// With `-channel=`, exposure is pinned and the tone curve is off.
			//
			// The channel views exist to read a number off a picture, and auto
			// exposure plus a film tone curve means the number in the file is
			// the channel put through two nonlinear functions. The first
			// reading taken this way said the terrain's normals had a mean
			// length of 0.4 -- alarming, and an artefact of the exposure rather
			// than a fact about the normals. A debug view that lies is worse
			// than no debug view.
			FString Channel;
			if (FParse::Value(FCommandLine::Get(), TEXT("channel="), Channel))
			{
				if (UCameraComponent* Component = Camera->GetCameraComponent())
				{
					FPostProcessSettings& Post = Component->PostProcessSettings;
					// Min == max is how you pin exposure: the histogram method
					// still runs and is clamped to one value, so the mapping is
					// exactly 1.0 in -> 1.0 out. AEM_Manual was tried first and
					// is a different thing entirely -- it derives exposure from
					// the physical camera's aperture, shutter and ISO, whose
					// defaults rendered every channel between 0 and 7 of 255.
					Post.bOverride_AutoExposureMethod = true;
					Post.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
					Post.bOverride_AutoExposureMinBrightness = true;
					Post.AutoExposureMinBrightness = 1.0f;
					Post.bOverride_AutoExposureMaxBrightness = true;
					Post.AutoExposureMaxBrightness = 1.0f;
					Post.bOverride_AutoExposureBias = true;
					Post.AutoExposureBias = 0.0f;
					Post.bOverride_ToneCurveAmount = true;
					Post.ToneCurveAmount = 0.0f;
					Post.bOverride_ExpandGamut = true;
					Post.ExpandGamut = 0.0f;
				}
			}
			Controller->SetViewTarget(Camera);
		}
	}
	if (Camera != nullptr)
	{
		Camera->SetActorLocationAndRotation(Eye, Look);
	}
}

bool ULedgerNearField::Measure()
{
	UWorld* World = GetWorld();
	ULedgerWorldBuilder* Builder = World != nullptr
		? World->GetSubsystem<ULedgerWorldBuilder>() : nullptr;
	ALedgerPlanet* Planet = Builder != nullptr ? Builder->GetPlanet() : nullptr;
	if (Planet == nullptr)
	{
		UE_LOG(LogLedger, Error, TEXT("near field: no planet"));
		return false;
	}

	const FVector3d Origin = FVector3d(Planet->GetActorLocation());
	const FVector3d Up = Standing(Builder->GetSiteDirection().GetSafeNormal(), Planet);
	const FVector3d Along = ProfileDirection(Up);

	TArray<double> Distance;
	TArray<double> Drawn;
	TArray<double> FieldFine;
	TArray<double> FieldCoarse;
	TArray<double> BandOnly;
	Distance.Reserve(ProfileSamples);

	double PatchWorldSize = 0.0;
	int32 Missing = 0;

	for (int32 Index = 0; Index < ProfileSamples; ++Index)
	{
		const double Metres = ProfileLength() * Index / (ProfileSamples - 1);
		const FVector3d Direction =
			(Up + Along * (Metres * 100.0 / Planet->Radius)).GetSafeNormal();

		// The drawn mesh, which is the only surface anything actually stands on.
		FLedgerTerrainSample Sample;
		if (!Planet->SampleTerrain(FVector(Origin + Direction * Planet->Radius), Sample))
		{
			++Missing;
			continue;
		}
		PatchWorldSize = FMath::Max(PatchWorldSize, Sample.PatchWorldSize);

		// And the field, at the spacing this patch is drawn at, against the
		// field at a spacing too coarse to carry the near-field band. The
		// difference between these two columns is exactly what T429 added.
		const double Spacing = (Sample.PatchWorldSize / 100.0) / 64.0;

		Distance.Add(Metres);
		Drawn.Add(Sample.RadiusCm / 100.0);
		FieldFine.Add(
			LedgerTerrain::Elevation(Direction, Planet->TerrainParams(), Spacing) / 100.0);
		FieldCoarse.Add(
			LedgerTerrain::Elevation(Direction, Planet->TerrainParams(), 20.0) / 100.0);

		// The band on its own. The two columns above are both dominated by the
		// landform underneath them -- fifty metres of hillside has curvature
		// worth more than any detail band -- so subtracting them is the only
		// way to see what T429 actually contributes.
		BandOnly.Add(FieldFine.Last() - FieldCoarse.Last());
	}

	if (Distance.Num() < ProfileSamples / 2)
	{
		// **Why, and the edges anyway.** This said "asked too early" for a run
		// at -forcedepth=18 that had waited four minutes: 8431 nodes wanted
		// drawing and the pool holds 3600, so a third of the ground was a hole
		// for as long as anyone cared to wait. And the edge check does not need
		// the profile at all -- it walks what is drawn -- so a short profile is
		// no reason to throw away the one measurement T049 is about.
		const FLedgerTerrainStats& Stats = Planet->GetStats();
		const FString Why = Stats.SectionsFree == 0 && Stats.VisibleNodes > Stats.SectionsActive
			? FString::Printf(TEXT("the section pool is full: %d nodes want drawing, %d sections exist"),
				Stats.VisibleNodes, Stats.SectionsActive)
			: FString(TEXT("asked before the ground arrived"));
		UE_LOG(LogLedger, Error,
			TEXT("near field: only %d of %d samples found a patch -- %s"),
			Distance.Num(), ProfileSamples, *Why);

		FString Gaps;
		const double WorstGapCm = Planet->MeasureEdgeGaps(Gaps);
		UE_LOG(LogLedger, Log, TEXT("%s"), *Gaps);
		FFileHelper::SaveStringToFile(FString::Printf(
			TEXT("PROFILE INCOMPLETE: %d of %d samples -- %s\n\n---- cracks at the shared edges ----\n\n%s")
			TEXT("the drawn patches meet: %s\n\nVERDICT: FAIL\n"),
			Distance.Num(), ProfileSamples, *Why, *Gaps,
			WorstGapCm <= 1.0 ? TEXT("yes, to within a centimetre") : TEXT("NO -- there is a crack")),
			*FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("near-field.txt"))));
		return false;
	}

	// ---- the worst step at a LOD boundary. T049 ---------------------------
	//
	// "No crack from any angle" cannot be settled by photographs: a crack is a
	// few centimetres of gap along one edge and a camera either happens to be
	// pointing at it or does not. So this walks a dense profile and looks for a
	// discontinuity instead.
	//
	// **It is not a crack test, and saying so is the point.** Sampling either
	// side of a patch boundary cannot distinguish a gap at the shared edge from
	// two patches at different depths describing the same ground differently --
	// both appear as a step. A real crack test samples AT the shared edge from
	// both sides, which needs the two sections vertices and not SampleTerrain.
	// What this does establish is that a boundary two kilometres out steps by
	// two metres on a clean build, which is worth knowing either way.
	//
	// The measure is the largest single-sample jump against the median one. On
	// continuous ground consecutive samples differ by whatever the slope is;
	// at a crack one of them differs by the size of the gap. Comparing to the
	// median rather than to zero is what stops a steep hillside reading as a
	// crack.
	double WorstJump = 0.0;
	double WorstJumpAt = 0.0;
	TArray<double> Steps;
	Steps.Reserve(Drawn.Num());
	for (int32 Index = 1; Index < Drawn.Num(); ++Index)
	{
		Steps.Add(FMath::Abs(Drawn[Index] - Drawn[Index - 1]));
	}
	TArray<double> Sorted = Steps;
	Sorted.Sort();
	const double MedianStep = Sorted.Num() > 0 ? Sorted[Sorted.Num() / 2] : 0.0;
	// Endpoints excluded. The first reading of this reported a 0.153 m "crack"
	// on a clean build, at exactly 50.0 m -- the last sample of a fifty metre
	// profile. A check that is red on a clean build is a check that gets
	// ignored, which is the failure this repository already wrote down once in
	// docs/comparisons/terrain-regression/.
	constexpr int32 Margin = 4;
	for (int32 Index = Margin; Index < Steps.Num() - Margin; ++Index)
	{
		const double Excess = Steps[Index] - MedianStep;
		if (Excess > WorstJump)
		{
			WorstJump = Excess;
			WorstJumpAt = Distance[Index + 1];
		}
	}

	const double DrawnRms = RmsFromFittedLine(Distance, Drawn);
	const double FineRms = RmsFromFittedLine(Distance, FieldFine);
	const double CoarseRms = RmsFromFittedLine(Distance, FieldCoarse);
	const double BandRms = RmsFromFittedLine(Distance, BandOnly);

	double BandLow = TNumericLimits<double>::Max();
	double BandHigh = -TNumericLimits<double>::Max();
	for (const double Value : BandOnly)
	{
		BandLow = FMath::Min(BandLow, Value);
		BandHigh = FMath::Max(BandHigh, Value);
	}

	// The quad the camera is standing on, in pixels. A patch resolves a
	// sixty-fourth of itself, and the nearest ground is about eye height away.
	const double QuadMetres = (PatchWorldSize / 100.0) / 64.0;
	const double HalfFovTangent = FMath::Tan(FMath::DegreesToRadians(HorizontalFovDegrees) * 0.5);
	const double PixelsPerRadian = ViewportWidthPixels / (2.0 * HalfFovTangent);
	const double QuadPixelsAtFeet = (QuadMetres / EyeMetres) * PixelsPerRadian;
	const double QuadPixels = (QuadMetres / LookingAtMetres) * PixelsPerRadian;

	FString Body;
	Body += TEXT("The ground at walking distance (T429).\n\n");
	Body += FString::Printf(
		TEXT("Standing at %.1f m, looking along a %.0f m profile sampled every %.0f cm.\n"),
		EyeMetres, ProfileLength(), ProfileLength() * 100.0 / (ProfileSamples - 1));
	Body += FString::Printf(TEXT("%d of %d samples found a loaded patch.\n\n"),
		Distance.Num(), ProfileSamples);

	Body += TEXT("---- how coarse the triangles are ----\n\n");
	Body += FString::Printf(TEXT("finest patch drawn here   %8.1f m across\n"), PatchWorldSize / 100.0);
	Body += FString::Printf(TEXT("its quads                 %8.2f m\n"), QuadMetres);
	Body += FString::Printf(TEXT("on a %.0f px viewport at %.0f degrees:\n"),
		ViewportWidthPixels, HorizontalFovDegrees);
	Body += FString::Printf(TEXT("  one quad at %4.1f m        %8.0f px  %s\n"),
		LookingAtMetres, QuadPixels,
		QuadPixels <= 40.0 ? TEXT("(acceptance: <= 40)") : TEXT("OVER -- acceptance is 40"));
	Body += FString::Printf(
		TEXT("  one quad at %4.1f m        %8.0f px  (at one\'s feet, not the criterion)\n\n"),
		EyeMetres, QuadPixelsAtFeet);

	Body += TEXT("---- how far the ground departs from a plane ----\n\n");
	Body += TEXT("RMS deviation from the best-fit line through the profile. A fitted\n");
	Body += TEXT("line rather than a mean, so a hillside is not scored as detail.\n\n");
	Body += FString::Printf(TEXT("field, near-field band off   %8.3f m   (what this was before T429)\n"), CoarseRms);
	Body += FString::Printf(TEXT("field, band on at this LOD   %8.3f m\n"), FineRms);
	Body += FString::Printf(TEXT("the drawn mesh               %8.3f m   %s\n\n"),
		DrawnRms, DrawnRms >= 0.15 ? TEXT("(acceptance: >= 0.15)") : TEXT("UNDER -- acceptance is 0.15"));

	Body += TEXT("---- cracks along the profile ----\n\n");
	Body += TEXT("A crack is a step in the drawn surface at a patch boundary. This is\n");
	Body += TEXT("the largest single-sample jump above the median one, so a hillside\n");
	Body += TEXT("does not read as a gap.\n\n");
	Body += FString::Printf(
		TEXT("worst jump                   %8.3f m at %.1f m along  %s\n\n"),
		WorstJump, WorstJumpAt,
		WorstJump <= 0.15 ? TEXT("(smooth)") : TEXT("<- a LOD boundary"));

	Body += TEXT("---- the near-field band on its own ----\n\n");
	Body += TEXT("The two columns above are both dominated by the landform under them.\n");
	Body += TEXT("This is their difference: what T429 put on the ground, and nothing else.\n\n");
	Body += FString::Printf(TEXT("RMS                          %8.3f m\n"), BandRms);
	Body += FString::Printf(TEXT("range over the profile       %8.3f m  (%.3f to %.3f)\n\n"),
		BandHigh - BandLow, BandLow, BandHigh);

	// **At the shared edges, with the mesh's own vertices.** The profile above
	// cannot tell a crack from two depths describing the same ground
	// differently; this can. T049 runs it with `-forcedepth`, which makes the
	// boundaries several levels deep that pure-distance LOD never produces.
	Body += TEXT("---- cracks at the shared edges ----\n\n");
	FString Gaps;
	const double WorstGapCm = Planet->MeasureEdgeGaps(Gaps);
	Body += Gaps;
	Body += FString::Printf(TEXT("the drawn patches meet: %s\n\n"),
		WorstGapCm <= 1.0 ? TEXT("yes, to within a centimetre") : TEXT("NO -- there is a crack"));
	UE_LOG(LogLedger, Log, TEXT("%s"), *Gaps);

	const bool bHolds = QuadPixels <= 40.0 && DrawnRms >= 0.15;
	Body += FString::Printf(TEXT("VERDICT: %s\n"), bHolds ? TEXT("PASS") : TEXT("FAIL"));

	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("out"), TEXT("near-field.txt")));
	FFileHelper::SaveStringToFile(Body, *Path);
	UE_LOG(LogLedger, Log, TEXT("near field -> %s"), *Path);
	UE_LOG(LogLedger, Log, TEXT("  %.2f m quads, %.0f px; mesh RMS %.3f m (band off: %.3f m)"),
		QuadMetres, QuadPixels, DrawnRms, CoarseRms);
	return bHolds;
}
