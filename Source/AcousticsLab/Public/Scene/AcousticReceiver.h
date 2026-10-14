// Receiver placeholder: a sphere in the level that collects energy (SPEC §2.2.5, §3.2 Scene/).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AcousticReceiver.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/** Spherical receiver. The radius shape is drawn as a wireframe, the marker is a small sphere; neither is acoustic geometry. */
UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API AAcousticReceiver : public AActor
{
	GENERATED_BODY()

public:
	AAcousticReceiver();

	/** Identifier shown in the HUD and used in result names ("R1"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName ReceiverId;

	/** Receiver sphere radius in meters (SPEC default 0.5 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float RadiusMeters = 0.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UStaticMeshComponent> Marker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<USphereComponent> RadiusShape;

	virtual void OnConstruction(const FTransform& Transform) override;

	FVector3d GetPositionMeters() const;

	/** All receivers of the world, sorted by ReceiverId. */
	static TArray<AAcousticReceiver*> FindAll(UWorld* World);
};
