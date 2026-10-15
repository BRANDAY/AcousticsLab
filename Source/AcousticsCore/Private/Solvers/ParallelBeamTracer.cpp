#include "Solvers/ParallelBeamTracer.h"
#include "Solvers/BeamTreeBuild.h"

#include "AcousticsCore.h"
#include "Geometry/AcousticScene.h"
#include "HAL/Event.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/IQueuedWork.h"
#include "Misc/QueuedThreadPool.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <atomic>

DECLARE_CYCLE_STAT(TEXT("ParallelBeamTracer Run"), STAT_AcousticsParallelBeamTracerRun, STATGROUP_Acoustics);

namespace
{
	/** One ParallelFor call: the item counter, the body and the completion event the caller waits on. */
	struct FParallelJob
	{
		TFunctionRef<void(int32, int32)> Body;
		const int32 NumItems;
		std::atomic<int32> NextItem{ 0 };
		std::atomic<int32> RemainingWorkers;
		FEvent* Done;

		FParallelJob(TFunctionRef<void(int32, int32)> InBody, int32 InNumItems, int32 InNumWorkers, FEvent* InDone)
			: Body(InBody)
			, NumItems(InNumItems)
			, RemainingWorkers(InNumWorkers)
			, Done(InDone)
		{
		}

		void Work(int32 Worker)
		{
			for (;;)
			{
				const int32 Item = NextItem.fetch_add(1, std::memory_order_relaxed);
				if (Item >= NumItems)
				{
					break;
				}
				Body(Item, Worker);
			}
			FinishWorker();
		}

		void FinishWorker()
		{
			// The last worker wakes the caller; nothing of the job is touched after the trigger.
			if (RemainingWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				Done->Trigger();
			}
		}
	};

	/** The pool item of one worker: pulls items from the job until none is left. */
	class FParallelWork final : public IQueuedWork
	{
	public:
		FParallelJob* Job = nullptr;
		int32 Worker = 0;

		virtual void DoThreadedWork() override
		{
			Job->Work(Worker);
		}

		virtual void Abandon() override
		{
			// Only when the pool is destroyed with work still queued (never while a ParallelFor waits): count it as done.
			Job->FinishWorker();
		}
	};
}

FParallelBeamTracer::FParallelBeamTracer(int32 InNumThreads, bool bPoolForSingleThread)
	: NumThreads(ResolveNumThreads(InNumThreads))
{
	if (NumThreads > 1 || bPoolForSingleThread)
	{
		Pool = FQueuedThreadPool::Allocate();
		if (!Pool->Create(static_cast<uint32>(NumThreads), WorkerStackSize, TPri_Normal, TEXT("AcousticsBeamPool")))
		{
			UE_LOG(LogAcoustics, Warning, TEXT("ParallelBeamTracer: could not create a pool of %d threads; running on the calling thread"), NumThreads);
			delete Pool;
			Pool = nullptr;
			NumThreads = 1;
		}
	}
}

FParallelBeamTracer::~FParallelBeamTracer()
{
	if (Pool)
	{
		Pool->Destroy();
		delete Pool;
		Pool = nullptr;
	}
}

int32 FParallelBeamTracer::ResolveNumThreads(int32 Requested)
{
	const int32 Count = Requested > 0 ? Requested : FPlatformMisc::NumberOfCoresIncludingHyperthreads();
	return FMath::Clamp(Count, 1, 256);
}

void FParallelBeamTracer::ParallelFor(int32 NumItems, TFunctionRef<void(int32 Item, int32 Worker)> Body)
{
	if (NumItems <= 0)
	{
		return;
	}
	if (!Pool)
	{
		for (int32 Item = 0; Item < NumItems; ++Item)
		{
			Body(Item, 0);
		}
		return;
	}
	const int32 NumWorkers = FMath::Min(NumThreads, NumItems);
	FEvent* Done = FPlatformProcess::GetSynchEventFromPool(true);
	{
		FParallelJob Job(Body, NumItems, NumWorkers, Done);
		TArray<FParallelWork> Works;
		Works.SetNum(NumWorkers);
		for (int32 Worker = 0; Worker < NumWorkers; ++Worker)
		{
			Works[Worker].Job = &Job;
			Works[Worker].Worker = Worker;
		}
		for (FParallelWork& Work : Works)
		{
			Pool->AddQueuedWork(&Work);
		}
		Done->Wait();
	}
	FPlatformProcess::ReturnSynchEventToPool(Done);
}

TSharedPtr<FBeamTree> FParallelBeamTracer::BuildTree(const FAcousticScene& Scene, const FVector3d& Source, const FBeamTracerSettings& Settings, FSolverProgress* Progress)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FParallelBeamTracer::BuildTree);
	FBeamBuildContext Context;
	AcousticsBeamBuild::InitContext(Context, Scene, Source, Settings, Progress);
	if (Progress)
	{
		Progress->Add(Context.NumRootBeams - Context.EmittedRoots.Num());
	}

	// One task per root beam; the subtree of slot i always lands in Subtrees[i], whichever worker built it.
	TArray<FBeamTree> Subtrees;
	Subtrees.SetNum(Context.EmittedRoots.Num());
	ParallelFor(Subtrees.Num(), [&Context, &Subtrees, Progress](int32 Slot, int32 /*Worker*/)
	{
		AcousticsBeamBuild::BuildRootSubtree(Context, Slot, Subtrees[Slot]);
		if (Progress)
		{
			Progress->Add(1);
		}
	});
	return AcousticsBeamBuild::MergeSubtrees(Context, Subtrees);
}

