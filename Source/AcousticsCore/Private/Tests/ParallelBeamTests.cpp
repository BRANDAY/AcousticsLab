// M7 tests (SPEC §2.7, §4): the parallel beam tracer gives the same tree and the same histograms for any thread count
// (subtrees per root beam merged in a fixed order), with and without the BVH, also when the node budget cuts the tree;
// the benchmark matrix and its summary.
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Results/BenchmarkResult.h"
#include "Solvers/BeamBenchmark.h"
#include "Solvers/BeamTracer.h"
#include "Solvers/ParallelBeamTracer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AcousticsParallelBeamTests
{
	static const FVector3d ShoeboxSource(-3.0, -1.5, 1.5);
	static const FVector3d PoolSource(-6.0, 0.0, 2.6);

	/** R1..R4 of the beam tests plus a 5 x 4 grid at 1.2 m: enough receivers for the query tasks to interleave. */
	static TArray<FSolverReceiver> ShoeboxReceivers()
	{
		TArray<FSolverReceiver> Receivers;
		Receivers.Emplace(TEXT("R1"), FVector3d(3.0, 1.5, 1.5), 0.5);
		Receivers.Emplace(TEXT("R2"), FVector3d(0.0, 2.5, 1.2), 0.5);
		Receivers.Emplace(TEXT("R3"), FVector3d(-2.0, -1.3, 1.4), 0.5);
		Receivers.Emplace(TEXT("R4"), FVector3d(4.3, 2.8, 1.0), 0.5);
		for (int32 X = 0; X < 5; ++X)
		{
			for (int32 Y = 0; Y < 4; ++Y)
			{
				Receivers.Emplace(FString::Printf(TEXT("G[%d,%d]"), X, Y), FVector3d(-4.0 + 2.0 * X, -2.7 + 1.8 * Y, 1.2), 0.5);
			}
		}
		return Receivers;
	}

	static TArray<FSolverReceiver> PoolReceivers()
	{
		TArray<FSolverReceiver> Receivers;
		Receivers.Emplace(TEXT("U1"), FVector3d(-6.0, 0.0, 1.0), 0.5);
		Receivers.Emplace(TEXT("U2"), FVector3d(2.0, 2.0, 0.8), 0.5);
		Receivers.Emplace(TEXT("A1"), FVector3d(3.0, -2.0, 2.6), 0.5);
		return Receivers;
	}

	static FAcousticScene Shoebox()
	{
		return FAcousticScene::MakeShoebox(FVector3d(10.0, 7.0, 3.0), FAcousticMaterialProps(TEXT("Test"), 0.1, 0.0));
	}

	/** The pool of RefractedBeamTreeDeterminism: 20 x 10 x 3 m of concrete, water surface at z = 2 (air above, water below). */
	static FAcousticScene Pool()
	{
		FAcousticScene Scene = FAcousticScene::MakeShoebox(FVector3d(20.0, 10.0, 3.0), FAcousticMaterialProps(TEXT("Concrete"), 0.03, 0.0));
		const int32 Water = Scene.AddMedium(FMedium::Water());
		const int32 Surface = Scene.AddMaterial(FAcousticMaterialProps(TEXT("WaterSurface"), 0.01, 0.0, true));
		FAcousticTriangle A(FVector3d(-10.0, -5.0, 2.0), FVector3d(10.0, -5.0, 2.0), FVector3d(10.0, 5.0, 2.0), Surface);
		FAcousticTriangle B(FVector3d(-10.0, -5.0, 2.0), FVector3d(10.0, 5.0, 2.0), FVector3d(-10.0, 5.0, 2.0), Surface);
		for (FAcousticTriangle* Triangle : { &A, &B })
		{
			Triangle->bInterface = true;
			Triangle->MediumFront = 0;
			Triangle->MediumBack = Water;
			Scene.AddTriangle(*Triangle);
		}
		Scene.BuildBVH();
		return Scene;
	}

	static FBeamTracerSettings Settings(int32 Level, int32 MaxOrder, bool bRefraction)
	{
		FBeamTracerSettings Result;
		Result.SubdivisionLevel = Level;
		Result.MaxBeamOrder = MaxOrder;
		Result.bRefraction = bRefraction;
		Result.MaxTimeSeconds = 1.0;
		return Result;
	}

	/** Same tree (bit for bit), same counters, same histograms and arrivals for every receiver. */
	static bool SameResult(FAutomationTestBase& Test, const FString& What, const FSimulationResult& A, const FSimulationResult& B)
	{
		bool bSame = A.bSuccess && B.bSuccess && A.BeamTree.IsValid() && B.BeamTree.IsValid();
		Test.TestTrue(FString::Printf(TEXT("%s: both runs succeeded"), *What), bSame);
		if (!bSame)
		{
			return false;
		}
		const FBeamTree& TreeA = *A.BeamTree;
		const FBeamTree& TreeB = *B.BeamTree;
		bSame &= Test.TestTrue(FString::Printf(TEXT("%s: bit-identical beam tree (%d / %d nodes)"), *What, TreeA.Nodes.Num(), TreeB.Nodes.Num()), TreeA.IsIdentical(TreeB, 0.0));
		bSame &= Test.TestTrue(FString::Printf(TEXT("%s: same nodes per order"), *What), TreeA.NumNodesPerOrder == TreeB.NumNodesPerOrder);
		bSame &= Test.TestTrue(FString::Printf(TEXT("%s: same counters"), *What), TreeA.NumEscaped == TreeB.NumEscaped && TreeA.NumUnresolved == TreeB.NumUnresolved && TreeA.NumSlivers == TreeB.NumSlivers
			&& TreeA.NumRefracted == TreeB.NumRefracted && TreeA.NumTotalInternalReflections == TreeB.NumTotalInternalReflections && TreeA.NumContinuations == TreeB.NumContinuations
			&& TreeA.bTruncated == TreeB.bTruncated && TreeA.NumRootBeamsEmitted == TreeB.NumRootBeamsEmitted);
		bSame &= Test.TestEqual(FString::Printf(TEXT("%s: same receiver count"), *What), A.Receivers.Num(), B.Receivers.Num());
		for (int32 Index = 0; Index < FMath::Min(A.Receivers.Num(), B.Receivers.Num()); ++Index)
		{
			const FReceiverResult& RA = A.Receivers[Index];
			const FReceiverResult& RB = B.Receivers[Index];
			const bool bReceiver = RA.Histogram.IsIdentical(RB.Histogram) && RA.EarlyReflections.Num() == RB.EarlyReflections.Num() && RA.ContainingBeamNodes == RB.ContainingBeamNodes
				&& RA.bDirectOccluded == RB.bDirectOccluded && RA.DirectEnergy == RB.DirectEnergy;
			if (!bReceiver)
			{
				Test.AddError(FString::Printf(TEXT("%s: receiver %s differs (%d vs %d arrivals, %d different bins)"), *What, *RA.Name, RA.EarlyReflections.Num(), RB.EarlyReflections.Num(),
					RA.Histogram.NumDifferentBins(RB.Histogram, 0)));
				bSame = false;
			}
		}
		return bSame;
	}

	static TArray<int32> ThreadCounts()
	{
		TArray<int32> Counts = { 1, 2, 4 };
		Counts.AddUnique(FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads()));
		return Counts;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsBeamThreadCountInvarianceTest, "AcousticsLab.Core.Solvers.BeamThreadCountInvariance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsBeamThreadCountInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsParallelBeamTests;

	struct FCase
	{
		const TCHAR* Name;
		FAcousticScene Scene;
		FVector3d Source;
		TArray<FSolverReceiver> Receivers;
		FBeamTracerSettings Settings;
	};
	TArray<FCase> Cases;
	Cases.Add({ TEXT("shoebox level 2 order 3"), Shoebox(), ShoeboxSource, ShoeboxReceivers(), Settings(2, 3, false) });
	Cases.Add({ TEXT("pool with refraction level 2 order 3"), Pool(), PoolSource, PoolReceivers(), Settings(2, 3, true) });

	for (const FCase& Case : Cases)
	{
		// Reference: the sequential tracer (one thread, inline on the caller).
		FBeamTracerSettings Sequential = Case.Settings;
		Sequential.NumThreads = 1;
		const FSimulationResult Reference = FBeamTracer::Run(Case.Scene, Case.Source, Case.Receivers, Sequential);
		TestEqual(FString::Printf(TEXT("%s: sequential run used one worker"), Case.Name), Reference.Timing.NumWorkers, 1);
		AddInfo(FString::Printf(TEXT("%s: %s"), Case.Name, *Reference.BeamTree->DescribeStats()));
		for (const int32 Threads : ThreadCounts())
		{
			FParallelBeamTracer Tracer(Threads, true);
			Tracer.bLogResults = false;
			TestTrue(FString::Printf(TEXT("%s: %d threads run on a dedicated pool"), Case.Name, Threads), Tracer.UsesPool());
			FBeamTracerSettings Parallel = Case.Settings;
			Parallel.NumThreads = Threads;
			const FSimulationResult Result = Tracer.Run(Case.Scene, Case.Source, Case.Receivers, Parallel);
			TestEqual(FString::Printf(TEXT("%s: %d threads reported"), Case.Name, Threads), Result.Timing.NumWorkers, Threads);
			SameResult(*this, FString::Printf(TEXT("%s, %d threads vs sequential"), Case.Name, Threads), Reference, Result);
			// The same pool again: no state survives between runs.
			const FSimulationResult Again = Tracer.Run(Case.Scene, Case.Source, Case.Receivers, Parallel);
			SameResult(*this, FString::Printf(TEXT("%s, %d threads, second run on the same pool"), Case.Name, Threads), Reference, Again);
		}
		// FBeamTracer::Run with NumThreads 0 (all logical cores, the HUD default) goes through the pool as well.
		FBeamTracerSettings Auto = Case.Settings;
		Auto.NumThreads = 0;
		SameResult(*this, FString::Printf(TEXT("%s, FBeamTracer with all cores"), Case.Name), Reference, FBeamTracer::Run(Case.Scene, Case.Source, Case.Receivers, Auto));
	}

	// A node budget that cuts the tree: the result is the breadth-first prefix of the full tree for any thread count. The
	// budget stays above half of the full tree so the memory guard (all subtrees together at 2 x MaxBeamNodes, which cuts
	// wherever the workers are) does not fire; only the deterministic cut of the merge applies.
	{
		const FAcousticScene Scene = Shoebox();
		FBeamTracerSettings Full = Settings(2, 3, false);
		Full.NumThreads = 1;
		const TSharedPtr<FBeamTree> FullTree = FBeamTracer::BuildTree(Scene, ShoeboxSource, Full);
		FBeamTracerSettings Budget = Full;
		Budget.MaxBeamNodes = FullTree->Nodes.Num() * 2 / 3;
		const TSharedPtr<FBeamTree> One = FBeamTracer::BuildTree(Scene, ShoeboxSource, Budget);
		FParallelBeamTracer Four(4, true);
		const TSharedPtr<FBeamTree> Parallel = Four.BuildTree(Scene, ShoeboxSource, Budget);
		TestTrue(TEXT("budget: the tree is marked truncated"), One->bTruncated && Parallel->bTruncated);
		TestEqual(TEXT("budget: exactly MaxBeamNodes nodes are kept"), One->Nodes.Num(), Budget.MaxBeamNodes);
		TestTrue(TEXT("budget: 1 and 4 threads give the same truncated tree"), One->IsIdentical(*Parallel, 0.0));
		bool bPrefix = One->Nodes.Num() <= FullTree->Nodes.Num();
		for (int32 Index = 0; bPrefix && Index < One->Nodes.Num(); ++Index)
		{
			const FBeamNode& A = One->Nodes[Index];
			const FBeamNode& B = FullTree->Nodes[Index];
			bPrefix = A.Parent == B.Parent && A.Order == B.Order && A.HitTriangle == B.HitTriangle && A.SequenceHash == B.SequenceHash && A.Apex == B.Apex;
		}
		TestTrue(TEXT("budget: the truncated tree is the breadth-first prefix of the full tree"), bPrefix);
		AddInfo(FString::Printf(TEXT("budget %d of %d nodes"), Budget.MaxBeamNodes, FullTree->Nodes.Num()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsBvhToggleInvarianceTest, "AcousticsLab.Core.Solvers.BvhToggleInvariance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsBvhToggleInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsParallelBeamTests;

	// Tessellation keeps the room: 12 * 3^2 triangles, the same area, the same materials.
	const FAcousticScene Room = Shoebox();
	const FAcousticScene Split = Room.Tessellated(3);
	TestEqual(TEXT("tessellated shoebox has 12 * 9 triangles"), Split.NumTriangles(), 12 * 9);
	TestTrue(TEXT("tessellation keeps the area"), FMath::IsNearlyEqual(Split.TotalArea(), Room.TotalArea(), 1e-9));
	TestTrue(TEXT("tessellated scene has its BVH"), Split.HasBVH());
	bool bNormals = true;
	for (const FAcousticTriangle& Triangle : Split.Triangles)
	{
		bNormals &= FVector3d::DotProduct(Triangle.N, FVector3d(0.0, 0.0, 1.5) - Triangle.Centroid()) > 0.0;
	}
	TestTrue(TEXT("tessellated triangles keep the inward normals"), bNormals);

	struct FCase
	{
		const TCHAR* Name;
		FAcousticScene Scene;
		FVector3d Source;
		TArray<FSolverReceiver> Receivers;
		FBeamTracerSettings Settings;
	};
	TArray<FCase> Cases;
	Cases.Add({ TEXT("shoebox"), Shoebox(), ShoeboxSource, ShoeboxReceivers(), Settings(2, 3, false) });
	Cases.Add({ TEXT("shoebox split 3 x 3"), Room.Tessellated(3), ShoeboxSource, ShoeboxReceivers(), Settings(1, 3, false) });
	Cases.Add({ TEXT("pool with refraction"), Pool(), PoolSource, PoolReceivers(), Settings(2, 2, true) });

	for (const FCase& Case : Cases)
	{
		for (const int32 Threads : { 1, 4 })
		{
			FParallelBeamTracer Tracer(Threads, true);
			Tracer.bLogResults = false;
			FBeamTracerSettings WithBvh = Case.Settings;
			WithBvh.NumThreads = Threads;
			FBeamTracerSettings BruteForce = WithBvh;
			BruteForce.bUseBVH = false;
			const FSimulationResult A = Tracer.Run(Case.Scene, Case.Source, Case.Receivers, WithBvh);
			const FSimulationResult B = Tracer.Run(Case.Scene, Case.Source, Case.Receivers, BruteForce);
			SameResult(*this, FString::Printf(TEXT("%s, %d thread(s): BVH on vs off"), Case.Name, Threads), A, B);
			if (Threads == 1)
			{
				AddInfo(FString::Printf(TEXT("%s: %d triangles, %d nodes, BVH %.3f s, brute force %.3f s"), Case.Name, Case.Scene.NumTriangles(), A.NumBeamNodes, A.Timing.TotalSeconds, B.Timing.TotalSeconds));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsBenchmarkMatrixTest, "AcousticsLab.Core.Solvers.BenchmarkMatrix",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsBenchmarkMatrixTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsParallelBeamTests;

	TestTrue(TEXT("default thread counts on 6 / 12 cores"), FBenchmarkResult::DefaultThreadCounts(12, 6) == TArray<int32>({ 1, 2, 4, 6, 8, 12 }));
	TestTrue(TEXT("default thread counts on 8 / 8 cores"), FBenchmarkResult::DefaultThreadCounts(8, 8) == TArray<int32>({ 1, 2, 4, 8 }));
	TestTrue(TEXT("default thread counts on 1 core"), FBenchmarkResult::DefaultThreadCounts(1, 1) == TArray<int32>({ 1 }));

	const FAcousticScene Scene = Shoebox();
	FBeamBenchmarkSettings Matrix;
	Matrix.ThreadCounts = { 1, 2 };
	Matrix.Repetitions = 2;
	FBenchmarkInfo Info;
	Info.SceneName = TEXT("Test shoebox");
	FBenchmarkProgress Progress;
	const FBenchmarkResult Result = FBeamBenchmark::Run(Scene, ShoeboxSource, ShoeboxReceivers(), Settings(1, 2, false), Matrix, Info, &Progress);
	TestEqual(TEXT("2 threads x 2 BVH modes x 2 repetitions"), Result.Runs.Num(), 8);
	TestEqual(TEXT("progress counted every run"), Progress.CompletedRuns.load(), 8);
	TestTrue(TEXT("every run gives the same result"), Result.AllIdentical());
	TestFalse(TEXT("not cancelled"), Result.bCancelled);
	TestTrue(TEXT("machine filled in"), Result.Info.LogicalCores > 0 && Result.Info.NumTriangles == 12);
	const TArray<FBenchmarkCell> Cells = Result.Summarize();
	TestEqual(TEXT("4 cells"), Cells.Num(), 4);
	if (Cells.Num() == 4)
	{
		TestTrue(TEXT("BVH cells first, threads ascending"), Cells[0].bUseBVH && Cells[0].Threads == 1 && Cells[1].Threads == 2 && !Cells[2].bUseBVH);
		TestTrue(TEXT("speedup of T1 is 1"), FMath::IsNearlyEqual(Cells[0].Speedup, 1.0) && FMath::IsNearlyEqual(Cells[2].Speedup, 1.0));
		TestTrue(TEXT("mean between min and max"), Cells[1].MinSeconds <= Cells[1].MeanSeconds && Cells[1].MeanSeconds <= Cells[1].MaxSeconds);
		TestTrue(TEXT("BVH gain filled for BVH cells"), Cells[0].BvhGain > 0.0);
	}
	const FString Csv = Result.RunsToCsv();
	TestTrue(TEXT("CSV has a header and 8 rows"), Csv.Contains(TEXT("threads,bvh,repetition")) && Csv.Contains(TEXT("2,off,2,")));
	AddInfo(Result.DescribeTable());

	// Cancel before the start: no runs.
	FBenchmarkProgress Cancelled;
	Cancelled.RequestCancel();
	const FBenchmarkResult Stopped = FBeamBenchmark::Run(Scene, ShoeboxSource, ShoeboxReceivers(), Settings(1, 2, false), Matrix, Info, &Cancelled);
	TestTrue(TEXT("cancelled matrix has no runs"), Stopped.bCancelled && Stopped.Runs.Num() == 0);
	return true;
}

#endif
