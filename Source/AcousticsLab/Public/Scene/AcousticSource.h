// Sound source placeholder: a position in the level that solvers emit from (SPEC §3.2, Scene/).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AcousticSource.generated.h"

class UArrowComponent;
class UStaticMeshComponent;

/**
 * Point source, omnidirectional or (M6, the lens test of Sikora 2012) restricted to a cone around the actor's forward
 * vector. MediumName names the medium the source sits in (SPEC §2.6: delays use its speed of sound until the first
 * refraction). The marker sphere is not part of the acoustic geometry (no "Acoustic" tag).
 */
UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API AAcousticSource : public AActor
{
	GENERATED_BODY()

public:
	AAcousticSource();

	/** Identifier shown in the HUD and used in result names ("S1"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName SourceId;

	/** Medium the source sits in: a name of AcousticMediumPresets (Air, Water, Glycerol, Rubber) or of the extractor's MediaLibrary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName MediumName;

	/** Emit only into a cone of ConeHalfAngleDeg around the actor's forward (+X) vector; the energy outside is discarded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Directivity")
	bool bDirectional = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Directivity", meta = (ClampMin = "0.5", ClampMax = "180.0", EditCondition = "bDirectional"))
	float ConeHalfAngleDeg = 10.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UStaticMeshComponent> Marker;

	/** Shows the cone axis of a directional source in the editor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UArrowComponent> DirectionArrow;

	/** World position in meters, the unit of the solvers. */
	FVector3d GetPositionMeters() const;

	/** Unit cone axis in world space (the actor's forward vector). */
	FVector3d GetDirection() const;

	virtual void OnConstruction(const FTransform& Transform) override;

	/** All sources of the world, sorted by SourceId. */
	static TArray<AAcousticSource*> FindAll(UWorld* World);
};
