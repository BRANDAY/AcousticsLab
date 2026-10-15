// Result of the thread / BVH benchmark of the beam tracer (SPEC §2.7, §3.2 FBenchmarkResult, M7).
#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

struct FSimulationResult;

/** One timed run of the matrix. */
struct ACOUSTICSCORE_API FBenchmarkRun
{
	int32 Threads = 1;
	bool bUseBVH = true;
	int32 Repetition = 0;

	/** Build + queries, the tree build alone (Timing.SetupSeconds) and the receiver queries alone (Timing.TraceSeconds). */
	double TotalSeconds = 0.0;
	double BuildSeconds = 0.0;
	double QuerySeconds = 0.0;

	int32 NumNodes = 0;

	/** FBenchmarkResult::Fingerprint of the run's result; bIdentical = equal to the first recorded run. */
	uint64 Fingerprint = 0;
	bool bIdentical = true;
};

/** The repetitions of one (threads, BVH) cell. */
struct ACOUSTICSCORE_API FBenchmarkCell
{
	int32 Threads = 1;
	bool bUseBVH = true;
	int32 NumRuns = 0;
	double MeanSeconds = 0.0;
	double MinSeconds = 0.0;
	double MaxSeconds = 0.0;
	double StdDevSeconds = 0.0;
	double MeanBuildSeconds = 0.0;
	double MeanQuerySeconds = 0.0;

	/** T1 / Tn of the same BVH mode, from the means and from the minima (T1 = the smallest thread count of the matrix). */
	double Speedup = 0.0;
	double SpeedupMin = 0.0;

	/** Speedup / (Threads / T1 threads). */
	double Efficiency = 0.0;

	/** Brute-force mean / BVH mean at the same thread count (0 when the other mode was not measured). */
	double BvhGain = 0.0;

	bool bAllIdentical = true;
};

/** What was measured and on which machine. */
struct ACOUSTICSCORE_API FBenchmarkInfo
{
	FString SceneName;
	int32 NumTriangles = 0;
	int32 NumReceivers = 0;
	int32 SubdivisionLevel = 0;
	int32 MaxBeamOrder = 0;
	int32 NumRootBeams = 0;
	bool bRefraction = false;

	/** Every triangle split into SurfaceDivisions^2 (FAcousticScene::Tessellated); 1 = the scene as extracted. */
	int32 SurfaceDivisions = 1;
	int32 Repetitions = 0;

	FString CpuBrand;
	int32 PhysicalCores = 0;
	int32 LogicalCores = 0;
	double TotalPhysicalGB = 0.0;
	double AvailablePhysicalGB = 0.0;
	FDateTime Timestamp;

	/** Free text (machine state, how the run was started). */
	FString Note;

	/** CPU brand, cores and memory of this machine (FPlatformMisc / FPlatformMemory), timestamp now. */
	void FillMachine();
};

struct ACOUSTICSCORE_API FBenchmarkResult
{
	FBenchmarkInfo Info;
	TArray<int32> ThreadCounts;
	TArray<FBenchmarkRun> Runs;
	bool bCancelled = false;

	/** Wall time of the whole matrix including the warm-up and the fingerprints. */
	double WallSeconds = 0.0;

	/** One cell per (thread count, BVH mode) that has runs, BVH on first, threads ascending. */
	TArray<FBenchmarkCell> Summarize() const;

	/** Every run has the fingerprint of the first one (results independent of threads and of the BVH). */
	bool AllIdentical() const;

	/** Raw runs, one row each, with "# " header lines describing the scene and the machine. */
	FString RunsToCsv() const;

	/** The cells of Summarize(). */
	FString SummaryToCsv() const;

	/** Fixed-width text table of the cells (panel, log). */
	FString DescribeTable() const;

	/** The same table in Markdown (docs/milestones/M7.md). */
	FString MarkdownTable() const;

	/** Writes <BasePath>.csv (runs) and <BasePath>_summary.csv; false when a file could not be written. */
	bool SaveCsv(const FString& BasePath, FString& OutRunsPath, FString& OutSummaryPath) const;

	/**
	 * The thread counts of the matrix: 1, 2, 4, 8, ... below LogicalCores, plus PhysicalCores and LogicalCores themselves
	 * (on 6 cores / 12 threads: 1, 2, 4, 6, 8, 12) - the physical count is where the scaling is expected to bend.
	 */
	static TArray<int32> DefaultThreadCounts(int32 LogicalCores, int32 PhysicalCores);

	/** FNV-1a over the beam tree (node geometry, factors, links) and every receiver histogram (bits of the doubles). */
	static uint64 Fingerprint(const FSimulationResult& Result);
};
