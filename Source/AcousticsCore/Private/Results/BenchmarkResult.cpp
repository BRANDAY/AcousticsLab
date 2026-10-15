#include "Results/BenchmarkResult.h"

#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Results/SimulationResult.h"
#include "Solvers/BeamTracer.h"

namespace
{
	constexpr uint64 FnvOffset = 0xcbf29ce484222325ull;
	constexpr uint64 FnvPrime = 0x100000001b3ull;

	void HashBytes(uint64& Hash, const void* Data, int64 Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		for (int64 Index = 0; Index < Size; ++Index)
		{
			Hash ^= Bytes[Index];
			Hash *= FnvPrime;
		}
	}

	template <typename T>
	void HashValue(uint64& Hash, const T& Value)
	{
		HashBytes(Hash, &Value, sizeof(T));
	}

	void HashVector(uint64& Hash, const FVector3d& V)
	{
		HashValue(Hash, V.X);
		HashValue(Hash, V.Y);
		HashValue(Hash, V.Z);
	}

	const TCHAR* OnOff(bool bValue)
	{
		return bValue ? TEXT("on") : TEXT("off");
	}
}

void FBenchmarkInfo::FillMachine()
{
	CpuBrand = FPlatformMisc::GetCPUBrand().TrimStartAndEnd();
	PhysicalCores = FPlatformMisc::NumberOfCores();
	LogicalCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	TotalPhysicalGB = static_cast<double>(Stats.TotalPhysical) / (1024.0 * 1024.0 * 1024.0);
	AvailablePhysicalGB = static_cast<double>(Stats.AvailablePhysical) / (1024.0 * 1024.0 * 1024.0);
	Timestamp = FDateTime::Now();
}

TArray<int32> FBenchmarkResult::DefaultThreadCounts(int32 LogicalCores, int32 PhysicalCores)
{
	const int32 Logical = FMath::Max(1, LogicalCores);
	TArray<int32> Counts;
	for (int32 Count = 1; Count < Logical; Count *= 2)
	{
		Counts.Add(Count);
	}
	if (PhysicalCores > 0 && PhysicalCores < Logical)
	{
		Counts.AddUnique(PhysicalCores);
	}
	Counts.AddUnique(Logical);
	Counts.Sort();
	return Counts;
}

uint64 FBenchmarkResult::Fingerprint(const FSimulationResult& Result)
{
	uint64 Hash = FnvOffset;
	HashValue(Hash, Result.NumBeamNodes);
	if (const FBeamTree* Tree = Result.BeamTree.Get())
	{
		for (const FBeamNode& Node : Tree->Nodes)
		{
			HashVector(Hash, Node.Apex);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				HashVector(Hash, Node.Directions[Index]);
				HashVector(Hash, Node.EndPoints[Index]);
			}
			HashBytes(Hash, Node.Factor, sizeof(Node.Factor));
			HashValue(Hash, Node.Parent);
			HashValue(Hash, Node.HitTriangle);
			HashValue(Hash, Node.Order);
			HashValue(Hash, Node.SequenceHash);
		}
	}
	for (const FReceiverResult& Receiver : Result.Receivers)
	{
		const int32 NumReflections = Receiver.EarlyReflections.Num();
		HashValue(Hash, NumReflections);
		HashBytes(Hash, Receiver.Histogram.Energy.GetData(), Receiver.Histogram.Energy.Num() * sizeof(double));
	}
	return Hash;
}

