// The validation room of Level_Shoebox (SPEC §3.4): six single-sided quads with inward normals.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShoeboxRoomActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * Rectangular room built from six static-mesh quads (100 x 100 cm plane scaled to the wall size), each facing inward,
 * so the extracted scene is exactly 12 triangles. Walls are tagged "Acoustic" and "AcMat:<MaterialName>".
 * The room spans [-X/2, X/2] x [-Y/2, Y/2] x [0, Z] meters around the actor (floor at the actor's height).
 */
UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API AShoeboxRoomActor : public AActor
{
	GENERATED_BODY()

public:
	AShoeboxRoomActor();

	/** Inner size in meters: length (X), width (Y), height (Z). SPEC: 10 x 7 x 3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "0.5"))
	FVector RoomSizeMeters = FVector(10.0, 7.0, 3.0);

	/** Acoustic material of every wall, written into the tag "AcMat:<MaterialName>". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName MaterialName;

	/** Tag that marks the walls as acoustic geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName AcousticTag;

	/** Single-sided 100 x 100 cm quad whose front faces +Z. Defaults to the engine's BasicShapes/Plane. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UStaticMesh> WallMesh;

	/** Floor, Ceiling, WallXMin, WallXMax, WallYMin, WallYMax. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TArray<TObjectPtr<UStaticMeshComponent>> Walls;

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Re-applies mesh, transforms and tags to the six wall components. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Acoustics")
	void RebuildRoom();

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	double GetVolumeM3() const { return RoomSizeMeters.X * RoomSizeMeters.Y * RoomSizeMeters.Z; }

	UFUNCTION(BlueprintPure, Category = "Acoustics")
	double GetSurfaceAreaM2() const { return 2.0 * (RoomSizeMeters.X * RoomSizeMeters.Y + RoomSizeMeters.X * RoomSizeMeters.Z + RoomSizeMeters.Y * RoomSizeMeters.Z); }

	/** The room of the world, or nullptr. */
	static AShoeboxRoomActor* Find(UWorld* World);
};
