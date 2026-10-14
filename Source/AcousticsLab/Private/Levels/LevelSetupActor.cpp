#include "Levels/LevelSetupActor.h"

#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Scene/AcousticSceneExtractorComponent.h"

ALevelSetupActor::ALevelSetupActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneExtractor = CreateDefaultSubobject<UAcousticSceneExtractorComponent>(TEXT("SceneExtractor"));
}

void ALevelSetupActor::LogExtractedScene()
{
	if (SceneExtractor)
	{
		SceneExtractor->LogExtractedScene();
	}
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
