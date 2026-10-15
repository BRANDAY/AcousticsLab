#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IndustryLevelLibrary.generated.h"

/** Scripted setup of the industry levels (Tools/Editor/create_m9_content.py through the python commandlet or the editor). */
UCLASS()
class ACOUSTICSLAB_API UIndustryLevelLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Steam Audio "Export Static Geometry (Current Level)" without the menu and its file dialog: every actor with a Steam
	 * Audio Geometry component of the persistent level goes into the asset ObjectPath (/Game/Path/Name.Name), and a Steam
	 * Audio Static Mesh actor in the level points at it. SteamAudio::ExportStaticGeometryForLevel waits for game-thread
	 * tasks, so it runs on a worker thread while this call pumps the game thread (works in the editor and in a commandlet).
	 * False when the plugin was not present at build time (WITH_STEAM_AUDIO = 0) or the export failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Industry", meta = (WorldContext = "WorldContextObject"))
	static bool ExportSteamAudioStaticGeometry(UObject* WorldContextObject, const FString& ObjectPath);

	/** True when the game module was built against the Steam Audio plugin. */
	UFUNCTION(BlueprintPure, Category = "Acoustics|Industry")
	static bool IsSteamAudioAvailable();
};
