#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "AcousticsExplorerPawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class USphereComponent;

/** First-person flying camera used to move through the acoustic scenes. Input is routed by AAcousticsPlayerController. */
UCLASS()
class ACOUSTICSLAB_API AAcousticsExplorerPawn : public APawn
{
	GENERATED_BODY()

public:
	AAcousticsExplorerPawn();

	/** X = right, Y = forward, relative to the view direction. */
	void Move(const FVector2D& Axis);
	void MoveUp(float Axis);
	/** X = yaw, Y = pitch. */
	void Look(const FVector2D& Axis);

	UCameraComponent* GetCamera() const { return Camera; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Explorer")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Explorer")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Explorer")
	TObjectPtr<UFloatingPawnMovement> Movement;
};
