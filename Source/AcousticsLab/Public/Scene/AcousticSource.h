// Sound source placeholder: a position in the level that solvers emit from (SPEC §3.2, Scene/).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AcousticSource.generated.h"

class UStaticMeshComponent;

/** Omnidirectional point source. The marker sphere is not part of the acoustic geometry (no "Acoustic" tag). */
UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API AAcousticSource : public AActor
{
	GENERATED_BODY()

public:
	AAcousticSource();

	/** Identifier shown in the HUD and used in result names ("S1"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName SourceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UStaticMeshComponent> Marker;

	/** World position in meters, the unit of the solvers. */
	FVector3d GetPositionMeters() const;

	/** All sources of the world, sorted by SourceId. */
	static TArray<AAcousticSource*> FindAll(UWorld* World);
};
