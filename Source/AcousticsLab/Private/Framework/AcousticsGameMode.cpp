#include "Framework/AcousticsGameMode.h"

#include "AcousticsCore.h"
#include "Framework/AcousticsExplorerPawn.h"
#include "Framework/AcousticsHUD.h"
#include "Framework/AcousticsPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Levels/AcousticsLevels.h"

AAcousticsGameMode::AAcousticsGameMode()
{
	DefaultPawnClass = AAcousticsExplorerPawn::StaticClass();
	PlayerControllerClass = AAcousticsPlayerController::StaticClass();
	HUDClass = AAcousticsHUD::StaticClass();
}

void AAcousticsGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (AcousticsLevels::IsMainMenuWorld(GetWorld()))
	{
		// The menu is pure UI: no explorer pawn, the player only spectates the empty level behind the widget.
		UE_LOG(LogAcoustics, Log, TEXT("Main menu level: player starts as spectator"));
		NewPlayer->StartSpectatingOnly();
		return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}