void FParallelBeamTracer::QueryReceivers(const FAcousticScene& Scene, const FBeamTree& Tree, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings, FSimulationResult& Result, FSolverProgress* Progress)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FParallelBeamTracer::QueryReceivers);
	struct FScratch
	{
		TArray<FBeamContribution> Contributions;
		TArray<int32> SegmentMedia;
	};
	TArray<FScratch> Scratch;
	Scratch.SetNum(NumThreads);
	Result.Receivers.SetNum(Receivers.Num());
	ParallelFor(Receivers.Num(), [&](int32 Index, int32 Worker)
	{
		if (Progress && Progress->IsCancelRequested())
		{
			return;
		}
		AcousticsBeamBuild::QueryReceiver(Scene, Tree, Receivers[Index], Settings, Result.Receivers[Index], Scratch[Worker].Contributions, Scratch[Worker].SegmentMedia);
		if (Progress)
		{
			Progress->Add(1);
		}
	});
}

FSimulationResult FParallelBeamTracer::Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> InReceivers, const FBeamTracerSettings& Settings, FSolverProgress* Progress)
{
	SCOPE_CYCLE_COUNTER(STAT_AcousticsParallelBeamTracerRun);
	TRACE_CPUPROFILER_EVENT_SCOPE(FParallelBeamTracer::Run);

	const double StartTime = FPlatformTime::Seconds();
	FSimulationResult Result;
	Result.SolverName = FBeamTracer::Name();
	Result.SourcePosition = Source;
	Result.NumTriangles = Scene.NumTriangles();
	Result.Timing.NumWorkers = NumThreads;

	if (!Settings.Validate(Result.Error))
	{
		UE_LOG(LogAcoustics, Error, TEXT("BeamTracer: invalid settings: %s"), *Result.Error);
		return Result;
	}
	if (Settings.bUseBVH && !Scene.HasBVH() && Scene.NumTriangles() > 0)
	{
		Result.Error = TEXT("scene has no BVH for its triangles (call BuildBVH) and bUseBVH is set");
		UE_LOG(LogAcoustics, Error, TEXT("BeamTracer: %s"), *Result.Error);
		return Result;
	}

	TArray<FSolverReceiver> Receivers(InReceivers.GetData(), InReceivers.Num());
	for (FSolverReceiver& Receiver : Receivers)
	{
		if (!(Receiver.Radius > 0.0))
		{
			Receiver.Radius = Settings.ReceiverRadius;
		}
	}
	if (Progress)
	{
		Progress->Reset(FBeamTracerSettings::NumRootBeams(Settings.SubdivisionLevel) + Receivers.Num());
	}

	// The tree depends on the source and the scene only (SPEC §2.5.7); receivers are queries.
	TSharedPtr<FBeamTree> Tree = BuildTree(Scene, Source, Settings, Progress);
	Result.Timing.SetupSeconds = FPlatformTime::Seconds() - StartTime;
	Result.BeamTree = Tree;
	Result.NumRootBeams = Tree->NumRootBeams;
	Result.NumBeamNodes = Tree->Nodes.Num();
	if (Tree->bTruncated)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("BeamTracer: node budget of %d reached; the tree is incomplete (%s)"), Settings.MaxBeamNodes, *Tree->DescribeStats());
	}
	if (Tree->NumUnresolved > 0 && bLogResults)
	{
		UE_LOG(LogAcoustics, Warning, TEXT("BeamTracer: %d beam pieces could not be assigned to a nearest triangle and were dropped"), Tree->NumUnresolved);
	}
	if (Progress && Progress->IsCancelRequested())
	{
		Result.bCancelled = true;
		Result.Timing.TotalSeconds = FPlatformTime::Seconds() - StartTime;
		return Result;
	}

	const double TraceStart = FPlatformTime::Seconds();
	QueryReceivers(Scene, *Tree, Receivers, Settings, Result, Progress);
	Result.Timing.TraceSeconds = FPlatformTime::Seconds() - TraceStart;
	Result.Timing.MetricsSeconds = 0.0;
	Result.Timing.TotalSeconds = FPlatformTime::Seconds() - StartTime;
	if (Progress && Progress->IsCancelRequested())
	{
		Result.bCancelled = true;
		return Result;
	}
	Result.bSuccess = true;

	if (bLogResults)
	{
		UE_LOG(LogAcoustics, Log, TEXT("BeamTracer: %s; %d thread(s)%s, BVH %s, build %.3f s, queries %.3f s"), *Tree->DescribeStats(), NumThreads,
			Pool ? TEXT(" (pool)") : TEXT(" (inline)"), Settings.bUseBVH ? TEXT("on") : TEXT("off"), Result.Timing.SetupSeconds, Result.Timing.TraceSeconds);
		UE_LOG(LogAcoustics, Log, TEXT("%s"), *Result.Describe());
	}
	return Result;
}
