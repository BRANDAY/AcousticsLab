// Beam-tracer benchmark of the current level (SPEC §2.7, M7): shared by the benchmark panel (background task) and the
// headless runner (Tools/Benchmark/run_m7_benchmark.py through the python commandlet, editor world).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Results/BenchmarkResult.h"
#include "Solvers/BeamBenchmark.h"
#include "AcousticsBenchmarkLibrary.generated.h"

struct FAcousticScene;

/** What to benchmark on the level. Negative level / order: the simulation manager's settings. */
USTRUCT(BlueprintType)
struct ACOUSTICSLAB_API FBeamBenchmarkOptions
{
	GENERATED_BODY()

	/** Icosahedron subdivisions of the emission (20 * 4^level root beams); -1 = the manager's BeamSubdivisionLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark", meta = (ClampMin = "-1", ClampMax = "7"))
	int32 SubdivisionLevel = -1;

	/** Depth of the beam tree; -1 = the manager's MaxBeamOrder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark", meta = (ClampMin = "-1", ClampMax = "10"))
	int32 MaxBeamOrder = -1;

	/**
	 * Node budget of the tree; 0 = the manager setting. A tree cut by the budget depends on the scheduling once the
	 * subtrees exceed twice the budget (M7), so a benchmark must stay below it (M10: the amphitheatre needs 1.2 M nodes
	 * at level 2, order 2).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark", meta = (ClampMin = "0"))
	int32 MaxBeamNodes = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark", meta = (ClampMin = "1", ClampMax = "20"))
	int32 Repetitions = 3;

	/** Every triangle of the extracted scene split into n x n coplanar ones (FAcousticScene::Tessellated): same room, more triangles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark", meta = (ClampMin = "1", ClampMax = "32"))
	int32 SurfaceDivisions = 1;

	/** Grid receivers are queried too (point receivers always). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark")
	bool bIncludeReceiverGrids = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark")
	bool bWithBVH = true;

	/** Brute-force triangle search as the second mode (SPEC §2.7: the acceleration structure is the second source of speed-up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark")
	bool bWithoutBVH = true;

	/** Empty: 1, 2, 4, ... below the logical core count, plus the physical and the logical count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark")
	TArray<int32> ThreadCounts;

	/** Source to emit from; None = the manager's SourceId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark")
	FName SourceId;

	/** Stored in the CSV header (machine state, how the run was started). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Benchmark")
	FString Note;
};

/** Inputs of one matrix, copied out of the world so the matrix can run on any thread. */
struct ACOUSTICSLAB_API FBeamBenchmarkJob
{
	TSharedPtr<FAcousticScene> Scene;
	FVector3d Source = FVector3d::ZeroVector;
	TArray<FSolverReceiver> Receivers;
	FBeamTracerSettings Settings;
	FBeamBenchmarkSettings Matrix;
	FBenchmarkInfo Info;
	FString LevelName;
};

UCLASS()
class ACOUSTICSLAB_API UAcousticsBenchmarkLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Extracts the level through its simulation manager exactly like a beam-tracer Run (scene, source, receivers, source
	 * medium / cone, refraction setting) and applies the options. False with a message when the level cannot be run.
	 */
	static bool PrepareJob(UWorld* World, const FBeamBenchmarkOptions& Options, FBeamBenchmarkJob& OutJob, FString& OutError);

	/** <Project>/Saved/Benchmarks/<Level>_<yyyymmdd_hhmmss> (without extension). */
	static FString MakeCsvBasePath(const FString& LevelName, const FDateTime& Time);

	/** Writes <base>.csv (runs) and <base>_summary.csv and logs the paths. */
	static bool SaveResult(const FBenchmarkResult& Result, const FString& LevelName, FString& OutRunsPath, FString& OutSummaryPath);

	/**
	 * Headless benchmark of the world's level: prepares, runs the matrix on the calling thread, writes the CSVs and returns
	 * the summary as a Markdown table (empty on failure; OutCsvPath = the runs CSV). Meant for the python commandlet with
	 * the map loaded in the editor world (Tools/Benchmark/run_m7_benchmark.py), where nothing else competes for the CPU.
	 */
	UFUNCTION(BlueprintCallable, Category = "Acoustics|Benchmark", meta = (WorldContext = "WorldContextObject"))
	static FString RunBeamBenchmark(UObject* WorldContextObject, const FBeamBenchmarkOptions& Options, FString& OutCsvPath);
};
