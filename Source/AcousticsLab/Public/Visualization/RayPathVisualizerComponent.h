// Draws the sampled ray paths of a simulation result as batched lines (SPEC §3.6, "Лучи").
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Results/SimulationResult.h"
#include "RayPathVisualizerComponent.generated.h"

class ULineBatchComponent;

/**
 * One ULineBatchComponent per visualizer (its own batch, so the extractor's debug drawing and the engine line batcher
 * are untouched). Colors by reflection order (0 white, 1 yellow, 2 orange, 3+ red), alpha by the energy at the start of
 * the segment (0 dB -> 1, -EnergyRangeDb -> MinOpacity); "only paths reaching the receiver" keeps the paths that entered
 * the filtered receiver and cuts them at the entry point. Meters -> centimeters happens here only.
 */
UCLASS(ClassGroup = (Acoustics), meta = (BlueprintSpawnableComponent))
class ACOUSTICSLAB_API URayPathVisualizerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URayPathVisualizerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Paths")
	bool bEnabled = true;

	/** Keep only paths that entered the receiver (ReceiverFilter, or any receiver) within MaxDrawOrder reflections, and stop them at the entry point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Paths")
	bool bOnlyPathsReachingReceiver = false;

	/** Receiver index in the result the "reaching" filter refers to; -1 = any receiver. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Paths", meta = (ClampMin = "-1"))
	int32 ReceiverFilter = INDEX_NONE;

	/** At most this many paths are drawn (SPEC: 2000). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Paths", meta = (ClampMin = "0", ClampMax = "2000"))
	int32 MaxPaths = 2000;

	/** Segments with a reflection order above this are not drawn (2000 paths of order 10 already fill the view; a 500-order path is noise). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Paths", meta = (ClampMin = "0", ClampMax = "100000"))
	int32 MaxDrawOrder = 3;

	/** Hard budget of line segments per rebuild, for the frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Paths", meta = (ClampMin = "1000", ClampMax = "2000000"))
	int32 MaxDrawSegments = 200000;

	/** Energy below 0 dB that maps to MinOpacity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Paths", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float EnergyRangeDb = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Paths", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinOpacity = 0.08f;

	/** 0 = one-pixel hardware lines; > 0 = world-space thickness in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Paths", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float LineThickness = 0.0f;

	/** Takes (and keeps) the result and redraws. */
	void SetResult(const TSharedPtr<const FSimulationResult>& InResult);
	TSharedPtr<const FSimulationResult> GetResult() const { return Result; }

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Paths")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Paths")
	void SetOnlyPathsReachingReceiver(bool bInOnly);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Paths")
	void SetReceiverFilter(int32 InReceiverIndex);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Paths")
	void SetMaxDrawOrder(int32 InMaxOrder);

	/** Rebuilds the line batch from the current result and settings. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Paths")
	void Rebuild();

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Paths")
	void Clear();

	UFUNCTION(BlueprintPure, Category = "Acoustics|Paths")
	int32 GetNumDrawnPaths() const { return NumDrawnPaths; }

	UFUNCTION(BlueprintPure, Category = "Acoustics|Paths")
	int32 GetNumDrawnSegments() const { return NumDrawnSegments; }

	/** Paths of the result that pass the receiver filter (before the MaxPaths cap). */
	UFUNCTION(BlueprintPure, Category = "Acoustics|Paths")
	int32 GetNumMatchingPaths() const { return NumMatchingPaths; }

	/** White, yellow, orange, red for orders 0, 1, 2, 3+ (SPEC §3.6). */
	static FLinearColor ColorForOrder(int32 Order);

	/** Alpha for an energy relative to E0: 1 at 0 dB, MinOpacity at -EnergyRangeDb, linear in dB between. */
	static float OpacityForEnergy(double RelativeEnergy, float RangeDb, float MinAlpha);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	ULineBatchComponent* GetOrCreateLineBatch();

	TSharedPtr<const FSimulationResult> Result;

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> LineBatch;

	int32 NumDrawnPaths = 0;
	int32 NumDrawnSegments = 0;
	int32 NumMatchingPaths = 0;
};
