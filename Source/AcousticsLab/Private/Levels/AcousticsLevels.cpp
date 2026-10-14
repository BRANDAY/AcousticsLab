#include "Levels/AcousticsLevels.h"

#include "Kismet/GameplayStatics.h"
#include "Levels/LevelSetupActor.h"

FString AcousticsLevels::MapPath(const FString& MapName)
{
	return FString::Printf(TEXT("/Game/Maps/%s"), *MapName);
}

bool AcousticsLevels::IsMainMenuWorld(UWorld* World)
{
	if (!World)
	{
		return false;
	}
	if (const ALevelSetupActor* Setup = ALevelSetupActor::Find(World))
	{
		return Setup->LevelKind == EAcousticLevelKind::MainMenu;
	}
	return UGameplayStatics::GetCurrentLevelName(World, true) == MainMenu;
}
