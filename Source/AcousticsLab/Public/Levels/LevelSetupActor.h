#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LevelSetupActor.generated.h"

/** What a level is for. Drives the framework (menu vs. explorable scene) and, from M1 on, per-level solver defaults. */
UENUM(BlueprintType)
enum class EAcousticLevelKind : uint8
{
	MainMenu,
	Shoebox,
	Amphitheatre,
	Refraction,
	IndustrySteamAudio,
	IndustryAudioTracing
};

/** One per level: identifies the level and holds its configuration (SPEC §3.2, Levels/). */
UCLASS(Blueprintable, HideCategories = (Rendering, Replication, Collision, Input, HLOD, LOD, Cooking, Physics, Networking))
class ACOUSTICSLAB_API ALevelSetupActor : public AActor
{
	GENERATED_BODY()

public:
	ALevelSetupActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	EAcousticLevelKind LevelKind = EAcousticLevelKind::Shoebox;

	/** Shown in the HUD next to the map name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FText DisplayName;

	/** The level's setup actor, or nullptr when the level has none. */
	static ALevelSetupActor* Find(UWorld* World);
};
