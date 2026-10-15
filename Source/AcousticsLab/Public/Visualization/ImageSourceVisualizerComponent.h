// Draws the image sources of a result as spheres (color by order) and the specular paths to the selected receiver (SPEC §3.6).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Results/SimulationResult.h"
#include "ImageSourceVisualizerComponent.generated.h"

class UInstancedStaticMeshComponent;
class ULineBatchComponent;
class UMaterialInterface;
class UStaticMesh;

/**
 * Spheres: one instance per image source (FSimulationResult::ImageSources of the image-source solver, or the apexes of
 * the beams containing the receiver for the beam tracer), colored by order like the ray paths (white, yellow, orange,
 * red) through M_HeatmapTile (unlit emissive from per-instance custom data); images without a valid path to the filtered
 * receiver are drawn dim or hidden. Paths: the early reflections of the filtered receiver as polylines source -> wall
 * points -> receiver, colored by order. Meters -> centimeters happens here only.
 */
UCLASS(ClassGroup = (Acoustics), meta = (BlueprintSpawnableComponent))
class ACOUSTICSLAB_API UImageSourceVisualizerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UImageSourceVisualizerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources")
	bool bEnabled = true;

	/** Hide image sources that have no valid path to the filtered receiver (otherwise they are drawn dim). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources")
	bool bOnlyValidForReceiver = true;

	/** Receiver index in the result whose paths are drawn; -1 = valid for any receiver, no paths. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources", meta = (ClampMin = "-1"))
	int32 ReceiverFilter = INDEX_NONE;

	/** Images and paths above this order are not drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxDrawOrder = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources", meta = (ClampMin = "100", ClampMax = "200000"))
	int32 MaxDrawImages = 20000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float SphereRadiusCm = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InvalidBrightness = 0.18f;

	/** 0 = one-pixel lines; > 0 = world-space thickness in centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float LineThickness = 1.5f;

	/** Sphere mesh; empty = /Engine/BasicShapes/Sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources")
	TObjectPtr<UStaticMesh> SphereMesh;

	/** Material reading the per-instance color; empty = /Game/Acoustics/Materials/M_HeatmapTile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|ImageSources")
	TObjectPtr<UMaterialInterface> SphereMaterial;

	void SetResult(const TSharedPtr<const FSimulationResult>& InResult);
	TSharedPtr<const FSimulationResult> GetResult() const { return Result; }

	UFUNCTION(BlueprintCallable, Category = "Acoustics|ImageSources")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|ImageSources")
	void SetOnlyValidForReceiver(bool bInOnly);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|ImageSources")
	void SetReceiverFilter(int32 InReceiverIndex);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|ImageSources")
	void SetMaxDrawOrder(int32 InMaxOrder);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|ImageSources")
	void Rebuild();

	UFUNCTION(BlueprintCallable, Category = "Acoustics|ImageSources")
	void Clear();

	UFUNCTION(BlueprintPure, Category = "Acoustics|ImageSources")
	int32 GetNumDrawnImages() const { return NumDrawnImages; }

	UFUNCTION(BlueprintPure, Category = "Acoustics|ImageSources")
	int32 GetNumValidImages() const { return NumValidImages; }

	UFUNCTION(BlueprintPure, Category = "Acoustics|ImageSources")
	int32 GetNumDrawnPaths() const { return NumDrawnPaths; }

	/** True when the result has something for this component (image sources or early reflections). */
	bool HasContent() const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	UInstancedStaticMeshComponent* GetOrCreateSpheres();
	ULineBatchComponent* GetOrCreateLineBatch();
	void AddSphere(const FVector3d& PositionMeters, const FLinearColor& Color);

	TSharedPtr<const FSimulationResult> Result;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Spheres;

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> LineBatch;

	int32 NumDrawnImages = 0;
	int32 NumValidImages = 0;
	int32 NumDrawnPaths = 0;
};
