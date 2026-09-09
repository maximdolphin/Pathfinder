// The asset review loop, as a runnable command.
//
// Launch with `-turntable`. Nothing flies. A generated asset is moved onto a
// neutral stage far from the planet and photographed from eight angles, at
// three framings, under two lighting rigs, then the run exits.
//
// **Why this exists before the generators that need it.** Under ADR-0005 every
// asset in the game is iterated rather than authored, and iteration is only
// affordable when looking at the result is cheap and honest. It was neither:
// in one session three separate readings of the surface captures were wrong --
// a camera underground, an exposure in stops mistaken for an intensity, a
// camera standing inside a building -- and each cost an hour because the
// picture was believed before the arithmetic. So the fixture checks the things
// that were got wrong, rather than assuming them.
//
// **Why a stage rather than the world.** The planet has animating clouds, a
// moving sun and streaming terrain, none of which is reproducible frame to
// frame. Two runs of the same asset have to produce the same image or the
// contact sheet cannot be diffed, and a review loop that cannot be diffed
// cannot tell a change from noise.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerTurntable.generated.h"

class ACameraActor;
class ADirectionalLight;
class AStaticMeshActor;

UCLASS()
class LEDGERHARNESS_API ULedgerTurntable : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/// Builds the stage: backdrop, lights, camera. Once, on the first tick that
	/// has a subject to photograph.
	bool Stage();

	/// Points the camera at the subject for shot `Index`, and sets the rig.
	void Compose();

	/// Whether the camera is outside the subject's bounds, which is the check
	/// that would have caught photographing the inside of a hull.
	bool CameraIsOutside(FString& Why) const;

	void Finish();

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> Subject = nullptr;

	UPROPERTY()
	TObjectPtr<AStaticMeshActor> Backdrop = nullptr;

	/// The world's own sun, switched off for the duration and put back after,
	/// so that this fixture's two lights are the only directional lights in the
	/// scene and nothing has to be resolved by a brightness tie-break.
	UPROPERTY()
	TObjectPtr<ADirectionalLight> WorldSun = nullptr;
	float WorldSunIntensity = 0.0f;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> StageActors;

	bool bRunning = false;
	bool bStaged = false;
	int32 Index = 0;

	/// Counted in **frames**, not seconds. Lumen accumulates temporally, so a
	/// fixed number of frames from a fixed state is reproducible and a fixed
	/// number of seconds is not.
	int32 SettleFrames = 0;

	/// Frames burned once, before the first capture, letting the renderer
	/// reach a steady state so that two runs agree from the first shot.
	int32 WarmUpFrames = 0;
	bool bCaptured = false;

	/// Where the subject's centre sits, and how big it is. Measured once so
	/// every framing is a multiple of the same number.
	FVector StageOrigin = FVector::ZeroVector;
	double SubjectRadius = 100.0;
	FVector SubjectExtent = FVector::ZeroVector;

	int32 Outside = 0;
	int32 Inside = 0;
};
