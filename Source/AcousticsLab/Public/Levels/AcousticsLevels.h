#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Map names and URL options shared by the main menu and the game framework. All maps live in /Game/Maps. */
namespace AcousticsLevels
{
	inline constexpr const TCHAR* MainMenu = TEXT("Level_MainMenu");
	inline constexpr const TCHAR* Shoebox = TEXT("Level_Shoebox");
	inline constexpr const TCHAR* Amphitheatre = TEXT("Level_Amphitheatre");
	inline constexpr const TCHAR* Refraction = TEXT("Level_Refraction");
	inline constexpr const TCHAR* AmphitheatreSteamAudio = TEXT("Level_Amphitheatre_SteamAudio");
	inline constexpr const TCHAR* AmphitheatreAudioTracing = TEXT("Level_Amphitheatre_AudioTracing");

	/** URL option carrying the requested mode, e.g. "?mode=Benchmark". */
	inline constexpr const TCHAR* ModeOption = TEXT("mode");

	/** Full package path of a map, e.g. /Game/Maps/Level_Shoebox. */
	ACOUSTICSLAB_API FString MapPath(const FString& MapName);

	/** True when the world is the main-menu level (ALevelSetupActor decides when present, map name otherwise). */
	ACOUSTICSLAB_API bool IsMainMenuWorld(UWorld* World);
}
