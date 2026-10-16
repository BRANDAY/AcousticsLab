// Virtual ambient sound source of the soundwalk (S5: "virtual sound sources in the scene of the monument").
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SoundwalkAmbientSource.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * A looping sound at a fixed place of the level (crowd, birds, wind), spatialized and attenuated by the engine (3D
 * panning + distance falloff). It is not convolved with our impulse responses: those belong to source S1 at the arena
 * centre, and every ambient source would need its own run (documented simplification). Silent outside the soundwalk
 * unless bPlayOutsideSoundwalk; USoundwalkComponent starts and stops it.
 */
UCLASS(HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API ASoundwalkAmbientSource : public AActor
{
	GENERATED_BODY()

public:
	ASoundwalkAmbientSource();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Ambient")
	TObjectPtr<USoundBase> Sound;

	/** Shown in the HUD ("crowd", "birds", "wind"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Ambient")
	FString Label;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Ambient", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Volume = 0.5f;

	/** Full level inside this radius, m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Ambient", meta = (ClampMin = "0.0"))
	float InnerRadiusMeters = 5.0f;

	/** Falloff distance beyond the inner radius, m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Ambient", meta = (ClampMin = "1.0"))
	float FalloffMeters = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Ambient")
	bool bPlayOutsideSoundwalk = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics|Ambient")
	TObjectPtr<UAudioComponent> Audio;

	UFUNCTION(BlueprintCallable, Category = "Acoustics|Ambient")
	void SetPlaying(bool bPlay);

	UFUNCTION(BlueprintPure, Category = "Acoustics|Ambient")
	bool IsPlaying() const;

	static TArray<ASoundwalkAmbientSource*> FindAll(UWorld* World);

protected:
	virtual void BeginPlay() override;
};
