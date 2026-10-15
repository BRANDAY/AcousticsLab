#include "Solvers/BeamBenchmark.h"

#include "AcousticsCore.h"
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Solvers/ParallelBeamTracer.h"

float FBenchmarkProgress::Fraction() const
{
	const int32 Total = TotalRuns.load(std::memory_order_relaxed);
	if (Total <= 0)
	{
		return 0.0f;
	}
	const float Inside = bWarmingUp.load(std::memory_order_relaxed) ? 0.0f : RunProgress.Fraction();
	return FMath::Clamp((CompletedRuns.load(std::memory_order_relaxed) + Inside) / static_cast<float>(Total), 0.0f, 1.0f);
}

void FBenchmarkProgress::CopyRuns(TArray<FBenchmarkRun>& OutRuns, TArray<int32>& OutThreadCounts) const
{
	FScopeLock Lock(&RunsLock);
	OutRuns = Runs;
	OutThreadCounts = ThreadCounts;
}

void FBenchmarkProgress::SetThreadCounts(const TArray<int32>& Counts)
{
	FScopeLock Lock(&RunsLock);
	ThreadCounts = Counts;
	Runs.Reset();
}

void FBenchmarkProgress::AddRun(const FBenchmarkRun& Run)
{
	FScopeLock Lock(&RunsLock);
	Runs.Add(Run);
}

