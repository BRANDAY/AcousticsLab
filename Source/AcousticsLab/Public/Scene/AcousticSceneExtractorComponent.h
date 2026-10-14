// Collects the acoustic geometry of a level into an FAcousticScene (SPEC §3.3).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AcousticSceneExtractorComponent.generated.h"

class UAcousticMaterialAsset;
class UDynamicMesh;
class UPrimitiveComponent;
class UStaticMesh;
struct FAcousticScene;

/** What an extraction found. Logged by "Log Extracted Scene", shown in the HUD from M2 on. */
USTRUCT(BlueprintType)
struct ACOUSTICSLAB_API FAcousticExtractionStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	int32 NumActors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	int32 NumStaticMeshComponents = 0;

	/** Instances of instanced static mesh components (each counted as one mesh copy). */
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	int32 NumInstances = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	int32 NumDynamicMeshComponents = 0;

	/** Tagged components that could not be used (no mesh, no CPU access, unsupported class). */
	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	int32 NumSkippedComponents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	int32 NumTriangles = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	int32 NumDegenerateTriangles = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	int32 NumMaterials = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	double TotalAreaM2 = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	FVector BoundsMinMeters = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	FVector BoundsMaxMeters = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	float ExtractionMilliseconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Acoustics")
	TArray<FString> Warnings;

	FString ToString() const;
};

/**
 * Walks the world for UStaticMeshComponent / UInstancedStaticMeshComponent / UDynamicMeshComponent tagged "Acoustic"
 * (component tag, or actor tag for all of its mesh components), reads LOD0 of the render data (static meshes need
 * CPU-accessible buffers: always true in uncooked builds, otherwise "Allow CPU Access" on the asset) or the dynamic mesh,
 * transforms to world space, converts cm to m and swaps the winding to the core convention.
 *
 * Material of a component: tag "AcMat:<name>" on the component, else on its actor, else MaterialByMeshName
 * (static mesh asset name, actor name or label), else DefaultMaterial (Concrete). A name resolves to the asset of
 * that MaterialName in MaterialLibrary / DefaultMaterial, else to the built-in preset of that name.
 */
UCLASS(ClassGroup = (Acoustics), meta = (BlueprintSpawnableComponent))
class ACOUSTICSLAB_API UAcousticSceneExtractorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAcousticSceneExtractorComponent();

	/** Components (or actors) carrying this tag take part in the simulation. */
	UPROPERTY(EditAnywhere, Category = "Acoustics|Tags")
	FName AcousticTag;

	/** Prefix of the material tag, followed by the material name. */
	UPROPERTY(EditAnywhere, Category = "Acoustics|Tags")
	FString MaterialTagPrefix;

	/** Material of every surface that names no other one; the built-in Concrete preset when unset. */
	UPROPERTY(EditAnywhere, Category = "Acoustics|Materials")
	TObjectPtr<UAcousticMaterialAsset> DefaultMaterial;

	/** Assets resolvable through the "AcMat:<MaterialName>" tag. */
	UPROPERTY(EditAnywhere, Category = "Acoustics|Materials")
	TArray<TObjectPtr<UAcousticMaterialAsset>> MaterialLibrary;

	/** Material by static mesh asset name, actor name or actor label, for meshes without a tag. */
	UPROPERTY(EditAnywhere, Category = "Acoustics|Materials")
	TMap<FName, TObjectPtr<UAcousticMaterialAsset>> MaterialByMeshName;

	UPROPERTY(EditAnywhere, Category = "Acoustics")
	bool bIncludeDynamicMeshes = true;

	/** Build the BVH at the end of the extraction (solvers expect it). */
	UPROPERTY(EditAnywhere, Category = "Acoustics")
	bool bBuildBVH = true;

	/** How long "Log Extracted Scene" draws the triangles and normals in the viewport; 0 disables drawing. */
	UPROPERTY(EditAnywhere, Category = "Acoustics|Debug", meta = (ClampMin = "0.0"))
	float DebugDrawSeconds = 10.0f;

	/** Result of the last "Log Extracted Scene". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Acoustics|Debug")
	FAcousticExtractionStats LastStats;

	/** Extracts the owner's world. Returns true when at least one triangle was found. */
	bool ExtractScene(FAcousticScene& OutScene, FAcousticExtractionStats& OutStats) const;

	bool ExtractFromWorld(UWorld* World, FAcousticScene& OutScene, FAcousticExtractionStats& OutStats) const;

	/** Appends LOD0 of a static mesh placed with ToWorld (UE units). Returns the number of triangles added. */
	static int32 AppendStaticMesh(const UStaticMesh* Mesh, const FTransform& ToWorld, int32 MaterialId, FAcousticScene& Scene, FAcousticExtractionStats& Stats);

	static int32 AppendDynamicMesh(const UDynamicMesh* Mesh, const FTransform& ToWorld, int32 MaterialId, FAcousticScene& Scene, FAcousticExtractionStats& Stats);

	/**
	 * Appends one triangle given in world centimeters with UE winding (front normal (P2 - P0) x (P1 - P0)).
	 * bMirrored: the placing transform has a negative determinant, which reverses the winding once more.
	 */
	static int32 AppendUETriangle(const FVector& P0, const FVector& P1, const FVector& P2, bool bMirrored, int32 MaterialId, FAcousticScene& Scene, FAcousticExtractionStats& Stats);

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	bool IsAcousticComponent(const UPrimitiveComponent* Component) const;

	/** Extracts the current world, logs the statistics (LogAcoustics) and draws the triangles for DebugDrawSeconds. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Acoustics")
	void LogExtractedScene();

	/** Draws every triangle (edges) and its normal (0.5 m) with debug lines. */
	static void DrawScene(const UWorld* World, const FAcousticScene& Scene, float Duration);

private:
	bool FindMaterialTag(const TArray<FName>& Tags, FName& OutName) const;
	const UAcousticMaterialAsset* FindMaterialAsset(FName Name) const;
	int32 ResolveMaterialId(const UPrimitiveComponent* Component, const UStaticMesh* Mesh, FAcousticScene& Scene, TMap<FName, int32>& MaterialIds, FAcousticExtractionStats& Stats) const;
};
