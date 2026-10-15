// Thread-level parallelization of the beam tracer (SPEC §2.7, M7; Sikora & Mateljan 2013, "A method for speeding up
// beam-tracing simulation using thread-level parallelization", Engineering with Computers 30(4)).
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Results/SimulationResult.h"
#include "Solvers/BeamTracer.h"
#include "Templates/Function.h"

struct FAcousticScene;
class FQueuedThreadPool;

/**
 * The beam tracer of M5 / M6 with the tree build and the receiver queries spread over a dedicated thread pool with an
 * explicit thread count (FQueuedThreadPool; the task graph's ParallelFor cannot fix the number of threads).
 *
 * The root beams and their subtrees are independent (S3): one task = one root beam, handed out to the workers through an
 * atomic counter (dynamic load balancing - subtrees differ a lot in size). Every subtree is built breadth-first into its
 * own FBeamTree (own nodes, own counters); the subtrees are then merged order by order in root order, which is exactly
 * the order of the sequential breadth-first build, so the tree - and everything computed from it - is bit-identical for
 * any thread count and any scheduling. The receiver queries are spread the same way (one task = one receiver; each
 * receiver's histogram is written by one worker in node order). Pure C++; meters, seconds, double.
 *
 * The object owns the pool: create it once per thread count and run it as often as needed (the benchmark measures runs,
 * not thread creation). With bUseBVH off every triangle search is brute force (frustum candidates, centre / depth rays,
 * direct-sound occlusion): identical results, slower.
 */
class ACOUSTICSCORE_API FParallelBeamTracer
{
public:
	static const TCHAR* Name() { return TEXT("ParallelBeamTracer"); }

	/**
	 * NumThreads <= 0: all logical cores (FPlatformMisc::NumberOfCoresIncludingHyperthreads). With one thread and
	 * bPoolForSingleThread the work runs on a one-thread pool like every other count (benchmark: same code path for T1);
	 * otherwise inline on the caller.
	 */
	explicit FParallelBeamTracer(int32 NumThreads = 0, bool bPoolForSingleThread = true);
	~FParallelBeamTracer();

	FParallelBeamTracer(const FParallelBeamTracer&) = delete;
	FParallelBeamTracer& operator=(const FParallelBeamTracer&) = delete;

	int32 GetNumThreads() const { return NumThreads; }
	bool UsesPool() const { return Pool != nullptr; }

	/** Logs the tree statistics and the result summary after every Run (off for benchmark repetitions). */
	bool bLogResults = true;

	/**
	 * FBeamTracer::Run with the pool: Timing.SetupSeconds = tree build, Timing.TraceSeconds = receiver queries,
	 * Timing.TotalSeconds, Timing.NumWorkers = thread count. Progress counts root beams, then receivers.
	 */
	FSimulationResult Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings, FSolverProgress* Progress = nullptr);

	/** The beam tree of a source, built by NumThreads threads (one task per root beam), merged in root order. */
	TSharedPtr<FBeamTree> BuildTree(const FAcousticScene& Scene, const FVector3d& Source, const FBeamTracerSettings& Settings, FSolverProgress* Progress = nullptr);

	/** FBeamTracer::QueryReceivers with one task per receiver. */
	void QueryReceivers(const FAcousticScene& Scene, const FBeamTree& Tree, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings, FSimulationResult& Result, FSolverProgress* Progress = nullptr);

	/**
	 * Body(Item, Worker) for every Item in [0, NumItems), items handed out in ascending order through an atomic counter to
	 * min(NumThreads, NumItems) pool workers (Worker = 0 .. workers - 1, for per-worker scratch); returns when all are done.
	 * Inline (Worker 0) without a pool.
	 */
	void ParallelFor(int32 NumItems, TFunctionRef<void(int32 Item, int32 Worker)> Body);

	/** <= 0 -> logical cores; clamped to [1, 256]. */
	static int32 ResolveNumThreads(int32 Requested);

	/** Stack of the pool threads (the division keeps its polygons on the heap; the BVH traversal is iterative). */
	static constexpr uint32 WorkerStackSize = 256 * 1024;

private:
	int32 NumThreads = 1;
	FQueuedThreadPool* Pool = nullptr;
};
