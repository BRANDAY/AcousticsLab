#include "Framework/AcousticsGameMode.h"

#include "AcousticsCore.h"
#include "Framework/AcousticsExplorerPawn.h"
#include "Framework/AcousticsHUD.h"
#include "Audio/SoundwalkComponent.h"
#include "Framework/AcousticsPlayerController.h"
#include "Framework/AcousticsWalkerPawn.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
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

bool AAcousticsGameMode::IsSoundwalk() const
{
	return UGameplayStatics::ParseOption(OptionsString, AcousticsLevels::ModeOption).Equals(TEXT("Soundwalk"), ESearchCase::IgnoreCase);
}

UClass* AAcousticsGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	return IsSoundwalk() ? AAcousticsWalkerPawn::StaticClass() : Super::GetDefaultPawnClassForController_Implementation(InController);
}

APawn* AAcousticsGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	FTransform Start;
	const float HalfHeight = GetDefault<AAcousticsWalkerPawn>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	if (IsSoundwalk() && USoundwalkComponent::ComputeStartTransform(GetWorld(), HalfHeight, Start))
	{
		// The level PlayerStart floats above the cavea for the overview; the walker starts on a seat instead.
		UE_LOG(LogAcoustics, Log, TEXT("Soundwalk: walker starts at %s"), *Start.GetLocation().ToString());
		return SpawnDefaultPawnAtTransform(NewPlayer, Start);
	}
	return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
}
