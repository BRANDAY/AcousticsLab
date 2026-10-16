#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AcousticsGameMode.generated.h"

/**
 * Project game mode: explorer pawn + acoustics controller in scenes; UI-only spectating in the main menu. In
 * ?mode=Soundwalk (menu 1.5) the player walks (AAcousticsWalkerPawn) and starts on the R2 seat of the amphitheatre.
 */
UCLASS()
class ACOUSTICSLAB_API AAcousticsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAcousticsGameMode();

	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

protected:
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

private:
	bool IsSoundwalk() const;
};
