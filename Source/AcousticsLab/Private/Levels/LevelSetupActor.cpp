#include "Levels/LevelSetupActor.h"

#include "Components/SceneComponent.h"
#include "EngineUtils.h"

ALevelSetupActor::ALevelSetupActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ALevelSetupActor* ALevelSetupActor::Find(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<ALevelSetupActor> It(World);
	return It ? *It : nullptr;
}