TArray<FBenchmarkCell> FBenchmarkResult::Summarize() const
{
	TArray<FBenchmarkCell> Cells;
	for (const bool bBVH : { true, false })
	{
		for (const int32 Threads : ThreadCounts)
		{
			FBenchmarkCell Cell;
			Cell.Threads = Threads;
			Cell.bUseBVH = bBVH;
			Cell.MinSeconds = TNumericLimits<double>::Max();
			double Sum = 0.0;
			double SumSquares = 0.0;
			for (const FBenchmarkRun& Run : Runs)
			{
				if (Run.Threads != Threads || Run.bUseBVH != bBVH)
				{
					continue;
				}
				++Cell.NumRuns;
				Sum += Run.TotalSeconds;
				SumSquares += Run.TotalSeconds * Run.TotalSeconds;
				Cell.MinSeconds = FMath::Min(Cell.MinSeconds, Run.TotalSeconds);
				Cell.MaxSeconds = FMath::Max(Cell.MaxSeconds, Run.TotalSeconds);
				Cell.MeanBuildSeconds += Run.BuildSeconds;
				Cell.MeanQuerySeconds += Run.QuerySeconds;
				Cell.bAllIdentical &= Run.bIdentical;
			}
			if (Cell.NumRuns == 0)
			{
				continue;
			}
			Cell.MeanSeconds = Sum / Cell.NumRuns;
			Cell.StdDevSeconds = FMath::Sqrt(FMath::Max(0.0, SumSquares / Cell.NumRuns - Cell.MeanSeconds * Cell.MeanSeconds));
			Cell.MeanBuildSeconds /= Cell.NumRuns;
			Cell.MeanQuerySeconds /= Cell.NumRuns;
			Cells.Add(Cell);
		}
	}
	// Speedup against the smallest thread count of the same mode; BVH gain against the other mode at the same count.
	for (FBenchmarkCell& Cell : Cells)
	{
		const FBenchmarkCell* Base = nullptr;
		const FBenchmarkCell* Other = nullptr;
		for (const FBenchmarkCell& Candidate : Cells)
		{
			if (Candidate.bUseBVH == Cell.bUseBVH && (!Base || Candidate.Threads < Base->Threads))
			{
				Base = &Candidate;
			}
			if (Candidate.bUseBVH != Cell.bUseBVH && Candidate.Threads == Cell.Threads)
			{
				Other = &Candidate;
			}
		}
		if (Base && Cell.MeanSeconds > 0.0)
		{
			Cell.Speedup = Base->MeanSeconds / Cell.MeanSeconds;
			Cell.SpeedupMin = Base->MinSeconds / Cell.MinSeconds;
			Cell.Efficiency = Cell.Speedup * Base->Threads / Cell.Threads;
		}
		if (Other && Cell.MeanSeconds > 0.0)
		{
			Cell.BvhGain = Cell.bUseBVH ? Other->MeanSeconds / Cell.MeanSeconds : Cell.MeanSeconds / Other->MeanSeconds;
		}
	}
	return Cells;
}

bool FBenchmarkResult::AllIdentical() const
{
	for (const FBenchmarkRun& Run : Runs)
	{
		if (!Run.bIdentical)
		{
			return false;
		}
	}
	return Runs.Num() > 0;
}

