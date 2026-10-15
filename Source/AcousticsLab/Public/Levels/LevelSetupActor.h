#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LevelSetupActor.generated.h"

class UAcousticSceneExtractorComponent;
class UAmphitheatrePositionsAsset;

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

/** One per level: identifies the level, owns the scene extractor and its material bindings (SPEC §3.2, Levels/). */
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

	/** Collects the level geometry for the solvers: tags "Acoustic" / "AcMat:<name>", material assets, default material. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TObjectPtr<UAcousticSceneExtractorComponent> SceneExtractor;

	/** Extracts the level, prints triangle count, area and materials to the Output Log (LogAcoustics) and draws the triangles for a few seconds. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Acoustics")
	void LogExtractedScene();

	/** Shared source / receiver positions (amphitheatre levels: DA_AmphitheatrePositions, SPEC §3.4); applied at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Acoustics|Positions")
	TObjectPtr<UAmphitheatrePositionsAsset> Positions;

	/** Moves the source and the receivers of this level to the positions of the Positions asset (editor button). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Acoustics|Positions")
	void ApplyPositions();

	/** The level's setup actor, or nullptr when the level has none. */
	static ALevelSetupActor* Find(UWorld* World);

protected:
	virtual void BeginPlay() override;
};
