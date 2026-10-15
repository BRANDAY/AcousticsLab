// Draws the beam tree of a result as translucent frustum meshes, one UDynamicMeshComponent per order (SPEC §3.6, "Пучки").
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Results/SimulationResult.h"
#include "BeamVisualizerComponent.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
struct FBeamNode;

/**
 * Every beam node becomes a frustum from its start cross-section (the source point for order 0, the fan triangle on
 * the reflecting wall otherwise) to its cross-section on the hit triangle; the meshes of one order share a
 * UDynamicMeshComponent (Engine/Source/Runtime/GeometryFramework/Public/Components/DynamicMeshComponent.h) with
 * per-vertex colors (order color, alpha = Opacity) rendered through M_BeamFrustum (unlit, translucent, two-sided).
 * "Only beams containing the receiver" draws the beams of FReceiverResult::ContainingBeamNodes of the filtered
 * receiver together with their ancestors (the chain of reflections that led there). Beams are colored by order in
 * medium 0 and by a second palette in every other medium (SPEC §3.6: the beam before and after an interface in
 * different colors); refracted beams are drawn a little more opaque. Meters -> centimeters here only.
 */
UCLASS(ClassGroup = (Acoustics), meta = (BlueprintSpawnableComponent))
class ACOUSTICSLAB_API UBeamVisualizerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBeamVisualizerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams")
	bool bEnabled = true;

	/** Beams above this order are not drawn (the HUD slider). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxDrawOrder = 1;

	/** Beams below this order are skipped (0 = draw from the root beams); with "only containing" it isolates the late branches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MinDrawOrder = 0;

	/** Draw only the beams that contain the filtered receiver (and the beams they descend from). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams")
	bool bOnlyBeamsContainingReceiver = false;

	/** Also draw the beams that leave the scene (they extend EscapeLength past the last surface and fill an open scene). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams")
	bool bDrawEscapedBeams = true;

	/** Receiver index in the result for the containment filter; -1 = any receiver. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams", meta = (ClampMin = "-1"))
	int32 ReceiverFilter = INDEX_NONE;

	/** At most this many beams per order (a level-3 tree of order 3 has tens of thousands). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams", meta = (ClampMin = "10", ClampMax = "100000"))
	int32 MaxBeamsPerOrder = 4000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams", meta = (ClampMin = "0.02", ClampMax = "1.0"))
	float Opacity = 0.22f;

	/** Beams containing the receiver are drawn with this opacity when the filter is on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams", meta = (ClampMin = "0.02", ClampMax = "1.0"))
	float HighlightOpacity = 0.55f;

	/** Opacity multiplier of beams produced by a refraction (M6), so the transmitted beams stand out from the reflected ones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams", meta = (ClampMin = "0.5", ClampMax = "4.0"))
	float RefractedOpacityScale = 1.8f;

	/** Also draw the start cross-section of reflected beams (the patch on the wall). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams")
	bool bDrawStartCaps = false;

	/** Translucent vertex-color material; empty = /Game/Acoustics/Materials/M_BeamFrustum (fallback: engine vertex-color material, opaque). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Beams")
	TObjectPtr<UMaterialInterface> FrustumMaterial;

	void SetResult(const TSharedPtr<const FSimulationResult>& InResult);
	TSharedPtr<const FSimulationResult> GetResult() const { return Result; }

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Beams")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Beams")
	void SetMaxDrawOrder(int32 InMaxOrder);
	void SetMinDrawOrder(int32 InMinOrder);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Beams")
	void SetOnlyBeamsContainingReceiver(bool bInOnly);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Beams")
	void SetReceiverFilter(int32 InReceiverIndex);

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Beams")
	void Rebuild();

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Beams")
	void Clear();

	UFUNCTION(BlueprintPure, Category = "Acoustics|Beams")
	int32 GetNumDrawnBeams() const { return NumDrawnBeams; }

	/** Beams of the result that pass the filters (before the per-order cap). */
	UFUNCTION(BlueprintPure, Category = "Acoustics|Beams")
	int32 GetNumMatchingBeams() const { return NumMatchingBeams; }

	UFUNCTION(BlueprintPure, Category = "Acoustics|Beams")
	int32 GetNumTriangles() const { return NumTriangles; }

	/** Highest order present in the result's tree, -1 without a tree. */
	int32 GetTreeMaxOrder() const;

	/** Order palette of URayPathVisualizerComponent in medium 0; cyan / blue / violet / magenta by order in medium 1, hue-shifted further for every other medium. */
	UFUNCTION(BlueprintPure, Category = "Acoustics|Beams")
	static FLinearColor ColorForOrderAndMedium(int32 Order, int32 Medium);

	bool HasTree() const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	UDynamicMeshComponent* GetOrCreateMesh(int32 Order);
	void DestroyMeshes();
	UMaterialInterface* ResolveMaterial();

	TSharedPtr<const FSimulationResult> Result;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> MeshPerOrder;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ResolvedMaterial;

	int32 NumDrawnBeams = 0;
	int32 NumMatchingBeams = 0;
	int32 NumTriangles = 0;
	bool bWarnedMaterial = false;
};
