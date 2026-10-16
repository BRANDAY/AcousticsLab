// The elliptical amphitheatre of Level_Amphitheatre (SPEC §3.4, M10): arena floor, 20 stepped cavea rows, optional
// audience and velarium. Built from the numbers of DA_AmphitheatrePositions so geometry and receivers agree.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AmphitheatreActor.generated.h"

class AReceiverGrid;
class UAmphitheatrePositionsAsset;
class UDynamicMeshComponent;
class UMaterialInterface;

/**
 * Three UDynamicMeshComponents built with GeometryCore (FDynamicMesh3, the mesh type behind Geometry Script):
 * - ArenaFloor: the arena ellipse (semi-axes a, b), single-sided, facing up; AcMat:<StoneMaterialName>.
 * - Cavea: one closed shell swept around the arena: row k (1..NumRows) has a riser at offset (k-1)*d from the arena
 *   ellipse, rising to k*h, and a tread from offset (k-1)*d to k*d; the back wall at offset NumRows*d and the ground
 *   face close it. "Offset o" is the ellipse with semi-axes (a + o, b + o), the same rule as
 *   UAmphitheatrePositionsAsset::GetSeatPositionMeters. AcMat:Audience when bAudience (SPEC: row absorption 0.7),
 *   otherwise the stone material.
 * - Velarium: a closed thin elliptical ring over the cavea at VelariumHeightMeters (open over the arena), AcMat:Absorber;
 *   without bVelarium the mesh is empty and the component carries no Acoustic tag (the extractor ignores visibility).
 * Meshes are in centimeters relative to the actor (the actor sits at the arena centre on the ground).
 */
UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Input, HLOD, LOD, Cooking, Networking))
class ACOUSTICSLAB_API AAmphitheatreActor : public AActor
{
	GENERATED_BODY()

public:
	AAmphitheatreActor();

	/** Arena / row numbers; empty = /Game/Acoustics/DA_AmphitheatrePositions, else the defaults below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UAmphitheatrePositionsAsset> Positions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bAudience = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bVelarium = false;

	/** Segments around the ellipse (each row ring has as many quads per face). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "16", ClampMax = "512"))
	int32 SegmentsAround = 96;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName StoneMaterialName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName AudienceMaterialName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName VelariumMaterialName;

	/** Underside of the velarium above the ground, m (the top row is at NumRows * rise = 15 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "1.0"))
	float VelariumHeightMeters = 18.0f;

	/** Grid of the heatmap (SPEC §3.4: all rows, GridSpacingMeters of the data asset). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName GridId;

	/** Render materials (optional); null = engine default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Rendering")
	TObjectPtr<UMaterialInterface> StoneRenderMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UDynamicMeshComponent> ArenaFloor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UDynamicMeshComponent> Cavea;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UDynamicMeshComponent> Velarium;

	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Acoustics")
	void Rebuild();

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void SetAudience(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	void SetVelarium(bool bEnabled);

	/** Fills Grid with the cavea points (plan step GridSpacingMeters, each lifted to its row's tread); returns the count. */
	UFUNCTION(BlueprintCallable, Category = "Acoustics")
	int32 ApplyGrid(AReceiverGrid* Grid) const;

	/** Applies the grid to the AReceiverGrid with GridId in this world. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Acoustics")
	void ApplyGridInWorld();

	/** Row (1..NumRows) under a plan point given relative to the arena centre, 0 inside the arena, -1 outside the cavea. */
	int32 RowAtPlanPoint(double XMeters, double YMeters) const;

	/** Effective numbers (data asset or defaults). */
	void GetDimensions(double& OutA, double& OutB, int32& OutRows, double& OutDepth, double& OutRise, double& OutGridStep) const;

	static AAmphitheatreActor* Find(UWorld* World);

protected:
	virtual void BeginPlay() override;

private:
	const UAmphitheatrePositionsAsset* ResolvePositions() const;
	void BuildFloor();
	void BuildCavea();
	void BuildVelarium();
	void ApplyTags();
};
