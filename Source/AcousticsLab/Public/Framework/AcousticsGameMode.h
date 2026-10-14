#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AcousticsGameMode.generated.h"

/** Project game mode: explorer pawn + acoustics controller in scenes; UI-only spectating in the main menu. */
UCLASS()
class ACOUSTICSLAB_API AAcousticsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAcousticsGameMode();

protected:
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};
