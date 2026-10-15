// M3 core tests: path sampling for visualization (cap, stride coverage, histogram untouched, thread invariance) and
// the level helpers used by the heatmap and the impulse-response plot.
#include "AcousticsConstants.h"
#include "Geometry/AcousticScene.h"
#include "Misc/AutomationTest.h"
#include "Results/RoomMetrics.h"
#include "Solvers/RayTracer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AcousticsVisualizationTests
{
	static const FVector3d RoomSize(10.0, 7.0, 3.0);
	static const FVector3d SourcePosition(-3.0, -1.5, 1.5);

	static TArray<FSolverReceiver> ShoeboxReceivers()
	{
		TArray<FSolverReceiver> Receivers;
		Receivers.Emplace(TEXT("R1"), FVector3d(3.0, 1.5, 1.5), 0.5);
		Receivers.Emplace(TEXT("R2"), FVector3d(0.0, 2.5, 1.2), 0.5);
		return Receivers;
	}

	static FRayTracerSettings TestSettings(int32 NumRays, int32 MaxPaths, int32 NumThreads = 1)
	{
		FRayTracerSettings Settings;
		Settings.NumRays = NumRays;
		Settings.Seed = 5;
		Settings.NumThreads = NumThreads;
		Settings.MaxPathsForVisualization = MaxPaths;
		Settings.RaysPerChunk = 500; // fixed for every run: the chunk-ordered reduction is bit-identical for any worker count at a fixed chunk size
		return Settings;
	}

	/** Distance from a point to the segment AB. */
	static double DistanceToSegment(const FVector3d& Point, const FVector3d& A, const FVector3d& B)
	{
		const FVector3d AB = B - A;
		const double Length2 = AB.SizeSquared();
		const double T = Length2 > 0.0 ? FMath::Clamp(FVector3d::DotProduct(Point - A, AB) / Length2, 0.0, 1.0) : 0.0;
		return (Point - (A + AB * T)).Length();
	}

	static bool SamePath(const FPathSample& A, const FPathSample& B)
	{
		if (A.RayIndex != B.RayIndex || A.Order != B.Order || A.FinalEnergy != B.FinalEnergy || A.bReachedReceiver != B.bReachedReceiver
			|| A.FirstReceiverHitOrder != B.FirstReceiverHitOrder || A.FirstReceiverIndex != B.FirstReceiverIndex
			|| A.Points != B.Points || A.SegmentEnergies != B.SegmentEnergies || A.ReceiverHits.Num() != B.ReceiverHits.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.ReceiverHits.Num(); ++Index)
		{
			const FPathReceiverHit& HitA = A.ReceiverHits[Index];
			const FPathReceiverHit& HitB = B.ReceiverHits[Index];
			if (HitA.ReceiverIndex != HitB.ReceiverIndex || HitA.Order != HitB.Order || HitA.EntryPoint != HitB.EntryPoint || HitA.TimeSeconds != HitB.TimeSeconds || HitA.Energy != HitB.Energy)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsPathSamplingTest, "AcousticsLab.Core.Solvers.PathSampling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsPathSamplingTest::RunTest(const FString& Parameters)
{
	using namespace AcousticsVisualizationTests;

	// Stride and count helpers.
	TestEqual(TEXT("stride 20000 / 2000"), FRayTracer::PathSampleStride(20000, 2000), 10);
	TestEqual(TEXT("stride 25000 / 2000"), FRayTracer::PathSampleStride(25000, 2000), 12);
	TestEqual(TEXT("stride when fewer rays than paths"), FRayTracer::PathSampleStride(100, 2000), 1);
	TestEqual(TEXT("stride with sampling off"), FRayTracer::PathSampleStride(20000, 0), 1);
	TestEqual(TEXT("count 20000 / 2000"), FRayTracer::NumPathSamples(20000, 2000), 2000);
	TestEqual(TEXT("count 25000 / 2000 is capped"), FRayTracer::NumPathSamples(25000, 2000), 2000);
	TestEqual(TEXT("count 20001 / 2000 is capped"), FRayTracer::NumPathSamples(20001, 2000), 2000);
	TestEqual(TEXT("count with fewer rays than paths"), FRayTracer::NumPathSamples(100, 2000), 100);
	TestEqual(TEXT("count with sampling off"), FRayTracer::NumPathSamples(20000, 0), 0);
	for (const int32 NumRays : { 1, 7, 100, 2000, 20001, 25000 })
	{
		for (const int32 MaxPaths : { 0, 1, 3, 2000, 5000 })
		{
			int32 Count = 0;
			for (int32 RayIndex = 0; RayIndex < NumRays; ++RayIndex)
			{
				Count += FRayTracer::IsPathSampled(RayIndex, NumRays, MaxPaths) ? 1 : 0;
			}
			TestEqual(FString::Printf(TEXT("IsPathSampled count for %d rays, %d paths"), NumRays, MaxPaths), Count, FRayTracer::NumPathSamples(NumRays, MaxPaths));
			TestTrue(FString::Printf(TEXT("cap holds for %d rays, %d paths"), NumRays, MaxPaths), Count <= FMath::Min(MaxPaths, NumRays));
		}
	}
	TestFalse(TEXT("ray 0 of 10 is not sampled with sampling off"), FRayTracer::IsPathSampled(0, 10, 0));
	TestTrue(TEXT("ray 0 is sampled"), FRayTracer::IsPathSampled(0, 20000, 2000));
	TestFalse(TEXT("ray 5 is skipped at stride 10"), FRayTracer::IsPathSampled(5, 20000, 2000));
	TestTrue(TEXT("ray 19990 is the last sample"), FRayTracer::IsPathSampled(19990, 20000, 2000));

	// Three runs that differ only in the path cap: identical histograms, different path counts.
	const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Validation"), 0.2, 0.3));
	const TArray<FSolverReceiver> Receivers = ShoeboxReceivers();
	const int32 NumRays = 8000;

	const FSimulationResult NoPaths = FRayTracer::Run(Scene, SourcePosition, Receivers, TestSettings(NumRays, 0));
	const FSimulationResult Hundred = FRayTracer::Run(Scene, SourcePosition, Receivers, TestSettings(NumRays, 100));
	const FSimulationResult Many = FRayTracer::Run(Scene, SourcePosition, Receivers, TestSettings(NumRays, 5000));
	TestTrue(TEXT("runs succeeded"), NoPaths.bSuccess && Hundred.bSuccess && Many.bSuccess);
	TestEqual(TEXT("no paths when the cap is 0"), NoPaths.Paths.Num(), 0);
	TestEqual(TEXT("100 paths"), Hundred.Paths.Num(), 100);
	TestEqual(TEXT("5000 paths"), Many.Paths.Num(), 5000);
	TestEqual(TEXT("stride of the 100-path run"), Hundred.PathSampleStride, 80);
	TestEqual(TEXT("stride of the 5000-path run"), Many.PathSampleStride, 1);
	for (int32 Index = 0; Index < Receivers.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("%s: histogram identical with 0 and 100 paths"), *Receivers[Index].Name), NoPaths.Receivers[Index].Histogram.IsIdentical(Hundred.Receivers[Index].Histogram));
		TestTrue(FString::Printf(TEXT("%s: histogram identical with 0 and 5000 paths"), *Receivers[Index].Name), NoPaths.Receivers[Index].Histogram.IsIdentical(Many.Receivers[Index].Histogram));
		TestEqual(FString::Printf(TEXT("%s: same hit count"), *Receivers[Index].Name), NoPaths.Receivers[Index].NumRayHits, Many.Receivers[Index].NumRayHits);
	}
	TestEqual(TEXT("same segment count with and without paths"), NoPaths.NumSegments, Many.NumSegments);
	TestTrue(TEXT("same mean order with and without paths"), NoPaths.MeanOrder == Many.MeanOrder);

	// The 100 samples are rays 0, 80, 160, ... and cover both hemispheres (the first 100 rays would all point up).
	{
		double SumZ = 0.0;
		int32 Upper = 0;
		for (int32 Index = 0; Index < Hundred.Paths.Num(); ++Index)
		{
			const FPathSample& Path = Hundred.Paths[Index];
			TestEqual(FString::Printf(TEXT("sample %d is ray %d"), Index, Index * 80), Path.RayIndex, Index * 80);
			if (Path.Points.Num() >= 2)
			{
				const FVector3d Direction = (Path.Points[1] - Path.Points[0]).GetSafeNormal();
				TestTrue(TEXT("first segment follows the Fibonacci direction of the ray"), FVector3d::DotProduct(Direction, FRayTracer::FibonacciDirection(Path.RayIndex, NumRays)) > 0.9999);
				SumZ += Direction.Z;
				Upper += Direction.Z > 0.0 ? 1 : 0;
			}
		}
		TestTrue(FString::Printf(TEXT("sampled directions are balanced in Z (mean %.3f)"), SumZ / 100.0), FMath::Abs(SumZ / 100.0) < 0.15);
		TestTrue(FString::Printf(TEXT("both hemispheres are sampled (%d up)"), Upper), Upper >= 35 && Upper <= 65);
	}

	// Per-path invariants, accumulated over the 5000 samples and asserted once per category.
	{
		const double SpeedOfSound = Scene.GetMedium(0).SpeedOfSound;
		int32 NumReached = 0;
		int32 NumHits = 0;
		int32 BadStart = 0, BadEnergyCount = 0, BadPointCount = 0, BadFirstEnergy = 0, NotMonotonic = 0, BadFinal = 0, BadReachedFlag = 0, BadFirstHit = 0, BadLookup = 0;
		int32 BadHitIndex = 0, BadHitOrder = 0, HitNotOnSphere = 0, HitNotOnSegment = 0, BadHitTime = 0, BadHitEnergy = 0, HitsOutOfOrder = 0;
		double MaxTimeError = 0.0;
		for (const FPathSample& Path : Many.Paths)
		{
			BadStart += (Path.Points.Num() > 0 && Path.Points[0].Equals(SourcePosition, 1e-9)) ? 0 : 1;
			BadEnergyCount += Path.SegmentEnergies.Num() == Path.NumSegments() ? 0 : 1;
			BadPointCount += (Path.Points.Num() == Path.Order + 1 || Path.Points.Num() == Path.Order + 2) ? 0 : 1;
			BadFirstEnergy += (Path.SegmentEnergies.Num() > 0 && Path.SegmentEnergies[0] == 1.0) ? 0 : 1;
			for (int32 Segment = 1; Segment < Path.SegmentEnergies.Num(); ++Segment)
			{
				if (Path.SegmentEnergies[Segment] > Path.SegmentEnergies[Segment - 1])
				{
					++NotMonotonic;
					break;
				}
			}
			BadFinal += (Path.SegmentEnergies.Num() > 0 && Path.FinalEnergy >= 0.0 && Path.FinalEnergy <= Path.SegmentEnergies.Last()) ? 0 : 1;
			BadReachedFlag += Path.bReachedReceiver == (Path.ReceiverHits.Num() > 0) ? 0 : 1;
			if (Path.ReceiverHits.Num() > 0)
			{
				++NumReached;
				BadFirstHit += (Path.FirstReceiverHitOrder == Path.ReceiverHits[0].Order && Path.FirstReceiverIndex == Path.ReceiverHits[0].ReceiverIndex) ? 0 : 1;
				const FPathReceiverHit* R2Hit = Path.FindReceiverHit(1);
				BadLookup += (Path.FindReceiverHit(INDEX_NONE) == &Path.ReceiverHits[0] && (R2Hit == nullptr || R2Hit->ReceiverIndex == 1)) ? 0 : 1;
			}
			else
			{
				BadFirstHit += Path.FirstReceiverHitOrder == INDEX_NONE ? 0 : 1;
				BadLookup += Path.FindReceiverHit(INDEX_NONE) == nullptr ? 0 : 1;
			}

			// Cumulative length up to every point for the arrival times. Reflected segments start SurfaceOffset (1e-7 m)
			// off the surface, so the time from the drawn points is exact to ~1e-7 m per reflection.
			TArray<double> Cumulative;
			Cumulative.Add(0.0);
			for (int32 Index = 1; Index < Path.Points.Num(); ++Index)
			{
				Cumulative.Add(Cumulative.Last() + (Path.Points[Index] - Path.Points[Index - 1]).Length());
			}
			int32 LastOrder = -1;
			for (const FPathReceiverHit& Hit : Path.ReceiverHits)
			{
				++NumHits;
				HitsOutOfOrder += Hit.Order >= LastOrder ? 0 : 1;
				LastOrder = Hit.Order;
				if (!Receivers.IsValidIndex(Hit.ReceiverIndex))
				{
					++BadHitIndex;
					continue;
				}
				if (Hit.Order < 1 || Hit.Order >= Path.NumSegments())
				{
					++BadHitOrder;
					continue;
				}
				const FSolverReceiver& Receiver = Receivers[Hit.ReceiverIndex];
				HitNotOnSphere += FMath::IsNearlyEqual((Hit.EntryPoint - Receiver.Position).Length(), Receiver.Radius, 1e-6) ? 0 : 1;
				HitNotOnSegment += DistanceToSegment(Hit.EntryPoint, Path.Points[Hit.Order], Path.Points[Hit.Order + 1]) < 1e-6 ? 0 : 1;
				const double ExpectedTime = (Cumulative[Hit.Order] + (Hit.EntryPoint - Path.Points[Hit.Order]).Length()) / SpeedOfSound;
				const double TimeError = FMath::Abs(Hit.TimeSeconds - ExpectedTime);
				MaxTimeError = FMath::Max(MaxTimeError, TimeError);
				BadHitTime += TimeError <= (Hit.Order + 1) * FRayTracer::SurfaceOffset / SpeedOfSound + 1e-9 ? 0 : 1;
				BadHitEnergy += Hit.Energy == Path.SegmentEnergies[Hit.Order] ? 0 : 1;
			}
		}
		TestEqual(TEXT("paths start at the source"), BadStart, 0);
		TestEqual(TEXT("one energy per segment"), BadEnergyCount, 0);
		TestEqual(TEXT("points = reflections + 1 (+ 1 when the ray escaped)"), BadPointCount, 0);
		TestEqual(TEXT("first segment carries E0"), BadFirstEnergy, 0);
		TestEqual(TEXT("segment energies never increase"), NotMonotonic, 0);
		TestEqual(TEXT("final energy does not exceed the last segment"), BadFinal, 0);
		TestEqual(TEXT("bReachedReceiver matches the hit list"), BadReachedFlag, 0);
		TestEqual(TEXT("first-hit fields match the hit list"), BadFirstHit, 0);
		TestEqual(TEXT("FindReceiverHit lookups"), BadLookup, 0);
		TestEqual(TEXT("hit receiver indices are valid"), BadHitIndex, 0);
		TestEqual(TEXT("hit orders are reflection orders of the path"), BadHitOrder, 0);
		TestEqual(TEXT("hits are listed in path order"), HitsOutOfOrder, 0);
		TestEqual(TEXT("entry points lie on the receiver sphere"), HitNotOnSphere, 0);
		TestEqual(TEXT("entry points lie on their segment"), HitNotOnSegment, 0);
		TestEqual(FString::Printf(TEXT("arrival times match the path length (max error %.3g s)"), MaxTimeError), BadHitTime, 0);
		TestEqual(TEXT("deposited energy is the segment energy (no air absorption)"), BadHitEnergy, 0);
		TestTrue(FString::Printf(TEXT("some sampled paths reach a receiver (%d of %d, %d hits)"), NumReached, Many.Paths.Num(), NumHits), NumReached > 100);
	}

	// The worker count does not change the samples (same RaysPerChunk: the reduction is chunk-ordered).
	{
		const FRayTracerSettings Settings = TestSettings(NumRays, 5000, 4);
		const FSimulationResult Four = FRayTracer::Run(Scene, SourcePosition, Receivers, Settings);
		TestEqual(TEXT("four workers"), Four.Timing.NumWorkers, 4);
		TestEqual(TEXT("same path count on four workers"), Four.Paths.Num(), Many.Paths.Num());
		bool bSame = Four.Paths.Num() == Many.Paths.Num();
		for (int32 Index = 0; bSame && Index < Four.Paths.Num(); ++Index)
		{
			bSame = SamePath(Four.Paths[Index], Many.Paths[Index]);
		}
		TestTrue(TEXT("paths are bit-identical for 1 and 4 workers"), bSame);
		for (int32 Index = 0; Index < Receivers.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s: histogram identical on four workers"), *Receivers[Index].Name), Four.Receivers[Index].Histogram.IsIdentical(Many.Receivers[Index].Histogram));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsLevelHelpersTest, "AcousticsLab.Core.Metrics.LevelHelpers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsLevelHelpersTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("0 dB at the reference"), FMath::IsNearlyEqual(RoomAcoustics::LevelDb(1.0, 1.0), 0.0, 1e-12));
	TestTrue(TEXT("-10 dB at a tenth"), FMath::IsNearlyEqual(RoomAcoustics::LevelDb(0.1, 1.0), -10.0, 1e-9));
	TestTrue(TEXT("+3 dB at double"), FMath::IsNearlyEqual(RoomAcoustics::LevelDb(2.0, 1.0), 3.0103, 1e-4));
	TestEqual(TEXT("silence for zero energy"), RoomAcoustics::LevelDb(0.0, 1.0), RoomAcoustics::SilenceDb);
	TestEqual(TEXT("silence for a zero reference"), RoomAcoustics::LevelDb(1.0, 0.0), RoomAcoustics::SilenceDb);
	TestEqual(TEXT("silence for negative energy"), RoomAcoustics::LevelDb(-1.0, 1.0), RoomAcoustics::SilenceDb);

	// Free field: L_p = L_W - 11 - 20 log10(d) for the captured fraction r^2 / (4 d^2), whatever the radius.
	for (const double Radius : { 0.25, 0.5, 1.0 })
	{
		for (const double Distance : { 1.0, 3.0, 10.0 })
		{
			const double Captured = Radius * Radius / (4.0 * Distance * Distance);
			const double Expected = 100.0 - 10.0 * FMath::LogX(10.0, 4.0 * UE_DOUBLE_PI) - 20.0 * FMath::LogX(10.0, Distance);
			TestTrue(FString::Printf(TEXT("direct SPL at %.0f m, r %.2f"), Distance, Radius), FMath::IsNearlyEqual(RoomAcoustics::DirectSoundLevelDb(Captured, Radius, 100.0), Expected, 1e-9));
		}
	}
	TestTrue(TEXT("89.0 dB at 1 m for L_W 100 dB"), FMath::IsNearlyEqual(RoomAcoustics::DirectSoundLevelDb(0.25 * 0.25 / 4.0, 0.25, 100.0), 89.0, 0.01));
	TestEqual(TEXT("occluded direct sound"), RoomAcoustics::DirectSoundLevelDb(0.0, 0.5, 100.0), RoomAcoustics::SilenceDb);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