FString FBenchmarkResult::RunsToCsv() const
{
	FString Csv;
	Csv += FString::Printf(TEXT("# AcousticsLab beam-tracer benchmark (M7), %s\n"), *Info.Timestamp.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Csv += FString::Printf(TEXT("# scene: %s; %d triangles (surface split %d); %d receivers; subdivision level %d (%d root beams); max beam order %d; refraction %s\n"),
		*Info.SceneName, Info.NumTriangles, Info.SurfaceDivisions, Info.NumReceivers, Info.SubdivisionLevel, Info.NumRootBeams, Info.MaxBeamOrder, OnOff(Info.bRefraction));
	Csv += FString::Printf(TEXT("# machine: %s; %d physical / %d logical cores; %.1f GB RAM, %.1f GB available at start\n"),
		*Info.CpuBrand, Info.PhysicalCores, Info.LogicalCores, Info.TotalPhysicalGB, Info.AvailablePhysicalGB);
	if (!Info.Note.IsEmpty())
	{
		Csv += FString::Printf(TEXT("# note: %s\n"), *Info.Note);
	}
	Csv += FString::Printf(TEXT("# %d repetitions, wall time %.1f s%s, all results identical: %s\n"), Info.Repetitions, WallSeconds, bCancelled ? TEXT(", CANCELLED") : TEXT(""), AllIdentical() ? TEXT("yes") : TEXT("no"));
	Csv += TEXT("threads,bvh,repetition,total_s,build_s,query_s,nodes,fingerprint,identical\n");
	for (const FBenchmarkRun& Run : Runs)
	{
		Csv += FString::Printf(TEXT("%d,%s,%d,%.6f,%.6f,%.6f,%d,%016llx,%s\n"), Run.Threads, OnOff(Run.bUseBVH), Run.Repetition + 1, Run.TotalSeconds, Run.BuildSeconds, Run.QuerySeconds,
			Run.NumNodes, Run.Fingerprint, Run.bIdentical ? TEXT("yes") : TEXT("no"));
	}
	return Csv;
}

FString FBenchmarkResult::SummaryToCsv() const
{
	FString Csv = TEXT("threads,bvh,runs,mean_s,min_s,max_s,stddev_s,mean_build_s,mean_query_s,speedup_mean,speedup_min,efficiency,bvh_gain,identical\n");
	for (const FBenchmarkCell& Cell : Summarize())
	{
		Csv += FString::Printf(TEXT("%d,%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.4f,%.4f,%.4f,%s\n"), Cell.Threads, OnOff(Cell.bUseBVH), Cell.NumRuns, Cell.MeanSeconds, Cell.MinSeconds, Cell.MaxSeconds,
			Cell.StdDevSeconds, Cell.MeanBuildSeconds, Cell.MeanQuerySeconds, Cell.Speedup, Cell.SpeedupMin, Cell.Efficiency, Cell.BvhGain, Cell.bAllIdentical ? TEXT("yes") : TEXT("no"));
	}
	return Csv;
}

FString FBenchmarkResult::DescribeTable() const
{
	FString Table = TEXT("Threads BVH   mean s    min s  speedup  eff.  BVH gain  same\n");
	for (const FBenchmarkCell& Cell : Summarize())
	{
		const FString Gain = Cell.bUseBVH && Cell.BvhGain > 0.0 ? FString::Printf(TEXT("%6.2fx"), Cell.BvhGain) : FString(TEXT("     -"));
		Table += FString::Printf(TEXT("%7d %-4s %8.3f %8.3f  %6.2fx %4.0f%%   %s   %s\n"), Cell.Threads, OnOff(Cell.bUseBVH), Cell.MeanSeconds, Cell.MinSeconds, Cell.Speedup,
			100.0 * Cell.Efficiency, *Gain, Cell.bAllIdentical ? TEXT("yes") : TEXT("NO"));
	}
	return Table;
}

FString FBenchmarkResult::MarkdownTable() const
{
	FString Table = TEXT("| threads | BVH | mean, s | min, s | build / queries, s | speedup T1/Tn (mean / min) | efficiency | BVH gain | identical |\n|---|---|---|---|---|---|---|---|---|\n");
	for (const FBenchmarkCell& Cell : Summarize())
	{
		const FString Gain = Cell.bUseBVH && Cell.BvhGain > 0.0 ? FString::Printf(TEXT("%.2f×"), Cell.BvhGain) : FString(TEXT("—"));
		Table += FString::Printf(TEXT("| %d | %s | %.3f | %.3f | %.3f / %.3f | %.2f / %.2f | %.0f %% | %s | %s |\n"), Cell.Threads, OnOff(Cell.bUseBVH), Cell.MeanSeconds, Cell.MinSeconds,
			Cell.MeanBuildSeconds, Cell.MeanQuerySeconds, Cell.Speedup, Cell.SpeedupMin, 100.0 * Cell.Efficiency, *Gain, Cell.bAllIdentical ? TEXT("yes") : TEXT("no"));
	}
	return Table;
}

bool FBenchmarkResult::SaveCsv(const FString& BasePath, FString& OutRunsPath, FString& OutSummaryPath) const
{
	OutRunsPath = BasePath + TEXT(".csv");
	OutSummaryPath = BasePath + TEXT("_summary.csv");
	const bool bRuns = FFileHelper::SaveStringToFile(RunsToCsv(), *OutRunsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	const bool bSummary = FFileHelper::SaveStringToFile(SummaryToCsv(), *OutSummaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	return bRuns && bSummary;
}
