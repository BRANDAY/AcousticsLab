#include "Industry/IndustryLevelLibrary.h"

#include "AcousticsCore.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"

#if WITH_STEAM_AUDIO
// Plugins/SteamAudio/Source/SteamAudio/Private/SteamAudioScene.h (exported from the SteamAudio module, header not public).
namespace SteamAudio
{
	bool STEAMAUDIO_API ExportStaticGeometryForLevel(UWorld* World, ULevel* Level, FString FileName, bool bExportOBJ);
}
#endif

bool UIndustryLevelLibrary::IsSteamAudioAvailable()
{
	return WITH_STEAM_AUDIO != 0;
}

bool UIndustryLevelLibrary::ExportSteamAudioStaticGeometry(UObject* WorldContextObject, const FString& ObjectPath)
{
#if WITH_STEAM_AUDIO
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	ULevel* Level = World ? World->PersistentLevel.Get() : nullptr;
	if (!Level)
	{
		return false;
	}
	TFuture<bool> Result = Async(EAsyncExecution::Thread, [World, Level, ObjectPath]()
	{
		return SteamAudio::ExportStaticGeometryForLevel(World, Level, ObjectPath, false);
	});
	const double Start = FPlatformTime::Seconds();
	while (!Result.IsReady())
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::Sleep(0.005f);
	}
	const bool bOk = Result.Get();
	UE_LOG(LogAcoustics, Log, TEXT("Steam Audio static geometry of %s -> %s: %s (%.2f s)"), *World->GetMapName(), *ObjectPath, bOk ? TEXT("exported") : TEXT("FAILED"),
		FPlatformTime::Seconds() - Start);
	return bOk;
#else
	UE_LOG(LogAcoustics, Warning, TEXT("Steam Audio export: the game module was built without the Steam Audio plugin (Tools/Plugins/install_steam_audio.ps1)"));
	return false;
#endif
}
