// The gas giant, its rings, and the way down into it. T079 and T080.
//
// `-giant`. The world is built for the home planet as always; this hides it and
// puts the camera at the system's first gas giant instead, far enough from the
// planet's centre that its terrain streams nothing but the root patches. The
// giant is drawn from the fields the core already has -- the bands and storms
// (LedgerGiant), the rings and their shadow (LedgerRings), the pressure law and
// the crush depth (LedgerGas) -- so what is photographed is those functions and
// not a painting of them.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LedgerGiantWorld.generated.h"

class ACameraActor;
class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;

UCLASS()
class LEDGERHARNESS_API ULedgerGiantWorld : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	bool Build();
	void PaintGlobe(double Seconds, double HalfWidth = 0.0);
	void AimSun(double Seconds);
	void PlaceRubble(const FVector& Eye);
	void Finish();

	UPROPERTY()
	TObjectPtr<AActor> Holder;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Globe;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Ring;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Rubble;

	bool bRunning = false;
	bool bBuilt = false;
	int32 Giant = INDEX_NONE;

	/// The giant's centre in the world, cm, and its one-bar radius. The body
	/// frame is laid onto world axes, so the ring plane is world XY.
	FVector Centre = FVector::ZeroVector;
	double RadiusCm = 0.0;
	double When = 0.0;
	double HalfYear = 0.0;

	/// The densest ring radius, cm from the centre, and its optical depth.
	double DenseRadiusCm = 0.0;
	double DenseTau = 0.0;

	/// Where the descent goes down: the sharpest band boundary under the
	/// star, radians of latitude.
	double DescentLatitude = 0.0;

	int32 Shot = 0;
	double Settle = 0.0;
	bool bCaptured = false;
	FIntVector RubbleCell = FIntVector(TNumericLimits<int32>::Max());
	int32 RubbleNear = 0;
	int32 RubbleInView = 0;

	/// The descent: depth below the one-bar level, metres, stepped each tick.
	bool bDescending = false;
	double DepthMetres = -20000.0;
	double CrushedAtMetres = -1.0;
	double PredictedCrushMetres = 0.0;

	TArray<FString> Lines;
};
