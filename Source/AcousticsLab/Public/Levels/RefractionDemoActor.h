// The two scenes of Level_Refraction (SPEC §2.6, §3.4): a pool with a water surface and the acoustic lens of Sikora 2012.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RefractionDemoActor.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Pool: floor and four Concrete walls (single-sided quads facing inward, like AShoeboxRoomActor) spanning
 * [-X/2, X/2] x [-Y/2, Y/2] x [0, Z] m around the actor, open at the top, with the water surface at WaterDepthMeters as an
 * interface quad (front = air above, back = water below; tags "Interface", "AcMediumFront:Air", "AcMediumBack:Water",
 * no collision). Lens: the test scene of Sikora 2012 §4.4 scaled x10 - a rubber sphere (UDynamicMeshComponent built
 * with FSphereGenerator, front = glycerol outside, back = rubber inside) at LensOffsetMeters + LensCentreOffsetMeters;
 * the source for it belongs at GetLensSourcePositionMeters() (LensSourceDistanceMeters in front of the sphere along -X),
 * the lens receivers in the axial plane. Both interfaces render with InterfaceMaterial (M_Interface, translucent).
 */
UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API ARefractionDemoActor : public AActor
{
	GENERATED_BODY()

public:
	ARefractionDemoActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Pool")
	bool bBuildPool = true;

	/** Inner size of the pool basin, m (SPEC: 20 x 10 x 3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Pool", meta = (ClampMin = "1.0"))
	FVector PoolSizeMeters = FVector(20.0, 10.0, 3.0);

	/** Height of the water surface above the pool floor, m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Pool", meta = (ClampMin = "0.1"))
	float WaterDepthMeters = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Pool")
	FName WallMaterialName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Lens")
	bool bBuildLens = true;

	/** Where the lens scene lives relative to the actor, m (far from the pool so the two demos do not see each other). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Lens")
	FVector LensOffsetMeters = FVector(0.0, 60.0, 2.0);

	/** Sphere centre relative to the lens origin (the source position), m: 7.5 m along +X as in the paper (0.75 m x 10). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Lens")
	FVector LensCentreOffsetMeters = FVector(7.5, 0.0, 0.0);

	/** Sphere radius, m (paper: diameter 0.1 m, here x10). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Lens", meta = (ClampMin = "0.05"))
	float LensRadiusMeters = 0.5f;

	/** Tessellation of the sphere (FSphereGenerator): vertices pole to pole and around. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Lens", meta = (ClampMin = "4", ClampMax = "128"))
	int32 LensNumPhi = 24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Lens", meta = (ClampMin = "6", ClampMax = "256"))
	int32 LensNumTheta = 48;

	/** Media around and inside the sphere (names of AcousticMediumPresets or of the extractor's MediaLibrary). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Lens")
	FName LensOutsideMedium;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Lens")
	FName LensInsideMedium;

	/** Translucent material of the interfaces; empty = /Game/Acoustics/Materials/M_Interface (fallback: the mesh default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UMaterialInterface> InterfaceMaterial;

	/** Single-sided 100 x 100 cm quad whose front faces +Z (engine BasicShapes/Plane). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UStaticMesh> QuadMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName AcousticTag;

	/** Floor, WallXMin, WallXMax, WallYMin, WallYMax. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Pool")
	TArray<TObjectPtr<UStaticMeshComponent>> PoolWalls;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Pool")
	TObjectPtr<UStaticMeshComponent> WaterSurface;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Lens")
	TObjectPtr<UDynamicMeshComponent> LensSphere;

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Re-applies meshes, transforms, tags and materials. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Acoustics")
	void RebuildScenes();

	/** World position (meters) where the pool source belongs: above the water, 1.5 m over the surface, a third along the pool. */
	UFUNCTION(BlueprintPure, Category = "Acoustics|Pool")
	FVector GetPoolSourcePositionMeters() const;

	/** World position (meters) of the lens source (the lens origin) and of the sphere centre. */
	UFUNCTION(BlueprintPure, Category = "Acoustics|Lens")
	FVector GetLensSourcePositionMeters() const;

	UFUNCTION(BlueprintPure, Category = "Acoustics|Lens")
	FVector GetLensCentreMeters() const;

	/** Paraxial focus of the ball lens measured from the sphere centre along the axis, m: n R / (2 (n - 1)), n = c_out / c_in. */
	UFUNCTION(BlueprintPure, Category = "Acoustics|Lens")
	float GetLensParaxialFocusMeters() const;

	static ARefractionDemoActor* Find(UWorld* World);

private:
	UMaterialInterface* ResolveInterfaceMaterial();
	void BuildPool();
	void BuildLens();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ResolvedInterfaceMaterial;
	bool bWarnedMaterial = false;
};
