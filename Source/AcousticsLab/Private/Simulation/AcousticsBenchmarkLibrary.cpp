#include "Simulation/AcousticsBenchmarkLibrary.h"

#include "AcousticsCore.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Geometry/AcousticScene.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Scene/AcousticSource.h"
#include "Simulation/AcousticSimulationManager.h"

bool UAcousticsBenchmarkLibrary::PrepareJob(UWorld* World, const FBeamBenchmarkOptions& Options, FBeamBenchmarkJob& OutJob, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("no world");
		return false;
	}
	// In PIE a level without a manager gets one like for a Run; the editor world (headless runner) is never modified.
	AAcousticSimulationManager* Manager = World->IsGameWorld() ? AAcousticSimulationManager::FindOrSpawn(World) : AAcousticSimulationManager::Find(World);
	if (!Manager)
	{
		OutError = TEXT("the level has no AcousticSimulationManager");
		return false;
	}

	const FName PreviousSource = Manager->SourceId;
	if (!Options.SourceId.IsNone())
	{
		Manager->SourceId = Options.SourceId;
	}
	OutJob.Scene = MakeShared<FAcousticScene>();
	const bool bPrepared = Manager->PrepareBeamRun(*OutJob.Scene, OutJob.Source, OutJob.Receivers, OutJob.Settings, Options.bIncludeReceiverGrids, OutError);
	const AAcousticSource* SourceActor = Manager->ResolveSourceActor();
	const FString SourceName = SourceActor ? SourceActor->SourceId.ToString() : FString(TEXT("?"));
	Manager->SourceId = PreviousSource;
	if (!bPrepared)
	{
		return false;
	}

	if (Options.SubdivisionLevel >= 0)
	{
		OutJob.Settings.SubdivisionLevel = Options.SubdivisionLevel;
	}
	if (Options.MaxBeamOrder >= 0)
	{
		OutJob.Settings.MaxBeamOrder = Options.MaxBeamOrder;
	}
	if (Options.SurfaceDivisions > 1)
	{
		*OutJob.Scene = OutJob.Scene->Tessellated(Options.SurfaceDivisions);
	}
	if (!OutJob.Settings.Validate(OutError))
	{
		return false;
	}

	OutJob.Matrix = FBeamBenchmarkSettings();
	OutJob.Matrix.ThreadCounts = Options.ThreadCounts;
	OutJob.Matrix.Repetitions = FMath::Max(1, Options.Repetitions);
	OutJob.Matrix.bWithBVH = Options.bWithBVH;
	OutJob.Matrix.bWithoutBVH = Options.bWithoutBVH;
	if (!OutJob.Matrix.bWithBVH && !OutJob.Matrix.bWithoutBVH)
	{
		OutError = TEXT("neither BVH on nor BVH off selected");
		return false;
	}

	OutJob.LevelName = UGameplayStatics::GetCurrentLevelName(World, true);
	OutJob.Info = FBenchmarkInfo();
	OutJob.Info.SceneName = FString::Printf(TEXT("%s, source %s%s"), *OutJob.LevelName, *SourceName,
		Options.SurfaceDivisions > 1 ? *FString::Printf(TEXT(", surfaces split %d x %d"), Options.SurfaceDivisions, Options.SurfaceDivisions) : TEXT(""));
	OutJob.Info.SurfaceDivisions = FMath::Max(1, Options.SurfaceDivisions);
	OutJob.Info.Note = Options.Note;
	OutJob.Info.FillMachine();
	return true;
}

FString UAcousticsBenchmarkLibrary::MakeCsvBasePath(const FString& LevelName, const FDateTime& Time)
{
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Benchmarks"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	return Directory / FString::Printf(TEXT("%s_%s"), *LevelName, *Time.ToString(TEXT("%Y%m%d_%H%M%S")));
}

bool UAcousticsBenchmarkLibrary::SaveResult(const FBenchmarkResult& Result, const FString& LevelName, FString& OutRunsPath, FString& OutSummaryPath)
{
	const bool bSaved = Result.SaveCsv(MakeCsvBasePath(LevelName, Result.Info.Timestamp), OutRunsPath, OutSummaryPath);
	if (bSaved)
	{
		UE_LOG(LogAcoustics, Log, TEXT("Benchmark CSV written: %s (summary %s)"), *OutRunsPath, *OutSummaryPath);
	}
	else
	{
		UE_LOG(LogAcoustics, Error, TEXT("Benchmark CSV could not be written to %s"), *OutRunsPath);
	}
	return bSaved;
}

FString UAcousticsBenchmarkLibrary::RunBeamBenchmark(UObject* WorldContextObject, const FBeamBenchmarkOptions& Options, FString& OutCsvPath)
{
	OutCsvPath.Reset();
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	FBeamBenchmarkJob Job;
	FString Error;
	if (!PrepareJob(World, Options, Job, Error))
	{
		UE_LOG(LogAcoustics, Error, TEXT("Benchmark: %s"), *Error);
		return FString();
	}
	UE_LOG(LogAcoustics, Log, TEXT("Benchmark of %s: %s"), *Job.Info.SceneName, *Job.Scene->DescribeStats());
	const FBenchmarkResult Result = FBeamBenchmark::Run(*Job.Scene, Job.Source, Job.Receivers, Job.Settings, Job.Matrix, Job.Info);
	FString SummaryPath;
	SaveResult(Result, Job.LevelName, OutCsvPath, SummaryPath);
	return Result.MarkdownTable();
}