FBenchmarkResult FBeamBenchmark::Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> Receivers, const FBeamTracerSettings& Settings,
	const FBeamBenchmarkSettings& Benchmark, const FBenchmarkInfo& Info, FBenchmarkProgress* Progress)
{
	const double WallStart = FPlatformTime::Seconds();
	FBenchmarkResult Result;
	Result.Info = Info;
	Result.Info.NumTriangles = Scene.NumTriangles();
	Result.Info.NumReceivers = Receivers.Num();
	Result.Info.SubdivisionLevel = Settings.SubdivisionLevel;
	Result.Info.MaxBeamOrder = Settings.MaxBeamOrder;
	Result.Info.NumRootBeams = FBeamTracerSettings::NumRootBeams(Settings.SubdivisionLevel);
	Result.Info.bRefraction = Settings.bRefraction && Scene.HasInterfaces();
	Result.Info.Repetitions = FMath::Max(1, Benchmark.Repetitions);
	if (Result.Info.LogicalCores == 0)
	{
		Result.Info.FillMachine();
	}
	Result.ThreadCounts = Benchmark.ThreadCounts.Num() > 0 ? Benchmark.ThreadCounts : FBenchmarkResult::DefaultThreadCounts(Result.Info.LogicalCores, Result.Info.PhysicalCores);
	for (int32& Count : Result.ThreadCounts)
	{
		Count = FParallelBeamTracer::ResolveNumThreads(Count);
	}

	TArray<bool> Modes;
	if (Benchmark.bWithBVH)
	{
		Modes.Add(true);
	}
	if (Benchmark.bWithoutBVH)
	{
		Modes.Add(false);
	}
	if (Progress)
	{
		Progress->CompletedRuns.store(0, std::memory_order_relaxed);
		Progress->TotalRuns.store(Result.Info.Repetitions * Modes.Num() * Result.ThreadCounts.Num(), std::memory_order_relaxed);
		Progress->SetThreadCounts(Result.ThreadCounts);
	}
	UE_LOG(LogAcoustics, Log, TEXT("Benchmark: %s, %d triangles, %d receivers, level %d, order %d, refraction %s; threads %s x BVH %s x %d repetitions"),
		*Result.Info.SceneName, Result.Info.NumTriangles, Result.Info.NumReceivers, Settings.SubdivisionLevel, Settings.MaxBeamOrder, Result.Info.bRefraction ? TEXT("on") : TEXT("off"),
		*FString::JoinBy(Result.ThreadCounts, TEXT("/"), [](int32 Count) { return FString::FromInt(Count); }), Modes.Num() == 2 ? TEXT("on/off") : (Modes.Num() == 1 && Modes[0] ? TEXT("on") : TEXT("off")), Result.Info.Repetitions);

	// One pool per thread count, created before any timed run (thread creation is not what S3 measures).
	TMap<int32, TUniquePtr<FParallelBeamTracer>> Tracers;
	for (const int32 Count : Result.ThreadCounts)
	{
		TUniquePtr<FParallelBeamTracer> Tracer = MakeUnique<FParallelBeamTracer>(Count, true);
		Tracer->bLogResults = false;
		Tracers.Add(Count, MoveTemp(Tracer));
	}

	FSolverProgress LocalProgress;
	FSolverProgress* RunProgress = Progress ? &Progress->RunProgress : &LocalProgress;
	auto Cancelled = [Progress]() { return Progress && Progress->IsCancelRequested(); };

	if (Benchmark.bWarmUp && Modes.Num() > 0 && !Cancelled())
	{
		if (Progress)
		{
			Progress->bWarmingUp.store(true, std::memory_order_relaxed);
			Progress->CurrentThreads.store(Result.ThreadCounts.Last(), std::memory_order_relaxed);
		}
		FBeamTracerSettings WarmUp = Settings;
		WarmUp.bUseBVH = true;
		Tracers[Result.ThreadCounts.Last()]->Run(Scene, Source, Receivers, WarmUp, RunProgress);
		if (Progress)
		{
			Progress->bWarmingUp.store(false, std::memory_order_relaxed);
		}
	}

	bool bHaveReference = false;
	uint64 Reference = 0;
	for (int32 Repetition = 0; Repetition < Result.Info.Repetitions && !Cancelled(); ++Repetition)
	{
		for (const bool bBVH : Modes)
		{
			for (const int32 Count : Result.ThreadCounts)
			{
				if (Cancelled())
				{
					break;
				}
				if (Progress)
				{
					Progress->CurrentThreads.store(Count, std::memory_order_relaxed);
					Progress->bCurrentBVH.store(bBVH, std::memory_order_relaxed);
					Progress->CurrentRepetition.store(Repetition, std::memory_order_relaxed);
				}
				FBeamTracerSettings RunSettings = Settings;
				RunSettings.bUseBVH = bBVH;
				RunSettings.NumThreads = Count;
				const FSimulationResult Single = Tracers[Count]->Run(Scene, Source, Receivers, RunSettings, RunProgress);
				if (Single.bCancelled || Cancelled())
				{
					break;
				}
				if (!Single.bSuccess)
				{
					UE_LOG(LogAcoustics, Error, TEXT("Benchmark: run failed (%d threads, BVH %s): %s"), Count, bBVH ? TEXT("on") : TEXT("off"), *Single.Error);
					Result.bCancelled = true;
					Result.WallSeconds = FPlatformTime::Seconds() - WallStart;
					return Result;
				}
				FBenchmarkRun Run;
				Run.Threads = Count;
				Run.bUseBVH = bBVH;
				Run.Repetition = Repetition;
				Run.TotalSeconds = Single.Timing.TotalSeconds;
				Run.BuildSeconds = Single.Timing.SetupSeconds;
				Run.QuerySeconds = Single.Timing.TraceSeconds;
				Run.NumNodes = Single.NumBeamNodes;
				Run.Fingerprint = FBenchmarkResult::Fingerprint(Single);
				if (!bHaveReference)
				{
					Reference = Run.Fingerprint;
					bHaveReference = true;
				}
				Run.bIdentical = Run.Fingerprint == Reference;
				Result.Runs.Add(Run);
				UE_LOG(LogAcoustics, Log, TEXT("Benchmark: %2d threads, BVH %-3s, repetition %d: %.3f s (build %.3f, queries %.3f), %d nodes, %s"), Count, bBVH ? TEXT("on") : TEXT("off"),
					Repetition + 1, Run.TotalSeconds, Run.BuildSeconds, Run.QuerySeconds, Run.NumNodes, Run.bIdentical ? TEXT("identical") : TEXT("DIFFERENT RESULT"));
				if (Progress)
				{
					Progress->AddRun(Run);
					Progress->CompletedRuns.fetch_add(1, std::memory_order_relaxed);
				}
			}
		}
	}
	Result.bCancelled = Cancelled();
	Result.WallSeconds = FPlatformTime::Seconds() - WallStart;
	UE_LOG(LogAcoustics, Log, TEXT("Benchmark finished in %.1f s%s, all results identical: %s\n%s"), Result.WallSeconds, Result.bCancelled ? TEXT(" (cancelled)") : TEXT(""),
		Result.AllIdentical() ? TEXT("yes") : TEXT("no"), *Result.DescribeTable());
	return Result;
}
