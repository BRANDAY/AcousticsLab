// The thread-count x BVH benchmark of the parallel beam tracer (SPEC §2.7, §5 experiment 4, M7).
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "HAL/CriticalSection.h"
#include "Results/BenchmarkResult.h"
#include "Results/SimulationResult.h"
#include "Solvers/BeamTracer.h"
#include <atomic>

struct FAcousticScene;

/** Matrix of the benchmark. */
struct ACOUSTICSCORE_API FBeamBenchmarkSettings
{
	/** Empty: FBenchmarkResult::DefaultThreadCounts of this machine. */
	TArray<int32> ThreadCounts;

	bool bWithBVH = true;
	bool bWithoutBVH = true;
	int32 Repetitions = 3;

	/** One untimed run (all threads, BVH on) first, so the first timed run does not pay for cold caches and page faults. */
	bool bWarmUp = true;
};

/** Progress of a running matrix, polled by the UI; cancel between and inside runs. */
struct ACOUSTICSCORE_API FBenchmarkProgress
{
	std::atomic<int32> CompletedRuns{ 0 };
	std::atomic<int32> TotalRuns{ 0 };
	std::atomic<int32> CurrentThreads{ 0 };
	std::atomic<bool> bCurrentBVH{ true };
	std::atomic<int32> CurrentRepetition{ 0 };
	std::atomic<bool> bWarmingUp{ false };
	std::atomic<bool> bCancelRequested{ false };

	/** Progress inside the current run (root beams, then receivers). */
	FSolverProgress RunProgress;

	void RequestCancel()
	{
		bCancelRequested.store(true, std::memory_order_relaxed);
		RunProgress.RequestCancel();
	}

	bool IsCancelRequested() const { return bCancelRequested.load(std::memory_order_relaxed); }

	/** 0..1 over the whole matrix. */
	float Fraction() const;

	/** The thread counts of the matrix and the runs finished so far (a live table while the matrix runs). */
	void CopyRuns(TArray<FBenchmarkRun>& OutRuns, TArray<int32>& OutThreadCounts) const;

	/** Called by FBeamBenchmark::Run. */
	void SetThreadCounts(const TArray<int32>& Counts);
	void AddRun(const FBenchmarkRun& Run);

private:
	mutable FCriticalSection RunsLock;
	TArray<FBenchmarkRun> Runs;
	TArray<int32> ThreadCounts;
};

/**
 * Runs the parallel beam tracer on one scene for every thread count x {BVH on, BVH off} x repetitions. One
 * FParallelBeamTracer (one pool) per thread count is created before the timed runs; repetitions are interleaved
 * (repetition 1 of every cell, then repetition 2, ...) so a slow drift of the machine spreads over all cells. Every
 * run's result is fingerprinted and compared with the first one: the parallel build must give the same tree and the
 * same histograms for any thread count and with or without the BVH. Pure C++, blocking; call it on a background thread.
 */
class ACOUSTICSCORE_API FBeamBenchmark
{
public:
	static FBenchmarkResult Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings,
		const FBeamBenchmarkSettings& Benchmark, const FBenchmarkInfo& Info, FBenchmarkProgress* Progress = nullptr);
};
