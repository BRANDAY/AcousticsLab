// First-person walker of the soundwalk mode (menu 1.5, SPEC §0, §3.7): walks the amphitheatre rows with gravity.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AcousticsWalkerPawn.generated.h"

class UCameraComponent;

/**
 * A character with a first-person camera at standing ear height. MaxStepHeight covers one cavea riser (0.75 m), so WASD
 * climbs and descends the rows; Space jumps. Input is routed by AAcousticsPlayerController like for the explorer pawn.
 * The camera is the live receiver of USoundwalkComponent.
 */
UCLASS()
class ACOUSTICSLAB_API AAcousticsWalkerPawn : public ACharacter
{
	GENERATED_BODY()

public:
	AAcousticsWalkerPawn();

	/** X = right, Y = forward, relative to the view yaw. */
	void Move(const FVector2D& Axis);
	/** > 0 jumps (Space / E). */
	void MoveUp(float Axis);
	/** X = yaw, Y = pitch. */
	void Look(const FVector2D& Axis);

	UCameraComponent* GetCamera() const { return Camera; }

	/** Walking speed, cm/s (a slow stroll along the rows). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Walker")
	float WalkSpeed = 300.0f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Walker")
	TObjectPtr<UCameraComponent> Camera;
};
