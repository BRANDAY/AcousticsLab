#include "Levels/LevelSetupActor.h"

#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Levels/AmphitheatrePositionsAsset.h"
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

void ALevelSetupActor::ApplyPositions()
{
	if (Positions)
	{
		Modify();
		Positions->ApplyToWorld(GetWorld());
	}
}

void ALevelSetupActor::BeginPlay()
{
	Super::BeginPlay();
	// The asset is the single source of truth for every amphitheatre level; a level saved with older positions follows it.
	if (Positions)
	{
		Positions->ApplyToWorld(GetWorld());
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
